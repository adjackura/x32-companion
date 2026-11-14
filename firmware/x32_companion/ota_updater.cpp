#include "ota_updater.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <sys/time.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

#include "mixer_state.h"
#include "osc_codec.h"

extern "C" {
extern const uint8_t _binary_x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t _binary_x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
}

namespace {

static constexpr uint32_t MAX_OTA_BYTES = 0x3EA000u;  // rainmaker_8MB ota slot size
static constexpr int MAX_REDIRECTS = 4;
static constexpr size_t MAX_REDIRECT_URL_LEN = 2048;
static constexpr size_t MAX_MANIFEST_BYTES = 4096;
static constexpr time_t MIN_VALID_UTC_EPOCH = 1760000000;  // Oct 2025

struct PendingManifest {
  bool valid = false;
  char version[24] = {0};
  char url[240] = {0};
  uint32_t size = 0;
  char sha256Lower[65] = {0};
};

std::atomic<bool> s_otaBusy{false};
PendingManifest s_pending;

void setOtaState(uint8_t phase, uint8_t pct, const char *ver, const char *msg) {
  if (!mixerStateLock(50)) return;
  MixerState &st = mixerStateUnsafe();
  st.otaPhase = phase;
  st.otaProgressPct = pct;
  if (ver != nullptr) {
    std::snprintf(st.otaLatestVer, sizeof(st.otaLatestVer), "%s", ver);
  }
  if (msg != nullptr) {
    std::snprintf(st.otaStatusMsg, sizeof(st.otaStatusMsg), "%s", msg);
  }
  mixerMarkUiOnlyDirty(DIRTY_NETWORK);
  mixerStateUnlock();
}

void toLowerCopy(const char *src, char *dst, size_t cap) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < cap; ++i) {
    dst[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(src[i])));
  }
  dst[i] = '\0';
}

class ManifestSink : public Stream {
 public:
  char buf[MAX_MANIFEST_BYTES + 1] = {0};
  size_t len = 0;
  bool overflow = false;

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    if (overflow || !data) {
      setWriteError(1);
      return 0;
    }
    if (len + n > MAX_MANIFEST_BYTES) {
      overflow = true;
      setWriteError(1);
      return 0;
    }
    std::memcpy(buf + len, data, n);
    len += n;
    buf[len] = '\0';
    return n;
  }
};

class FirmwareSink : public Stream {
 public:
  mbedtls_sha256_context *sha = nullptr;
  uint32_t expectedSize = 0;
  uint32_t written = 0;
  const char *ver = "";
  uint8_t lastPct = 0;
  bool aborted = false;
  char failReason[48] = {0};

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    if (aborted || !data) {
      setWriteError(1);
      return 0;
    }
    if (n == 0) return 0;

    if (written == 0 && data[0] != 0xE9u) {
      aborted = true;
      std::snprintf(failReason, sizeof(failReason), "Invalid image header (not ESP32 bin)");
      setWriteError(1);
      return 0;
    }
    if (written + n > expectedSize) {
      aborted = true;
      std::snprintf(failReason, sizeof(failReason), "Download exceeded manifest size");
      setWriteError(1);
      return 0;
    }

    if (mbedtls_sha256_update(sha, data, n) != 0) {
      aborted = true;
      std::snprintf(failReason, sizeof(failReason), "SHA-256 hash error");
      setWriteError(1);
      return 0;
    }

    const size_t w = Update.write(const_cast<uint8_t *>(data), n);
    if (w != n) {
      aborted = true;
      std::snprintf(failReason, sizeof(failReason), "Flash write failed");
      setWriteError(1);
      return 0;
    }

    written += static_cast<uint32_t>(w);
    const uint8_t pct = expectedSize > 0
                            ? static_cast<uint8_t>((static_cast<uint64_t>(written) * 100u) / expectedSize)
                            : 0u;
    if (pct >= lastPct + 2u || written == expectedSize) {
      lastPct = pct;
      char msg[64];
      std::snprintf(msg, sizeof(msg), "Downloading v%s (%u%%)...", ver, static_cast<unsigned>(pct));
      setOtaState(OTA_DOWNLOADING, pct, ver, msg);
    }
    vTaskDelay(1);
    return w;
  }
};

