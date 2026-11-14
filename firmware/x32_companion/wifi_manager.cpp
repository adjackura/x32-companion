#include "wifi_manager.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/md.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "mixer_state.h"
#include "x32_client.h"

namespace {

static constexpr const char *NVS_NAMESPACE = "x32comp";
static constexpr const char *FALLBACK_AP_SSID = "X32-Companion";
static constexpr uint32_t STA_CONNECT_FALLBACK_MS = 10000;
static constexpr uint32_t PIN_ROTATE_INTERVAL_MS = 15u * 60u * 1000u;  // 15 minutes
static constexpr size_t AUTH_SECRET_LEN = 32;
static constexpr size_t SESSION_TOKEN_LEN = 52;  // "s1." + 16 hex + "." + 32 hex

Preferences s_prefs;
bool s_prefsReady = false;
bool s_mdnsStarted = false;
bool s_scanInProgress = false;
uint32_t s_scanStartedMs = 0;
uint32_t s_holdStaRetryUntilMs = 0;
bool s_autoScanAfterBootTimeoutDone = false;
bool s_staWanted = false;
uint32_t s_connectStartMs = 0;
uint32_t s_lastStatusPollMs = 0;
uint32_t s_lastStaRetryMs = 0;
bool s_wasConnected = false;
bool s_clockLogged = false;
char s_savedSsid[33] = {0};
char s_savedPass[65] = {0};
char s_savedX32Ip[20] = {0};
uint16_t s_savedScreenTimeoutSec = DEFAULT_SCREEN_TIMEOUT_SEC;

SemaphoreHandle_t s_authMutex = nullptr;
uint8_t s_authSecret[AUTH_SECRET_LEN] = {0};
char s_currentPin[8] = {0};
uint32_t s_pinIssuedMs = 0;
std::atomic<uint32_t> s_authEpoch{1};

uint16_t sanitizeScreenTimeoutSec(uint32_t v) {
  static constexpr uint16_t kAllowed[] = {0, 15, 30, 60, 120, 300, 600};
  for (uint16_t a : kAllowed) {
    if (v == a) return a;
  }
  return DEFAULT_SCREEN_TIMEOUT_SEC;
}

bool isSixDigitPasskey(const char *s) {
  if (!s || std::strlen(s) != 6) return false;
  for (int i = 0; i < 6; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

bool isLowerHex(const char *s, size_t len) {
  if (!s) return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool isValidTokenFormat(const char *tok) {
  if (!tok || std::strlen(tok) != SESSION_TOKEN_LEN) return false;
  if (tok[0] != 's' || tok[1] != '1' || tok[2] != '.' || tok[19] != '.') return false;
  return isLowerHex(tok + 3, 16) && isLowerHex(tok + 20, 32);
}

void bytesToLowerHex(const uint8_t *in, size_t n, char *out) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[i * 2] = kHex[(in[i] >> 4) & 0x0Fu];
    out[i * 2 + 1] = kHex[in[i] & 0x0Fu];
  }
  out[n * 2] = '\0';
}

bool computeHmac128Hex(const uint8_t secret[AUTH_SECRET_LEN],
                       const char *msg,
                       size_t msgLen,
                       char outHex33[33]) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  uint8_t digest[32] = {0};
  const int rc = mbedtls_md_hmac(info,
                                 secret,
                                 AUTH_SECRET_LEN,
                                 reinterpret_cast<const unsigned char *>(msg),
                                 msgLen,
                                 digest);
  if (rc != 0) return false;
  bytesToLowerHex(digest, 16, outHex33);
  volatile uint8_t *vp = digest;
  for (size_t i = 0; i < sizeof(digest); ++i) vp[i] = 0;
  return true;
}

bool constantTimeEq(const char *a, const char *b, size_t len) {
  if (!a || !b) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) {
    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return diff == 0;
}

void fillRandomBytes(uint8_t *out, size_t len) {
  size_t off = 0;
  while (off < len) {
    const uint32_t r = esp_random();
    const size_t n = std::min(len - off, sizeof(r));
    std::memcpy(out + off, &r, n);
    off += n;
  }
}

bool takeAuthMutex(uint32_t timeoutMs = 50) {
  return !s_authMutex || xSemaphoreTake(s_authMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void giveAuthMutex() {
  if (s_authMutex) xSemaphoreGive(s_authMutex);
}

void generateRandomPasskey(char *out, size_t cap, const char *avoid) {
  if (!out || cap < 7) return;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const uint32_t num = 100000u + (esp_random() % 900000u);
    std::snprintf(out, cap, "%06lu", static_cast<unsigned long>(num));
    if (!avoid || std::strcmp(out, avoid) != 0) break;
  }
}

void ensurePrefs() {
  if (!s_prefsReady) {
    s_prefs.begin(NVS_NAMESPACE, false);
    s_prefsReady = true;
  }
}

void publishPinToState(const char *pin, const char *statusMsg) {
  if (!mixerStateLock(50)) return;
  MixerState &st = mixerStateUnsafe();
  std::snprintf(st.webPasskey, sizeof(st.webPasskey), "%s", pin);
  if (statusMsg && statusMsg[0] != '\0') {
    std::snprintf(st.statusMsg, sizeof(st.statusMsg), "%s", statusMsg);
  }
  mixerMarkUiOnlyDirty(DIRTY_NETWORK | DIRTY_OVERVIEW);
  mixerStateUnlock();
}

void rotatePinAndPublish(bool announceStatus) {
  char nextPin[8] = {0};
  if (takeAuthMutex(50)) {
    char prev[8] = {0};
    std::snprintf(prev, sizeof(prev), "%s", s_currentPin);
    generateRandomPasskey(nextPin, sizeof(nextPin), prev[0] ? prev : nullptr);
    std::snprintf(s_currentPin, sizeof(s_currentPin), "%s", nextPin);
    s_pinIssuedMs = millis();
    giveAuthMutex();
  }
  if (nextPin[0] != '\0') {
    char msg[64] = {0};
    if (announceStatus) {
      std::snprintf(msg, sizeof(msg), "New pairing PIN: %s", nextPin);
    }
    publishPinToState(nextPin, announceStatus ? msg : nullptr);
    Serial.printf("[AUTH] Web UI Pairing PIN: %s\n", nextPin);
  }
}

void enableFallbackAp() {
  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
  }
  if (!WiFi.softAPIP() || WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    WiFi.softAP(FALLBACK_AP_SSID);  // Open setup AP for fast venue onboarding
  }
  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    st.apActive = true;
    IPAddress ap = WiFi.softAPIP();
    std::snprintf(st.apIp, sizeof(st.apIp), "%s", ap.toString().c_str());
    mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
    mixerStateUnlock();
  }
  Serial.printf("[WIFI] Fallback AP '%s' active at %s\n",
                FALLBACK_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void disableFallbackAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    st.apActive = false;
    mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
    mixerStateUnlock();
  }
}

