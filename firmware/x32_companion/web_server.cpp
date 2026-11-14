#include "web_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "mixer_state.h"
#include "webui_assets.h"
#include "wifi_manager.h"
#include "x32_icons.h"

namespace {

static constexpr size_t JSON_BUF_CAP = 32768;
static constexpr size_t METER_BUF_CAP = 1024;
static constexpr size_t SESSION_TOKEN_LEN = 52;
static constexpr int MAX_HTTP_SOCKETS = 7;

httpd_handle_t s_server = nullptr;
std::atomic<int> s_pendingWsJobs{0};
std::atomic<bool> s_forceClientRecount{false};
uint32_t s_lastStatePushMs = 0;
uint32_t s_lastMeterPushMs = 0;
uint32_t s_lastClientCountPollMs = 0;
int s_failedAuthCount = 0;
uint32_t s_authLockoutUntilMs = 0;
uint32_t s_activeAuthEpoch = 0;

struct WsWorkMsg {
  size_t len;
  char data[1];
};

void setSecurityHeaders(httpd_req_t *req, const char *cacheControl) {
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  if (cacheControl) {
    httpd_resp_set_hdr(req, "Cache-Control", cacheControl);
  }
}

bool isValidSessionTokenFmt(const char *s, size_t len) {
  if (!s || len != SESSION_TOKEN_LEN) return false;
  if (s[0] != 's' || s[1] != '1' || s[2] != '.' || s[19] != '.') return false;
  for (size_t i = 3; i < SESSION_TOKEN_LEN; ++i) {
    if (i == 19) continue;
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool extractCookieSessionToken(const char *cookieHdr, char *out, size_t cap) {
  if (!cookieHdr || !out || cap <= SESSION_TOKEN_LEN) return false;
  const char *p = cookieHdr;
  while (*p) {
    while (*p == ' ' || *p == ';') ++p;
    if (std::strncmp(p, "x32_session=", 12) == 0) {
      p += 12;
      size_t n = 0;
      while (p[n] && p[n] != ';' && p[n] != ' ' && n + 1 < cap) {
        out[n] = p[n];
        ++n;
      }
      out[n] = '\0';
      return isValidSessionTokenFmt(out, n);
    }
    const char *semi = std::strchr(p, ';');
    if (!semi) break;
    p = semi + 1;
  }
  return false;
}

bool extractTokenFromReq(httpd_req_t *req, char *out, size_t cap) {
  if (!req || !out || cap <= SESSION_TOKEN_LEN) return false;
  out[0] = '\0';

  if (httpd_req_get_hdr_value_len(req, "X-Session-Token") == SESSION_TOKEN_LEN &&
      httpd_req_get_hdr_value_str(req, "X-Session-Token", out, cap) == ESP_OK) {
    if (isValidSessionTokenFmt(out, std::strlen(out))) return true;
  }

  const size_t cLen = httpd_req_get_hdr_value_len(req, "Cookie");
  if (cLen > 0 && cLen < 512) {
    char cBuf[512];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cBuf, sizeof(cBuf)) == ESP_OK &&
        extractCookieSessionToken(cBuf, out, cap)) {
      return true;
    }
  }

  out[0] = '\0';
  return false;
}

bool isRequestAuthorized(httpd_req_t *req) {
  char tok[64] = {0};
  if (!extractTokenFromReq(req, tok, sizeof(tok))) return false;
  return wifiManagerVerifySessionToken(tok);
}

esp_err_t sendUnauthorizedJson(httpd_req_t *req) {
  setSecurityHeaders(req, "no-store");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, "401 Unauthorized");
  return httpd_resp_sendstr(req, "{\"ok\":false,\"authRequired\":true,\"error\":\"Pairing PIN required\"}");
}

bool isAuthorizedWsFd(int fd) {
  return s_server && fd >= 0 &&
         s_activeAuthEpoch == wifiManagerGetAuthEpoch() &&
         httpd_ws_get_fd_info(s_server, fd) == HTTPD_WS_CLIENT_WEBSOCKET;
}