bool syncClockViaHttpDate() {
  static constexpr const char *kProbeUrls[] = {
      "http://www.google.com/generate_204",
      "http://1.1.1.1/",
  };
  for (const char *probeUrl : kProbeUrls) {
    NetworkClient plainClient;
    HTTPClient http;
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(3500);
    if (!http.begin(plainClient, probeUrl)) {
      continue;
    }
    const char *hdrKeys[] = {"Date"};
    http.collectHeaders(hdrKeys, 1);
    const int code = http.sendRequest("HEAD");
    String dateHdr = http.header("Date");
    http.end();
    if (code > 0 && dateHdr.length() > 0) {
      const int64_t epoch = x32_parse_http_date_epoch(dateHdr.c_str());
      if (epoch >= static_cast<int64_t>(MIN_VALID_UTC_EPOCH)) {
        struct timeval tv = {};
        tv.tv_sec = static_cast<time_t>(epoch);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        Serial.printf("[OTA] Synced UTC clock via HTTP Date (%lld)\n",
                      static_cast<long long>(epoch));
        return true;
      }
    }
  }
  return false;
}

bool ensureClockSyncedForTls(uint8_t phase, const char *ver) {
  if (std::time(nullptr) >= MIN_VALID_UTC_EPOCH) {
    return true;
  }
  setOtaState(phase, 0, ver, "Syncing UTC clock...");
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  const uint32_t startMs = millis();
  while (static_cast<uint32_t>(millis() - startMs) < 4000u) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (std::time(nullptr) >= MIN_VALID_UTC_EPOCH) {
      Serial.printf("[OTA] Synced UTC clock via SNTP (%lld)\n",
                    static_cast<long long>(std::time(nullptr)));
      return true;
    }
  }
  return syncClockViaHttpDate();
}

bool httpsGetWithRedirects(const char *startUrl,
                           Stream *sink,
                           uint32_t expectedSize,
                           int *outHttpCode,
                           char *errBuf,
                           size_t errCap) {
  if (outHttpCode) *outHttpCode = 0;
  String currentUrl = startUrl ? startUrl : "";

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    if (!currentUrl.startsWith("https://") || currentUrl.length() > MAX_REDIRECT_URL_LEN) {
      std::snprintf(errBuf, errCap, "Non-HTTPS or oversized URL");
      return false;
    }

    NetworkClientSecure client;
    client.setCACertBundle(_binary_x509_crt_bundle_start,
                           static_cast<size_t>(_binary_x509_crt_bundle_end - _binary_x509_crt_bundle_start));
    client.setHandshakeTimeout(12);

    HTTPClient http;
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setUserAgent("x32-companion-ota/1.1");

    if (!http.begin(client, currentUrl)) {
      std::snprintf(errBuf, errCap, "HTTPS begin failed");
      return false;
    }

    const char *hdrKeys[] = {"Location"};
    http.collectHeaders(hdrKeys, 1);

    const int code = http.GET();
    if (outHttpCode) *outHttpCode = code;

    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String loc = http.header("Location");
      http.end();
      if (loc.length() == 0 || loc.length() > MAX_REDIRECT_URL_LEN || !loc.startsWith("https://")) {
        std::snprintf(errBuf, errCap, "Invalid HTTPS redirect");
        return false;
      }
      currentUrl = loc;
      continue;
    }

    if (code != 200) {
      if (code < 0) {
        char tlsErr[96] = {0};
        const int tlsCode = client.lastError(tlsErr, sizeof(tlsErr));
        Serial.printf("[OTA] HTTPS GET error code=%d tls=%d (%s)\n", code, tlsCode, tlsErr);
      }
      http.end();
      if (code == 404) {
        std::snprintf(errBuf, errCap, "No release published on GitHub yet");
      } else if (code < 0) {
        std::snprintf(errBuf, errCap, "HTTPS connect error (%d)", code);
      } else {
        std::snprintf(errBuf, errCap, "HTTP error %d", code);
      }
      return false;
    }

    const int contentLen = http.getSize();
    if (expectedSize > 0 && contentLen > 0 && static_cast<uint32_t>(contentLen) != expectedSize) {
      http.end();
      std::snprintf(errBuf, errCap, "Size mismatch vs manifest");
      return false;
    }

    const int rx = http.writeToStream(sink);
    http.end();
    if (rx < 0) {
      std::snprintf(errBuf, errCap, "Download interrupted (%d)", rx);
      return false;
    }
    return true;
  }

  std::snprintf(errBuf, errCap, "Too many HTTPS redirects");
  return false;
}