void finishWifiScan(int n) {
  s_scanInProgress = false;
  s_holdStaRetryUntilMs = millis() + 30000u;
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
  }
  if (n < 0) {
    WiFi.scanDelete();
    if (mixerStateLock(50)) {
      MixerState &st = mixerStateUnsafe();
      st.wifiScanning = false;
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "Scan interrupted, tap Scan Wi-Fi");
      mixerMarkDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    Serial.printf("[WIFI] Async scan aborted (%d)\n", n);
    return;
  }

  // Deduplicate by SSID keeping strongest RSSI
  WifiScanEntry entries[MAX_WIFI_SCAN];
  int count = 0;
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    int rssi = WiFi.RSSI(i);
    bool sec = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

    int existing = -1;
    for (int j = 0; j < count; ++j) {
      if (ssid.equals(entries[j].ssid)) {
        existing = j;
        break;
      }
    }
    if (existing >= 0) {
      if (rssi > entries[existing].rssi) {
        entries[existing].rssi = static_cast<int8_t>(rssi);
        entries[existing].secure = sec;
      }
    } else if (count < MAX_WIFI_SCAN) {
      std::snprintf(entries[count].ssid, sizeof(entries[count].ssid), "%s", ssid.c_str());
      entries[count].rssi = static_cast<int8_t>(rssi);
      entries[count].secure = sec;
      count++;
    }
  }

  std::sort(entries, entries + count, [](const WifiScanEntry &a, const WifiScanEntry &b) {
    return a.rssi > b.rssi;
  });

  WiFi.scanDelete();

  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    st.wifiScanning = false;
    st.wifiScanDone = true;
    st.wifiScanCount = static_cast<uint8_t>(count);
    for (int i = 0; i < count; ++i) {
      st.wifiScan[i] = entries[i];
    }
    if (count > 0) {
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg),
                    "Found %d networks (tap to connect)", count);
    } else {
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "No 2.4 GHz networks found");
    }
    if (!st.x32Connected) {
      std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Found %d Wi-Fi networks", count);
    }
    mixerMarkDirty(DIRTY_NETWORK);
    mixerStateUnlock();
  }
}

}  // namespace