bool matchUriWithWsAuth(const char *ref, const char *uri, size_t len) {
  if (!ref || !uri) return false;
  if (std::strcmp(ref, "/ws/*") == 0) {
    if (len == 5 && std::strncmp(uri, "/ws/*", 5) == 0) return true;
    if (len >= 4 + SESSION_TOKEN_LEN && std::strncmp(uri, "/ws/", 4) == 0 &&
        (len == 4 + SESSION_TOKEN_LEN || uri[4 + SESSION_TOKEN_LEN] == '?')) {
      char reqTok[SESSION_TOKEN_LEN + 1];
      std::memcpy(reqTok, uri + 4, SESSION_TOKEN_LEN);
      reqTok[SESSION_TOKEN_LEN] = '\0';
      if (!wifiManagerVerifySessionToken(reqTok)) return false;
      s_forceClientRecount.store(true);
      mixerRestoreWsDirtyFlags(DIRTY_OVERVIEW | DIRTY_HEADER);
      return true;
    }
    return false;
  }
  if (std::strcmp(ref, "/ws*") == 0) {
    if (len == 5 && std::strncmp(uri, "/ws/*", 5) == 0) return false;
    if (len == 4 && std::strncmp(uri, "/ws*", 4) == 0) return true;
    return (len == 3 && std::strncmp(uri, "/ws", 3) == 0) ||
           (len >= 4 && std::strncmp(uri, "/ws/", 4) == 0);
  }
  return std::strlen(ref) == len && std::strncmp(ref, uri, len) == 0;
}

esp_err_t handleWsUnauth(httpd_req_t *req) {
  return sendUnauthorizedJson(req);
}

esp_err_t handleRootGet(httpd_req_t *req) {
  setSecurityHeaders(req, "no-store");
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, reinterpret_cast<const char *>(WEBUI_INDEX_GZ), WEBUI_INDEX_GZ_LEN);
}

esp_err_t handleIconsGet(httpd_req_t *req) {
  setSecurityHeaders(req, "public, max-age=86400");
  httpd_resp_set_type(req, "application/octet-stream");
  return httpd_resp_send(req, reinterpret_cast<const char *>(kX32IconBitmaps), sizeof(kX32IconBitmaps));
}

esp_err_t handleAuthPost(httpd_req_t *req) {
  setSecurityHeaders(req, "no-store");
  httpd_resp_set_type(req, "application/json");

  const uint32_t now = millis();
  if (s_failedAuthCount >= 5 && static_cast<int32_t>(now - s_authLockoutUntilMs) < 0) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Too many attempts, wait 3s\"}");
  }

  if (req->content_len <= 0 || req->content_len > 128) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Bad request\"}");
  }

  char body[129];
  int total = 0;
  while (total < req->content_len) {
    int r = httpd_req_recv(req, body + total, req->content_len - total);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
      return ESP_FAIL;
    }
    total += r;
  }
  body[total] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, body, static_cast<size_t>(total)) != DeserializationError::Ok) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Invalid JSON\"}");
  }

  const char *submitted = doc["pin"];
  if (!submitted || !submitted[0]) {
    submitted = doc["key"] | "";
  }
  char token[64] = {0};
  if (!wifiManagerExchangePinForToken(submitted, token, sizeof(token))) {
    s_failedAuthCount++;
    if (s_failedAuthCount >= 5) {
      s_authLockoutUntilMs = now + 3000;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Invalid or expired 6-digit pairing PIN\"}");
  }

  s_failedAuthCount = 0;
  char cookieHdr[144];
  std::snprintf(cookieHdr, sizeof(cookieHdr),
                "x32_session=%s; Path=/; Max-Age=31536000; HttpOnly; SameSite=Lax", token);
  httpd_resp_set_hdr(req, "Set-Cookie", cookieHdr);
  char respJson[112];
  std::snprintf(respJson, sizeof(respJson), "{\"ok\":true,\"token\":\"%s\"}", token);
  return httpd_resp_sendstr(req, respJson);
}

esp_err_t handleLogoutPost(httpd_req_t *req) {
  setSecurityHeaders(req, "no-store");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Set-Cookie",
                     "x32_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  httpd_resp_set_hdr(req, "Clear-Site-Data", "\"cookies\", \"storage\"");
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handleStateGet(httpd_req_t *req) {
  char tok[64] = {0};
  if (!extractTokenFromReq(req, tok, sizeof(tok)) || !wifiManagerVerifySessionToken(tok)) {
    return sendUnauthorizedJson(req);
  }
  char *buf = static_cast<char *>(
      heap_caps_malloc(JSON_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<char *>(malloc(JSON_BUF_CAP));
  }
  if (!buf) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    return ESP_FAIL;
  }

  const size_t n = mixerBuildFullStateJson(buf, JSON_BUF_CAP);
  setSecurityHeaders(req, "no-store");
  httpd_resp_set_hdr(req, "X-Session-Token", tok);
  httpd_resp_set_type(req, "application/json");
  const esp_err_t err = httpd_resp_send(req, buf, static_cast<ssize_t>(n));
  free(buf);
  return err;
}