void otaCheckTask(void *param) {
  (void)param;
  if (WiFi.status() != WL_CONNECTED) {
    setOtaState(OTA_ERROR, 0, nullptr, "Connect Wi-Fi first");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  s_pending.valid = false;
  if (!ensureClockSyncedForTls(OTA_CHECKING, "")) {
    setOtaState(OTA_ERROR, 0, nullptr, "Clock sync failed (check internet)");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }
  setOtaState(OTA_CHECKING, 0, "", "Checking GitHub Releases...");

  ManifestSink *sink = static_cast<ManifestSink *>(
      heap_caps_malloc(sizeof(ManifestSink), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!sink) sink = static_cast<ManifestSink *>(malloc(sizeof(ManifestSink)));
  if (!sink) {
    setOtaState(OTA_ERROR, 0, nullptr, "Out of memory");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }
  new (sink) ManifestSink();

  int httpCode = 0;
  char err[64] = {0};
  const bool ok = httpsGetWithRedirects(X32_COMPANION_MANIFEST_URL, sink, 0, &httpCode, err, sizeof(err));
  if (!ok) {
    setOtaState(OTA_ERROR, 0, nullptr, err[0] ? err : "Manifest fetch failed");
    sink->~ManifestSink();
    free(sink);
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }
  if (sink->overflow || sink->len == 0) {
    setOtaState(OTA_ERROR, 0, nullptr, "Manifest empty or too large");
    sink->~ManifestSink();
    free(sink);
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  JsonDocument doc;
  const DeserializationError jErr = deserializeJson(doc, sink->buf, sink->len);
  sink->~ManifestSink();
  free(sink);

  if (jErr != DeserializationError::Ok) {
    setOtaState(OTA_ERROR, 0, nullptr, "Invalid manifest JSON");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  const char *ver = doc["version"] | "";
  const char *url = doc["url"] | "";
  const uint32_t size = doc["size"] | 0u;
  const char *sha = doc["sha256"] | "";

  if (!x32_ota_validate_manifest(ver, url, size, sha, X32_COMPANION_GITHUB_REPO, MAX_OTA_BYTES)) {
    setOtaState(OTA_ERROR, 0, nullptr, "Invalid release manifest");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  const char *cleanSrc = (ver[0] == 'v' || ver[0] == 'V') ? (ver + 1) : ver;
  char cleanVer[24];
  std::snprintf(cleanVer, sizeof(cleanVer), "%s", cleanSrc);

  const int cmp = x32_ota_compare_semver(cleanVer, X32_COMPANION_FW_VERSION);
  if (cmp > 0) {
    s_pending.valid = true;
    std::snprintf(s_pending.version, sizeof(s_pending.version), "%s", cleanVer);
    std::snprintf(s_pending.url, sizeof(s_pending.url), "%s", url);
    s_pending.size = size;
    toLowerCopy(sha, s_pending.sha256Lower, sizeof(s_pending.sha256Lower));

    char msg[64];
    std::snprintf(msg, sizeof(msg), "v%s available - tap Install", cleanVer);
    setOtaState(OTA_AVAILABLE, 0, cleanVer, msg);
  } else {
    s_pending.valid = false;
    char msg[64];
    std::snprintf(msg, sizeof(msg), "Up to date (v%s)", X32_COMPANION_FW_VERSION);
    setOtaState(OTA_UP_TO_DATE, 0, cleanVer, msg);
  }

  s_otaBusy.store(false);
  vTaskDelete(nullptr);
}

void otaInstallTask(void *param) {
  (void)param;
  if (WiFi.status() != WL_CONNECTED) {
    setOtaState(OTA_ERROR, 0, nullptr, "Connect Wi-Fi first");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }
  if (!s_pending.valid) {
    setOtaState(OTA_ERROR, 0, nullptr, "Run Check Update first");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }
  if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
    setOtaState(OTA_ERROR, 0, nullptr, "No OTA partition (USB flash once)");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  if (!ensureClockSyncedForTls(OTA_DOWNLOADING, s_pending.version)) {
    setOtaState(OTA_ERROR, 0, nullptr, "Clock sync failed (check internet)");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  const PendingManifest manifest = s_pending;
  setOtaState(OTA_DOWNLOADING, 0, manifest.version, "Connecting to GitHub CDN...");

  if (!Update.begin(manifest.size, U_FLASH)) {
    setOtaState(OTA_ERROR, 0, nullptr, "Not enough OTA flash space");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  FirmwareSink sink;
  sink.sha = &sha;
  sink.expectedSize = manifest.size;
  sink.ver = manifest.version;

  int httpCode = 0;
  char err[64] = {0};
  const bool ok = httpsGetWithRedirects(manifest.url, &sink, manifest.size, &httpCode, err, sizeof(err));
  if (!ok || sink.aborted || sink.written != manifest.size) {
    Update.abort();
    mbedtls_sha256_free(&sha);
    const char *msg = sink.failReason[0] ? sink.failReason : (err[0] ? err : "Download failed - unchanged");
    setOtaState(OTA_ERROR, 0, nullptr, msg);
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  setOtaState(OTA_VERIFYING, 100, manifest.version, "Verifying SHA-256...");
  uint8_t digest[32] = {0};
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  static constexpr char kHex[] = "0123456789abcdef";
  char actualHex[65];
  for (size_t i = 0; i < 32; ++i) {
    actualHex[i * 2] = kHex[(digest[i] >> 4) & 0x0Fu];
    actualHex[i * 2 + 1] = kHex[digest[i] & 0x0Fu];
  }
  actualHex[64] = '\0';

  if (std::strcmp(actualHex, manifest.sha256Lower) != 0) {
    Update.abort();
    setOtaState(OTA_ERROR, 0, nullptr, "SHA-256 mismatch - aborted safely");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  if (!Update.end(false)) {
    Update.abort();
    setOtaState(OTA_ERROR, 0, nullptr, "Flash finalize failed - unchanged");
    s_otaBusy.store(false);
    vTaskDelete(nullptr);
    return;
  }

  s_pending.valid = false;
  char rebootMsg[64];
  std::snprintf(rebootMsg, sizeof(rebootMsg), "Verified! Rebooting into v%s...", manifest.version);
  setOtaState(OTA_REBOOTING, 100, manifest.version, rebootMsg);
  Serial.printf("[OTA] Verified v%s (%u bytes). Rebooting...\n",
                manifest.version, static_cast<unsigned>(manifest.size));
  vTaskDelay(pdMS_TO_TICKS(1200));
  ESP.restart();
}

}  // namespace

void otaUpdaterMarkBootValid() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running) return;
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      Serial.printf("[OTA] Boot verified on partition %s (v%s)\n",
                    running->label, X32_COMPANION_FW_VERSION);
    } else {
      Serial.println("[OTA] Warning: failed to cancel OTA rollback state");
    }
  }
}

void otaUpdaterRequestCheck() {
  if (s_otaBusy.exchange(true)) return;
  if (xTaskCreatePinnedToCore(otaCheckTask, "ota_check", 16384, nullptr, 2, nullptr, 0) != pdPASS) {
    s_otaBusy.store(false);
    setOtaState(OTA_ERROR, 0, nullptr, "Failed to start OTA check");
  }
}

void otaUpdaterRequestInstall() {
  if (s_otaBusy.exchange(true)) return;
  if (xTaskCreatePinnedToCore(otaInstallTask, "ota_install", 16384, nullptr, 2, nullptr, 0) != pdPASS) {
    s_otaBusy.store(false);
    setOtaState(OTA_ERROR, 0, nullptr, "Failed to start OTA install");
  }
}