void wifiManagerInit() {
  if (!s_authMutex) {
    s_authMutex = xSemaphoreCreateMutex();
  }
  ensurePrefs();
  String savedSsid = s_prefs.getString("wifi_ssid", "");
  String savedPass = s_prefs.getString("wifi_pass", "");
  String savedX32 = s_prefs.getString("x32_ip", "");
  s_savedScreenTimeoutSec = sanitizeScreenTimeoutSec(
      s_prefs.getUShort("scr_off_s", DEFAULT_SCREEN_TIMEOUT_SEC));
  std::snprintf(s_savedSsid, sizeof(s_savedSsid), "%s", savedSsid.c_str());
  std::snprintf(s_savedPass, sizeof(s_savedPass), "%s", savedPass.c_str());
  std::snprintf(s_savedX32Ip, sizeof(s_savedX32Ip), "%s", savedX32.c_str());

  // Persistent 256-bit HMAC secret (survives reboots so paired devices keep working until Revoke)
  if (takeAuthMutex(100)) {
    const size_t secLen = s_prefs.getBytesLength("auth_sec");
    bool loadedSec = (secLen == AUTH_SECRET_LEN &&
                      s_prefs.getBytes("auth_sec", s_authSecret, AUTH_SECRET_LEN) == AUTH_SECRET_LEN);
    uint8_t zeroCheck = 0;
    for (size_t i = 0; i < AUTH_SECRET_LEN; ++i) zeroCheck |= s_authSecret[i];
    if (!loadedSec || zeroCheck == 0) {
      fillRandomBytes(s_authSecret, AUTH_SECRET_LEN);
      s_authSecret[0] |= 0x01u;
      s_prefs.putBytes("auth_sec", s_authSecret, AUTH_SECRET_LEN);
    }
    uint32_t ep = s_prefs.getULong("auth_ep", 1u);
    if (ep == 0) ep = 1u;
    s_authEpoch.store(ep, std::memory_order_relaxed);
    generateRandomPasskey(s_currentPin, sizeof(s_currentPin), nullptr);
    s_pinIssuedMs = millis();
    giveAuthMutex();
  }
  Serial.printf("[AUTH] Web UI Pairing PIN: %s (rotates on use or every 15m)\n", s_currentPin);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    std::snprintf(st.savedWifiSsid, sizeof(st.savedWifiSsid), "%s", s_savedSsid);
    std::snprintf(st.x32Ip, sizeof(st.x32Ip), "%s", s_savedX32Ip);
    st.screenTimeoutSec = s_savedScreenTimeoutSec;
    std::snprintf(st.webPasskey, sizeof(st.webPasskey), "%s", s_currentPin);
    mixerStateUnlock();
  }

  if (s_savedSsid[0] != '\0') {
    s_staWanted = true;
    s_connectStartMs = millis();
    s_lastStaRetryMs = s_connectStartMs;
    s_autoScanAfterBootTimeoutDone = false;
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.begin(s_savedSsid, s_savedPass);
    enableFallbackAp();
    if (mixerStateLock(50)) {
      MixerState &st = mixerStateUnsafe();
      st.wifiConnecting = true;
      std::snprintf(st.wifiSsid, sizeof(st.wifiSsid), "%s", s_savedSsid);
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "Connecting to %s...", s_savedSsid);
      std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Connecting to %s...", s_savedSsid);
      mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
      mixerStateUnlock();
    }
  } else {
    s_staWanted = false;
    s_autoScanAfterBootTimeoutDone = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    enableFallbackAp();
    wifiManagerStartScan();
  }
}