esp_err_t handleCmdPost(httpd_req_t *req) {
  if (!isRequestAuthorized(req)) {
    return sendUnauthorizedJson(req);
  }
  if (req->content_len <= 0 || req->content_len > 1024) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad content length");
    return ESP_FAIL;
  }

  char body[1025];
  int total = 0;
  while (total < req->content_len) {
    int r = httpd_req_recv(req, body + total, req->content_len - total);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
      return ESP_FAIL;
    }
    total += r;
  }
  body[total] = '\0';

  AppCommand cmd;
  if (!mixerParseJsonCommand(body, static_cast<size_t>(total), &cmd)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON cmd");
    return ESP_FAIL;
  }

  enqueueAppCommand(cmd);
  setSecurityHeaders(req, "no-store");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handleWs(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    s_forceClientRecount.store(true);
    mixerRestoreWsDirtyFlags(DIRTY_OVERVIEW | DIRTY_HEADER);
    return ESP_OK;
  }

  const int fd = httpd_req_to_sockfd(req);
  if (!isAuthorizedWsFd(fd)) {
    if (fd >= 0) httpd_sess_trigger_close(s_server, fd);
    return ESP_FAIL;
  }

  httpd_ws_frame_t pkt = {};
  pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
  if (ret != ESP_OK) return ret;

  if (pkt.len == 0 || pkt.len > 1024) {
    return ESP_OK;
  }

  uint8_t *payload = static_cast<uint8_t *>(malloc(pkt.len + 1));
  if (!payload) return ESP_ERR_NO_MEM;
  pkt.payload = payload;
  ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
  if (ret == ESP_OK && pkt.type == HTTPD_WS_TYPE_TEXT) {
    payload[pkt.len] = '\0';
    AppCommand cmd;
    if (mixerParseJsonCommand(reinterpret_cast<const char *>(payload), pkt.len, &cmd)) {
      enqueueAppCommand(cmd);
    }
  }
  free(payload);
  return ret;
}

void wsBroadcastWorkFn(void *arg) {
  WsWorkMsg *msg = static_cast<WsWorkMsg *>(arg);
  if (msg && s_server) {
    int fds[MAX_HTTP_SOCKETS] = {};
    size_t numFds = MAX_HTTP_SOCKETS;
    if (httpd_get_client_list(s_server, &numFds, fds) == ESP_OK) {
      httpd_ws_frame_t frame = {};
      frame.final = true;
      frame.fragmented = false;
      frame.type = HTTPD_WS_TYPE_TEXT;
      frame.payload = reinterpret_cast<uint8_t *>(msg->data);
      frame.len = msg->len;

      for (size_t i = 0; i < numFds; ++i) {
        if (isAuthorizedWsFd(fds[i])) {
          httpd_ws_send_frame_async(s_server, fds[i], &frame);
        }
      }
    }
  }
  if (msg) free(msg);
  s_pendingWsJobs.fetch_sub(1);
}

int countWsClients() {
  if (!s_server) return 0;
  int fds[MAX_HTTP_SOCKETS] = {};
  size_t numFds = MAX_HTTP_SOCKETS;
  if (httpd_get_client_list(s_server, &numFds, fds) != ESP_OK) return 0;
  int wsCount = 0;
  for (size_t i = 0; i < numFds; ++i) {
    if (isAuthorizedWsFd(fds[i])) {
      wsCount++;
    }
  }
  return wsCount;
}

bool queueWsJson(bool fullState) {
  if (!s_server || s_pendingWsJobs.load() >= 2) return false;
  const size_t cap = fullState ? JSON_BUF_CAP : METER_BUF_CAP;
  WsWorkMsg *msg = static_cast<WsWorkMsg *>(
      heap_caps_malloc(sizeof(WsWorkMsg) + cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!msg) {
    msg = static_cast<WsWorkMsg *>(malloc(sizeof(WsWorkMsg) + cap));
  }
  if (!msg) return false;

  msg->len = fullState ? mixerBuildFullStateJson(msg->data, cap)
                       : mixerBuildMetersJson(msg->data, cap);
  if (msg->len == 0) {
    free(msg);
    return false;
  }

  s_pendingWsJobs.fetch_add(1);
  if (httpd_queue_work(s_server, wsBroadcastWorkFn, msg) != ESP_OK) {
    s_pendingWsJobs.fetch_sub(1);
    free(msg);
    return false;
  }
  return true;
}

}  // namespace