void wifiManagerStartScan() {
  const uint32_t now = millis();
  s_holdStaRetryUntilMs = now + 30000u;

  if (s_scanInProgress) {
    if (static_cast<uint32_t>(now - s_scanStartedMs) < 12000u) {
      return;
    }
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    s_scanInProgress = false;
  }

  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
  }

  // ESP-IDF rejects esp_wifi_scan_start() with ESP_ERR_WIFI_STATE if STA is mid-connect.
  // When not already associated, halt auto-reconnect and abort any active connect attempt first.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(60));
  }

  int res = WiFi.scanNetworks(true, false, false, 220);
  if (res == WIFI_SCAN_FAILED) {
    WiFi.setAutoReconnect(false);
    esp_wifi_scan_stop();
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    res = WiFi.scanNetworks(true, false, false, 220);
  }

  if (res == WIFI_SCAN_RUNNING) {
    s_scanInProgress = true;
    s_scanStartedMs = millis();
    if (mixerStateLock(50)) {
      MixerState &st = mixerStateUnsafe();
      st.wifiScanning = true;
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "Scanning 2.4 GHz Wi-Fi...");
      mixerMarkDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    Serial.println("[WIFI] Started async 2.4 GHz scan");
  } else if (res >= 0) {
    finishWifiScan(res);
  } else {
    Serial.printf("[WIFI] scanNetworks failed (%d)\n", res);
    if (mixerStateLock(50)) {
      MixerState &st = mixerStateUnsafe();
      st.wifiScanning = false;
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "Wi-Fi radio busy, tap Scan again");
      mixerMarkDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
  }
}

void wifiManagerConnect(const char *ssid, const char *password) {
  if (!ssid || ssid[0] == '\0') return;
  const char *safePass = password ? password : "";
  ensurePrefs();
  if (std::strcmp(s_savedSsid, ssid) != 0) {
    std::snprintf(s_savedSsid, sizeof(s_savedSsid), "%s", ssid);
    s_prefs.putString("wifi_ssid", s_savedSsid);
  }
  if (std::strcmp(s_savedPass, safePass) != 0) {
    std::snprintf(s_savedPass, sizeof(s_savedPass), "%s", safePass);
    s_prefs.putString("wifi_pass", s_savedPass);
  }

  if (s_scanInProgress) {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    s_scanInProgress = false;
  }
  s_holdStaRetryUntilMs = 0;
  s_autoScanAfterBootTimeoutDone = false;
  s_staWanted = true;
  s_wasConnected = false;
  s_connectStartMs = millis();
  s_lastStaRetryMs = s_connectStartMs;

  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
  }
  WiFi.setAutoReconnect(false);
  esp_wifi_disconnect();
  WiFi.begin(ssid, safePass);

  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    st.wifiScanning = false;
    st.wifiConnected = false;
    st.wifiConnecting = true;
    std::snprintf(st.savedWifiSsid, sizeof(st.savedWifiSsid), "%s", ssid);
    std::snprintf(st.wifiSsid, sizeof(st.wifiSsid), "%s", ssid);
    std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "Connecting to %s...", ssid);
    std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Connecting to %s...", ssid);
    mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
    mixerStateUnlock();
  }
}

void wifiManagerSaveX32Ip(const char *ip) {
  if (!ip || std::strcmp(s_savedX32Ip, ip) == 0) return;
  ensurePrefs();
  std::snprintf(s_savedX32Ip, sizeof(s_savedX32Ip), "%s", ip);
  s_prefs.putString("x32_ip", s_savedX32Ip);
}

void wifiManagerLoadX32Ip(char *outIp, size_t cap) {
  if (!outIp || cap == 0) return;
  std::snprintf(outIp, cap, "%s", s_savedX32Ip);
}

void wifiManagerSaveScreenTimeoutSec(uint16_t sec) {
  const uint16_t clean = sanitizeScreenTimeoutSec(sec);
  ensurePrefs();
  if (s_savedScreenTimeoutSec != clean) {
    s_savedScreenTimeoutSec = clean;
    s_prefs.putUShort("scr_off_s", clean);
  }
}

uint16_t wifiManagerLoadScreenTimeoutSec() {
  return s_savedScreenTimeoutSec;
}

void wifiManagerRegenPasskey() {
  rotatePinAndPublish(true);
}

bool wifiManagerExchangePinForToken(const char *pin, char *outToken, size_t tokenCap) {
  if (!isSixDigitPasskey(pin) || !outToken || tokenCap < SESSION_TOKEN_LEN + 1u) {
    return false;
  }
  outToken[0] = '\0';

  bool matched = false;
  char nextPin[8] = {0};
  uint8_t secretCopy[AUTH_SECRET_LEN] = {0};

  if (takeAuthMutex(50)) {
    if (isSixDigitPasskey(s_currentPin) && constantTimeEq(pin, s_currentPin, 6)) {
      matched = true;
      std::memcpy(secretCopy, s_authSecret, AUTH_SECRET_LEN);
      // Single-use pairing PIN: immediately rotate to a fresh PIN on match
      char prev[8] = {0};
      std::snprintf(prev, sizeof(prev), "%s", s_currentPin);
      generateRandomPasskey(nextPin, sizeof(nextPin), prev);
      std::snprintf(s_currentPin, sizeof(s_currentPin), "%s", nextPin);
      s_pinIssuedMs = millis();
    }
    giveAuthMutex();
  }

  if (!matched) {
    return false;
  }

  uint8_t nonce[8] = {0};
  fillRandomBytes(nonce, sizeof(nonce));
  char nonceHex[17] = {0};
  bytesToLowerHex(nonce, sizeof(nonce), nonceHex);

  char prefix[24] = {0};
  const int pn = std::snprintf(prefix, sizeof(prefix), "s1.%s", nonceHex);
  char macHex[33] = {0};
  const bool hmacOk = (pn == 19) && computeHmac128Hex(secretCopy, prefix, 19, macHex);

  volatile uint8_t *sp = secretCopy;
  for (size_t i = 0; i < AUTH_SECRET_LEN; ++i) sp[i] = 0;

  if (!hmacOk) {
    return false;
  }

  std::snprintf(outToken, tokenCap, "%s.%s", prefix, macHex);
  publishPinToState(nextPin, nullptr);
  Serial.printf("[AUTH] Pairing PIN used -> rotated to %s\n", nextPin);
  return true;
}

bool wifiManagerVerifySessionToken(const char *token) {
  if (!isValidTokenFormat(token)) return false;

  uint8_t secretCopy[AUTH_SECRET_LEN] = {0};
  if (!takeAuthMutex(20)) return false;
  std::memcpy(secretCopy, s_authSecret, AUTH_SECRET_LEN);
  giveAuthMutex();

  char expectedMacHex[33] = {0};
  const bool hmacOk = computeHmac128Hex(secretCopy, token, 19, expectedMacHex);
  volatile uint8_t *sp = secretCopy;
  for (size_t i = 0; i < AUTH_SECRET_LEN; ++i) sp[i] = 0;

  return hmacOk && constantTimeEq(token + 20, expectedMacHex, 32);
}

void wifiManagerRevokeAllSessions() {
  ensurePrefs();
  char nextPin[8] = {0};
  if (takeAuthMutex(100)) {
    fillRandomBytes(s_authSecret, AUTH_SECRET_LEN);
    s_authSecret[0] |= 0x01u;
    s_prefs.putBytes("auth_sec", s_authSecret, AUTH_SECRET_LEN);

    uint32_t nextEpoch = s_authEpoch.load(std::memory_order_relaxed) + 1u;
    if (nextEpoch == 0) nextEpoch = 1u;
    s_authEpoch.store(nextEpoch, std::memory_order_relaxed);
    s_prefs.putULong("auth_ep", nextEpoch);

    char prev[8] = {0};
    std::snprintf(prev, sizeof(prev), "%s", s_currentPin);
    generateRandomPasskey(nextPin, sizeof(nextPin), prev[0] ? prev : nullptr);
    std::snprintf(s_currentPin, sizeof(s_currentPin), "%s", nextPin);
    s_pinIssuedMs = millis();
    giveAuthMutex();
  }
  if (nextPin[0] != '\0') {
    publishPinToState(nextPin, "All web sessions revoked");
    Serial.printf("[AUTH] Revoked all web sessions; new PIN: %s\n", nextPin);
  }
}

uint32_t wifiManagerGetAuthEpoch() {
  return s_authEpoch.load(std::memory_order_relaxed);
}