void webServerInit() {
  if (s_server) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_open_sockets = MAX_HTTP_SOCKETS;
  config.max_uri_handlers = 10;
  config.lru_purge_enable = true;
  config.stack_size = 8192;
  config.core_id = 0;
  config.uri_match_fn = matchUriWithWsAuth;

  if (httpd_start(&s_server, &config) != ESP_OK) {
    Serial.println("[HTTP] Failed to start httpd");
    s_server = nullptr;
    return;
  }

  s_activeAuthEpoch = wifiManagerGetAuthEpoch();

  static const httpd_uri_t kRootUri = {
      .uri = "/", .method = HTTP_GET, .handler = handleRootGet, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kIconsUri = {
      .uri = "/api/icons.bin", .method = HTTP_GET, .handler = handleIconsGet, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kAuthUri = {
      .uri = "/api/auth", .method = HTTP_POST, .handler = handleAuthPost, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kLogoutUri = {
      .uri = "/api/logout", .method = HTTP_POST, .handler = handleLogoutPost, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kStateUri = {
      .uri = "/api/state", .method = HTTP_GET, .handler = handleStateGet, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kCmdUri = {
      .uri = "/api/cmd", .method = HTTP_POST, .handler = handleCmdPost, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kWsAuthUri = {
      .uri = "/ws/*", .method = HTTP_GET, .handler = handleWs, .user_ctx = nullptr,
      .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  static const httpd_uri_t kWsUnauthUri = {
      .uri = "/ws*", .method = HTTP_GET, .handler = handleWsUnauth, .user_ctx = nullptr,
      .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};

  const httpd_uri_t *uris[] = {&kRootUri, &kIconsUri, &kAuthUri, &kLogoutUri,
                               &kStateUri, &kCmdUri, &kWsAuthUri, &kWsUnauthUri};
  for (const httpd_uri_t *u : uris) {
    const esp_err_t err = httpd_register_uri_handler(s_server, u);
    if (err != ESP_OK) {
      Serial.printf("[HTTP] URI register failed for %s: %d\n", u->uri, static_cast<int>(err));
    }
  }

  Serial.println("[HTTP] Web UI + WebSocket server started on port 80");
}

void closeAllWsClients() {
  if (!s_server) return;
  int fds[MAX_HTTP_SOCKETS] = {};
  size_t numFds = MAX_HTTP_SOCKETS;
  if (httpd_get_client_list(s_server, &numFds, fds) != ESP_OK) return;
  for (size_t i = 0; i < numFds; ++i) {
    if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
      httpd_sess_trigger_close(s_server, fds[i]);
    }
  }
}

void webServerTick() {
  if (!s_server) return;
  const uint32_t now = millis();

  // Close WebSockets ONLY when Revoke increments auth epoch (NOT on PIN rotation)
  const uint32_t curEpoch = wifiManagerGetAuthEpoch();
  if (s_activeAuthEpoch != curEpoch) {
    s_activeAuthEpoch = curEpoch;
    closeAllWsClients();
  }

  if (s_forceClientRecount.exchange(false) || (now - s_lastClientCountPollMs >= 500)) {
    s_lastClientCountPollMs = now;
    const int wsCnt = countWsClients();
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      if (st.wsClientCount != wsCnt) {
        st.wsClientCount = static_cast<uint8_t>(wsCnt);
        mixerMarkDirty(DIRTY_HEADER | DIRTY_NETWORK);
      }
      mixerStateUnlock();
    }
  }

  int wsCnt = 0;
  if (mixerStateLock(10)) {
    wsCnt = mixerStateUnsafe().wsClientCount;
    mixerStateUnlock();
  }
  if (wsCnt == 0) {
    (void)mixerTakeWsDirtyFlags();
    return;
  }

  const uint32_t dirty = mixerTakeWsDirtyFlags();
  if (dirty == DIRTY_NONE) return;

  uint32_t unsent = dirty;
  const uint32_t nonMeterDirty = dirty & ~DIRTY_METERS;
  if (nonMeterDirty != 0) {
    if ((now - s_lastStatePushMs >= 35) && queueWsJson(true)) {
      s_lastStatePushMs = now;
      s_lastMeterPushMs = now;
      unsent = DIRTY_NONE;
    } else if ((dirty & DIRTY_METERS) != 0 && (now - s_lastMeterPushMs >= 85) &&
               queueWsJson(false)) {
      s_lastMeterPushMs = now;
      unsent &= ~DIRTY_METERS;
    }
  } else if ((dirty & DIRTY_METERS) != 0) {
    if ((now - s_lastMeterPushMs >= 85) && queueWsJson(false)) {
      s_lastMeterPushMs = now;
      unsent = DIRTY_NONE;
    }
  }

  if (unsent != DIRTY_NONE) {
    mixerRestoreWsDirtyFlags(unsent);
  }
}