void wifiManagerTick() {
  uint32_t now = millis();

  if (s_scanInProgress) {
    const int n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) {
      finishWifiScan(n);
    }
  }

  const bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !s_wasConnected) {
    s_wasConnected = true;
    s_autoScanAfterBootTimeoutDone = true;
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    // Disable SoftAP once STA gets an IP so UDP broadcasts go strictly to the X32 subnet
    disableFallbackAp();
    x32ClientOnWifiConnected();

    if (!s_mdnsStarted) {
      if (MDNS.begin("x32-companion")) {
        MDNS.addService("http", "tcp", 80);
        s_mdnsStarted = true;
      }
    }

    if (mixerStateLock(50)) {
      MixerState &st = mixerStateUnsafe();
      st.wifiConnected = true;
      st.wifiConnecting = false;
      st.wifiRssi = static_cast<int8_t>(WiFi.RSSI());
      st.wifiStatusMsg[0] = '\0';
      std::snprintf(st.wifiSsid, sizeof(st.wifiSsid), "%s", WiFi.SSID().c_str());
      std::snprintf(st.staIp, sizeof(st.staIp), "%s", WiFi.localIP().toString().c_str());
      std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Wi-Fi online: %s", st.staIp);
      mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
      mixerStateUnlock();
    }
    Serial.printf("[WIFI] Connected! IP=%s RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // Connect to saved X32 IP directly when present; only broadcast discovery when unconfigured
    char savedIp[20] = {0};
    wifiManagerLoadX32Ip(savedIp, sizeof(savedIp));
    if (savedIp[0] != '\0') {
      x32ClientConnect(savedIp);
    } else {
      x32ClientDiscover();
    }
  } else if (!connected && s_wasConnected) {
    s_wasConnected = false;
    s_connectStartMs = now;
    s_lastStaRetryMs = now;
    WiFi.setAutoReconnect(false);
    if (mixerStateLock(50)) {
      MixerState &st = mixerStateUnsafe();
      st.wifiConnected = false;
      st.wifiConnecting = s_staWanted;
      st.staIp[0] = '\0';
      st.x32Connected = false;
      st.x32Connecting = false;
      st.syncProgress = 0;
      mixerClearMetersLocked(st);
      std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg), "Wi-Fi lost, reconnecting...");
      std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Wi-Fi lost, reconnecting...");
      mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER | DIRTY_METERS);
      mixerStateUnlock();
    }
    x32ClientOnWifiLost();
  }

  if (connected && !s_clockLogged && std::time(nullptr) >= 1760000000) {
    s_clockLogged = true;
    Serial.printf("[TIME] UTC clock synced (%lld)\n",
                  static_cast<long long>(std::time(nullptr)));
  }

  // Re-sample millis() after potentially slow connect actions before timeout/RSSI comparisons
  now = millis();

  // Periodically nudge STA reconnect during extended router/rack power outages
  if (!connected && s_staWanted && s_savedSsid[0] != '\0' && !s_scanInProgress &&
      WiFi.softAPgetStationNum() == 0 &&
      static_cast<int32_t>(now - s_holdStaRetryUntilMs) >= 0 &&
      static_cast<int32_t>(now - s_lastStaRetryMs) >= 20000) {
    s_lastStaRetryMs = now;
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    WiFi.begin(s_savedSsid, s_savedPass);
  }

  // If STA isn't up after timeout, ensure Fallback AP is running
  if (!connected && s_staWanted &&
      static_cast<int32_t>(now - s_connectStartMs) > static_cast<int32_t>(STA_CONNECT_FALLBACK_MS)) {
    bool needAp = false;
    bool timedOutTransition = false;
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      needAp = !st.apActive;
      if (st.wifiConnecting) {
        timedOutTransition = true;
        st.wifiConnecting = false;
        std::snprintf(st.wifiStatusMsg, sizeof(st.wifiStatusMsg),
                      "Saved Wi-Fi not found; AP active");
        std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Wi-Fi timeout; AP X32-Companion active");
        mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
      }
      mixerStateUnlock();
    }
    if (timedOutTransition) {
      esp_wifi_disconnect();
    }
    if (needAp) {
      enableFallbackAp();
    }
    if (!s_autoScanAfterBootTimeoutDone && !s_scanInProgress) {
      s_autoScanAfterBootTimeoutDone = true;
      wifiManagerStartScan();
    }
  }

  // Periodic 15-minute Pairing PIN rotation (existing HMAC sessions stay valid)
  uint32_t issuedMs = now;
  if (takeAuthMutex(10)) {
    issuedMs = s_pinIssuedMs;
    giveAuthMutex();
  }
  if (static_cast<uint32_t>(now - issuedMs) >= PIN_ROTATE_INTERVAL_MS) {
    rotatePinAndPublish(false);
  }

  // Periodic RSSI refresh
  if (connected && static_cast<int32_t>(now - s_lastStatusPollMs) >= 2000) {
    s_lastStatusPollMs = now;
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      int8_t r = static_cast<int8_t>(WiFi.RSSI());
      if (st.wifiRssi == 0 || std::abs(static_cast<int>(st.wifiRssi) - static_cast<int>(r)) >= 2) {
        st.wifiRssi = r;
        mixerMarkDirty(DIRTY_HEADER | DIRTY_NETWORK);
      }
      mixerStateUnlock();
    }
  }
}
