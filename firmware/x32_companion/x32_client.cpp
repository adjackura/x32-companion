#include "x32_client.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "board_driver.h"
#include "osc_codec.h"
#include "ota_updater.h"
#include "wifi_manager.h"

namespace {

static constexpr uint16_t X32_OSC_PORT = 10023;
static constexpr uint16_t X32_LOCAL_UDP_PORT = 10025;
static constexpr uint32_t XREMOTE_INTERVAL_MS = 7000;
static constexpr uint32_t METERS_INTERVAL_MS = 4500;
static constexpr uint32_t SILENCE_PROBE_MS = 4000;
static constexpr uint32_t SILENCE_PROBE_STEP_MS = 2000;
static constexpr uint32_t CONSOLE_TIMEOUT_MS = 10000;
static constexpr uint32_t CONNECTING_BADGE_MS = 4000;
static constexpr uint32_t OFFLINE_PROBE_INTERVAL_MS = 3000;
static constexpr uint32_t OFFLINE_DISCOVER_INTERVAL_MS = 15000;
static constexpr uint32_t SYNC_STEP_INTERVAL_MS = 8;
static constexpr int SYNC_BATCH_SIZE = 4;
static constexpr int SYNC_TOTAL_PASSES = 2;

WiFiUDP s_udp;
bool s_udpBound = false;
IPAddress s_targetIp(0, 0, 0, 0);
bool s_hasTarget = false;

uint32_t s_lastXremoteMs = 0;
uint32_t s_lastMetersMs = 0;
uint32_t s_lastSyncTickMs = 0;
uint32_t s_lastDiscoverMs = 0;
uint32_t s_connectAttemptStartMs = 0;
uint32_t s_lastProbeMs = 0;
uint8_t s_fastRetriesLeft = 0;

uint32_t s_lastScreenCheckMs = 0;
uint32_t s_lastScreenActivityMs = 0;
uint32_t s_lastSeenActivitySeq = 0;

bool s_fullSyncActive = false;
int s_syncStep = 0;
int s_syncPass = 0;

bool s_busSendSyncActive = false;
int s_busSendSyncTarget = -1;
int s_busSendSyncCh = 0;

uint8_t s_rxBuf[2048];
uint8_t s_txBuf[256];

bool elapsedMs(uint32_t now, uint32_t since, uint32_t intervalMs) {
  return static_cast<int32_t>(now - since) >= static_cast<int32_t>(intervalMs);
}

void sendOscBytes(const IPAddress &ip, const uint8_t *buf, size_t len) {
  if (!s_udpBound || len == 0 || ip == IPAddress(0, 0, 0, 0)) return;
  s_udp.beginPacket(ip, X32_OSC_PORT);
  s_udp.write(buf, len);
  s_udp.endPacket();
}

void sendQueryToTarget(const char *addr) {
  if (!s_hasTarget) return;
  size_t n = osc_build_query(s_txBuf, sizeof(s_txBuf), addr);
  sendOscBytes(s_targetIp, s_txBuf, n);
}

void sendIntToTarget(const char *addr, int32_t val) {
  if (!s_hasTarget) return;
  size_t n = osc_build_int(s_txBuf, sizeof(s_txBuf), addr, val);
  sendOscBytes(s_targetIp, s_txBuf, n);
}

void sendFloatToTarget(const char *addr, float val) {
  if (!s_hasTarget) return;
  size_t n = osc_build_float(s_txBuf, sizeof(s_txBuf), addr, val);
  sendOscBytes(s_targetIp, s_txBuf, n);
}

void sendStrToTarget(const char *addr, const char *str) {
  if (!s_hasTarget) return;
  size_t n = osc_build_str(s_txBuf, sizeof(s_txBuf), addr, str);
  sendOscBytes(s_targetIp, s_txBuf, n);
}

void formatStripPrefix(StripKind kind, int idx, char *buf, size_t cap) {
  switch (kind) {
    case STRIP_CH: std::snprintf(buf, cap, "/ch/%02d", idx + 1); break;
    case STRIP_AUX: std::snprintf(buf, cap, "/auxin/%02d", idx + 1); break;
    case STRIP_BUS: std::snprintf(buf, cap, "/bus/%02d", idx + 1); break;
    case STRIP_DCA: std::snprintf(buf, cap, "/dca/%d", idx + 1); break;
    case STRIP_MAIN: std::snprintf(buf, cap, "/main/st"); break;
    default: std::snprintf(buf, cap, "/ch/01"); break;
  }
}

int stripToLibChanIdx(StripKind kind, int idx) {
  if (kind == STRIP_DCA) return -1;
  return strip_to_x32_selidx(kind, idx);
}

void queryHeadampIfValid(int haIdx) {
  if (!s_hasTarget || haIdx < 0 || haIdx >= 128) return;
  char addr[40];
  std::snprintf(addr, sizeof(addr), "/headamp/%03d/gain", haIdx);
  sendQueryToTarget(addr);
  std::snprintf(addr, sizeof(addr), "/headamp/%03d/phantom", haIdx);
  sendQueryToTarget(addr);
}

void querySingleStripParams(StripKind kind, int idx) {
  if (!s_hasTarget) return;
  char addr[64];
  char prefix[24];
  formatStripPrefix(kind, idx, prefix, sizeof(prefix));
  static constexpr const char *kCfgSubs[3] = {"config/name", "config/color", "config/icon"};
  for (const char *s : kCfgSubs) {
    std::snprintf(addr, sizeof(addr), "%s/%s", prefix, s);
    sendQueryToTarget(addr);
  }
  if (kind == STRIP_DCA) {
    std::snprintf(addr, sizeof(addr), "%s/fader", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/on", prefix);
    sendQueryToTarget(addr);
  } else {
    std::snprintf(addr, sizeof(addr), "%s/mix/fader", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/mix/on", prefix);
    sendQueryToTarget(addr);
  }
  if (kind == STRIP_CH || kind == STRIP_AUX) {
    std::snprintf(addr, sizeof(addr), "%s/grp/dca", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/grp/mute", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/config/source", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/preamp/trim", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/preamp/invert", prefix);
    sendQueryToTarget(addr);
    if (kind == STRIP_CH) {
      std::snprintf(addr, sizeof(addr), "%s/preamp/hpon", prefix);
      sendQueryToTarget(addr);
      std::snprintf(addr, sizeof(addr), "%s/preamp/hpf", prefix);
      sendQueryToTarget(addr);
    }
    std::snprintf(addr, sizeof(addr), "%s/mix/st", prefix);
    sendQueryToTarget(addr);
    std::snprintf(addr, sizeof(addr), "%s/mix/pan", prefix);
    sendQueryToTarget(addr);
  }
}

void sendStripFaderOsc(StripKind kind, int idx, float val) {
  char addr[48];
  char prefix[24];
  formatStripPrefix(kind, idx, prefix, sizeof(prefix));
  if (kind == STRIP_DCA) {
    std::snprintf(addr, sizeof(addr), "%s/fader", prefix);
  } else {
    std::snprintf(addr, sizeof(addr), "%s/mix/fader", prefix);
  }
  sendFloatToTarget(addr, val);
}

void sendStripMuteOsc(StripKind kind, int idx, bool muted) {
  char addr[48];
  char prefix[24];
  formatStripPrefix(kind, idx, prefix, sizeof(prefix));
  // CRITICAL: X32 strip 'on' is 1=unmuted, 0=muted
  const int32_t wireOn = muted ? 0 : 1;
  if (kind == STRIP_DCA) {
    std::snprintf(addr, sizeof(addr), "%s/on", prefix);
  } else {
    std::snprintf(addr, sizeof(addr), "%s/mix/on", prefix);
  }
  sendIntToTarget(addr, wireOn);
}

int stripToSoloSwitchIndex(StripKind kind, int idx) {
  switch (kind) {
    case STRIP_CH: return (idx >= 0 && idx < 32) ? (idx + 1) : -1;
    case STRIP_AUX: return (idx >= 0 && idx < 8) ? (33 + idx) : -1;
    case STRIP_BUS: return (idx >= 0 && idx < 16) ? (49 + idx) : -1;
    case STRIP_MAIN: return 71;
    case STRIP_DCA: return (idx >= 0 && idx < 8) ? (73 + idx) : -1;
    default: return -1;
  }
}

bool soloSwitchIndexToStrip(int sw, StripKind *outKind, int *outIdx) {
  if (sw >= 1 && sw <= 32) {
    *outKind = STRIP_CH;
    *outIdx = sw - 1;
    return true;
  }
  if (sw >= 33 && sw <= 40) {
    *outKind = STRIP_AUX;
    *outIdx = sw - 33;
    return true;
  }
  if (sw >= 49 && sw <= 64) {
    *outKind = STRIP_BUS;
    *outIdx = sw - 49;
    return true;
  }
  if (sw == 71) {
    *outKind = STRIP_MAIN;
    *outIdx = 0;
    return true;
  }
  if (sw >= 73 && sw <= 80) {
    *outKind = STRIP_DCA;
    *outIdx = sw - 73;
    return true;
  }
  return false;
}

void sendXremoteAndMeters() {
  if (!s_hasTarget) return;
  size_t n1 = osc_build_query(s_txBuf, sizeof(s_txBuf), "/xremote");
  sendOscBytes(s_targetIp, s_txBuf, n1);
  size_t n2 = osc_build_meters_req(s_txBuf, sizeof(s_txBuf), "/meters/1");
  sendOscBytes(s_targetIp, s_txBuf, n2);
  s_lastXremoteMs = millis();
  s_lastMetersMs = millis();
}

void finishConsoleCameOnline(int8_t activeBus, bool queryXinfo) {
  s_fastRetriesLeft = 0;
  s_lastScreenActivityMs = millis();
  if (!boardGetBacklight()) {
    boardSetBacklight(true);
    if (mixerStateLock(20)) {
      mixerStateUnsafe().backlightOn = true;
      mixerMarkUiOnlyDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    Serial.println("[SCREEN] Woke display on X32 coming online");
  }
  if (queryXinfo) {
    sendQueryToTarget("/xinfo");
  }
  sendXremoteAndMeters();
  x32ClientStartFullSync();
  if (activeBus >= 0 && activeBus < NUM_BUSES) {
    x32ClientRequestBusSends(activeBus);
  }
  Serial.printf("[X32] Console online (%s) - subscribed & syncing\n",
                s_targetIp.toString().c_str());
}

void markTargetConsoleOffline(const char *reason) {
  s_fullSyncActive = false;
  s_busSendSyncActive = false;
  if (mixerStateLock(30)) {
    MixerState &st = mixerStateUnsafe();
    st.x32Connected = false;
    st.x32Connecting = false;
    st.syncProgress = 0;
    mixerClearMetersLocked(st);
    std::snprintf(st.statusMsg, sizeof(st.statusMsg),
                  "X32 offline - waiting for %s...",
                  st.x32Ip[0] ? st.x32Ip : "console");
    mixerMarkDirty(DIRTY_HEADER | DIRTY_NETWORK | DIRTY_METERS);
    mixerStateUnlock();
  }
  Serial.printf("[X32] Console offline (%s)\n", reason ? reason : "timeout");
}

// Emits one OSC query corresponding to flat index s_syncStep. Returns false when complete.
bool emitSyncStep(int step, int *outTotalSteps) {
  // Layout of sync steps (1187 total):
  // 0..5:       /config/routing/IN/{1-8,9-16,17-24,25-32,AUX}, /-prefs/haflags
  // 6..453:     32 channels * 14 props
  // 454..549:   8 aux * 12 props
  // 550..613:   Local headamps 000..031 * 2 props (gain, phantom)
  // 614..693:   16 buses * 5 props (name, color, icon, fader, on)
  // 694..733:   8 dcas * 5 props (name, color, icon, fader, on)
  // 734..737:   Main LR (name, color, fader, on)
  // 738..743:   Mute Groups 1..6
  // 744..759:   ChLinks 1-2..31-32
  // 760..767:   BusLinks 1-2..15-16
  // 768..769:   /-stat/selidx, /-show/prepos/current
  // 770..834:   65 valid solo switches
  // 835..930:   32 Scenes * 3 props (name, notes, hasdata)
  // 931..994:   32 Ch Presets * 2 props (/-libs/ch/001..032 name, hasdata)
  // 995..1186:  Remote AES50 headamps 032..127 * 2 props (gain, phantom)
  static constexpr int TOTAL_STEPS = 1187;
  if (outTotalSteps) *outTotalSteps = TOTAL_STEPS;
  if (step < 0 || step >= TOTAL_STEPS) return false;

  char addr[64];
  if (step < 6) {
    static constexpr const char *kRoutingQueries[6] = {
        "/config/routing/IN/1-8",   "/config/routing/IN/9-16",
        "/config/routing/IN/17-24", "/config/routing/IN/25-32",
        "/config/routing/IN/AUX",   "/-prefs/haflags"};
    sendQueryToTarget(kRoutingQueries[step]);
    return true;
  }
  step -= 6;
  if (step < 32 * 14) {
    const int ch = step / 14;
    const int sub = step % 14;
    static constexpr const char *kSubs[14] = {
        "config/name",   "config/color",   "config/icon",   "config/source",
        "mix/fader",     "mix/on",         "grp/dca",       "grp/mute",
        "preamp/trim",   "preamp/invert",  "preamp/hpon",   "preamp/hpf",
        "mix/st",        "mix/pan"};
    std::snprintf(addr, sizeof(addr), "/ch/%02d/%s", ch + 1, kSubs[sub]);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 32 * 14;
  if (step < 8 * 12) {
    const int aux = step / 12;
    const int sub = step % 12;
    static constexpr const char *kSubs[12] = {
        "config/name",   "config/color",   "config/icon",   "config/source",
        "mix/fader",     "mix/on",         "grp/dca",       "grp/mute",
        "preamp/trim",   "preamp/invert",  "mix/st",        "mix/pan"};
    std::snprintf(addr, sizeof(addr), "/auxin/%02d/%s", aux + 1, kSubs[sub]);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 8 * 12;
  if (step < 32 * 2) {
    const int ha = step / 2;
    const int sub = step % 2;
    std::snprintf(addr, sizeof(addr), "/headamp/%03d/%s", ha, sub == 0 ? "gain" : "phantom");
    sendQueryToTarget(addr);
    return true;
  }
  step -= 32 * 2;
  if (step < 80) {
    const int bus = step / 5;
    const int sub = step % 5;
    static constexpr const char *kSubs[5] = {
        "config/name", "config/color", "config/icon", "mix/fader", "mix/on"};
    std::snprintf(addr, sizeof(addr), "/bus/%02d/%s", bus + 1, kSubs[sub]);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 80;
  if (step < 40) {
    const int dca = step / 5;
    const int sub = step % 5;
    static constexpr const char *kSubs[5] = {
        "config/name", "config/color", "config/icon", "fader", "on"};
    std::snprintf(addr, sizeof(addr), "/dca/%d/%s", dca + 1, kSubs[sub]);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 40;
  if (step < 4) {
    static constexpr const char *kMain[4] = {
        "/main/st/config/name", "/main/st/config/color",
        "/main/st/mix/fader", "/main/st/mix/on"};
    sendQueryToTarget(kMain[step]);
    return true;
  }
  step -= 4;
  if (step < 6) {
    std::snprintf(addr, sizeof(addr), "/config/mute/%d", step + 1);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 6;
  if (step < 16) {
    const int first = step * 2 + 1;
    std::snprintf(addr, sizeof(addr), "/config/chlink/%d-%d", first, first + 1);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 16;
  if (step < 8) {
    const int first = step * 2 + 1;
    std::snprintf(addr, sizeof(addr), "/config/buslink/%d-%d", first, first + 1);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 8;
  if (step == 0) {
    sendQueryToTarget("/-stat/selidx");
    return true;
  }
  if (step == 1) {
    sendQueryToTarget("/-show/prepos/current");
    return true;
  }
  step -= 2;
  if (step < 65) {
    int sw = 1;
    if (step < 32) sw = step + 1;
    else if (step < 40) sw = 33 + (step - 32);
    else if (step < 56) sw = 49 + (step - 40);
    else if (step == 56) sw = 71;
    else sw = 73 + (step - 57);
    std::snprintf(addr, sizeof(addr), "/-stat/solosw/%02d", sw);
    sendQueryToTarget(addr);
    return true;
  }
  step -= 65;
  if (step < NUM_SCENES * 3) {
    const int sc = step / 3;
    const int sub = step % 3;
    static constexpr const char *kSceneSubs[3] = {"name", "notes", "hasdata"};
    std::snprintf(addr, sizeof(addr), "/-show/showfile/scene/%03d/%s", sc, kSceneSubs[sub]);
    sendQueryToTarget(addr);
    return true;
  }
  step -= NUM_SCENES * 3;
  if (step < NUM_CH_PRESETS * 2) {
    const int pr = step / 2;
    const int sub = step % 2;
    std::snprintf(addr, sizeof(addr), "/-libs/ch/%03d/%s", pr + 1, sub == 0 ? "name" : "hasdata");
    sendQueryToTarget(addr);
    return true;
  }
  step -= NUM_CH_PRESETS * 2;
  if (step < 96 * 2) {
    const int ha = 32 + (step / 2);
    const int sub = step % 2;
    std::snprintf(addr, sizeof(addr), "/headamp/%03d/%s", ha, sub == 0 ? "gain" : "phantom");
    sendQueryToTarget(addr);
    return true;
  }
  return false;
}

void recordDiscoveredConsole(const IPAddress &remoteIp, const OscPacketView &pkt) {
  if (remoteIp == IPAddress(0, 0, 0, 0) || remoteIp == IPAddress(255, 255, 255, 255)) {
    return;
  }
  const IPAddress localIp = WiFi.localIP();
  if (localIp != IPAddress(0, 0, 0, 0) && remoteIp == localIp) {
    return;
  }
  if (!x32_is_valid_xinfo_reply(pkt)) {
    return;
  }
  IPAddress advIp;
  if (advIp.fromString(pkt.args[0].s) && localIp != IPAddress(0, 0, 0, 0) && advIp == localIp) {
    return;
  }
  const char *finalIp = pkt.args[0].s;
  const char *name = pkt.args[1].s;
  const char *model = pkt.args[2].s;
  const char *fw = pkt.args[3].s;

  bool autoConnectNow = false;
  bool cameOnline = false;
  int8_t activeBus = -1;
  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    bool changed = false;
    int slot = -1;
    for (int i = 0; i < st.consoleCount; ++i) {
      if (std::strcmp(st.consoles[i].ip, finalIp) == 0) {
        slot = i;
        break;
      }
    }
    if (slot < 0 && st.consoleCount < MAX_DISCOVERED_X32) {
      slot = st.consoleCount++;
      changed = true;
    }
    if (slot >= 0) {
      DiscoveredConsole &c = st.consoles[slot];
      if (std::strcmp(c.ip, finalIp) != 0 || std::strcmp(c.name, name) != 0 ||
          std::strcmp(c.model, model) != 0 || std::strcmp(c.firmware, fw) != 0) {
        changed = true;
      }
      std::snprintf(c.ip, sizeof(c.ip), "%s", finalIp);
      std::snprintf(c.name, sizeof(c.name), "%s", name);
      std::snprintf(c.model, sizeof(c.model), "%s", model);
      std::snprintf(c.firmware, sizeof(c.firmware), "%s", fw);
      c.lastSeenMs = millis();
    }

    const bool matchesTarget =
        s_hasTarget && (remoteIp == s_targetIp || std::strcmp(st.x32Ip, finalIp) == 0);
    if (matchesTarget) {
      st.lastX32RxMs = millis();
      if (std::strcmp(st.x32Name, name) != 0 || std::strcmp(st.x32Model, model) != 0 ||
          std::strcmp(st.x32Firmware, fw) != 0) {
        std::snprintf(st.x32Name, sizeof(st.x32Name), "%s", name);
        std::snprintf(st.x32Model, sizeof(st.x32Model), "%s", model);
        std::snprintf(st.x32Firmware, sizeof(st.x32Firmware), "%s", fw);
        changed = true;
      }
      if (!st.x32Connected || st.x32Connecting) {
        cameOnline = !st.x32Connected;
        activeBus = st.activeBusSend;
        st.x32Connected = true;
        st.x32Connecting = false;
        std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Connected to %s (%s)",
                      st.x32Name[0] ? st.x32Name : "X32", st.x32Ip);
        changed = true;
      }
      autoConnectNow = false;
    } else if (!s_hasTarget ||
               (!st.x32Connected && st.x32Name[0] != '\0' && name[0] != '\0' &&
                std::strcmp(st.x32Name, name) == 0)) {
      std::snprintf(st.x32Ip, sizeof(st.x32Ip), "%s", finalIp);
      std::snprintf(st.x32Name, sizeof(st.x32Name), "%s", name);
      std::snprintf(st.x32Model, sizeof(st.x32Model), "%s", model);
      std::snprintf(st.x32Firmware, sizeof(st.x32Firmware), "%s", fw);
      changed = true;
      autoConnectNow = true;
    }
    if (changed) {
      mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
    }
    mixerStateUnlock();
  }

  if (cameOnline) {
    finishConsoleCameOnline(activeBus, false);
  } else if (autoConnectNow) {
    x32ClientConnect(finalIp);
  }
}

void applyStripParam(StripKind kind, int idx, const char *sub, const OscPacketView &pkt) {
  if (pkt.num_args < 1) return;
  const OscArg &a0 = pkt.args[0];

  if (!mixerStateLock(30)) return;
  MixerState &st = mixerStateUnsafe();
  StripState *strip = mixerGetStripMutable(st, kind, idx);
  if (!strip) {
    mixerStateUnlock();
    return;
  }

  uint32_t dirty = DIRTY_NONE;
  int queryHaIdx = -1;
  if (std::strcmp(sub, "config/name") == 0 && a0.type == OSC_ARG_STR) {
    if (std::strncmp(strip->name, a0.s, sizeof(strip->name) - 1) != 0) {
      std::snprintf(strip->name, sizeof(strip->name), "%s", a0.s);
      dirty = DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_FADERS | DIRTY_MATRIX | DIRTY_LIBRARY | DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "config/color") == 0 && a0.type == OSC_ARG_INT) {
    const uint8_t newColor = static_cast<uint8_t>(a0.i & 0x0F);
    if (strip->color != newColor) {
      strip->color = newColor;
      dirty = DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_FADERS | DIRTY_MATRIX | DIRTY_LIBRARY | DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "config/icon") == 0 && a0.type == OSC_ARG_INT) {
    const uint8_t newIcon = static_cast<uint8_t>(a0.i < 1 ? 1 : (a0.i > 74 ? 74 : a0.i));
    if (strip->icon != newIcon) {
      strip->icon = newIcon;
      dirty = DIRTY_SCRIBBLE | DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "config/source") == 0 && a0.type == OSC_ARG_INT &&
             (kind == STRIP_CH || kind == STRIP_AUX)) {
    const uint8_t newSrc = static_cast<uint8_t>((a0.i >= 0 && a0.i <= 64) ? a0.i : 0);
    uint8_t *curSrc = (kind == STRIP_CH) ? &st.chCfg[idx].source : &st.auxCfg[idx].source;
    if (*curSrc != newSrc) {
      *curSrc = newSrc;
      dirty = DIRTY_OVERVIEW | DIRTY_CONFIG;
      queryHaIdx = x32_source_to_headamp_idx(newSrc, st.inRouting);
    }
  } else if (std::strcmp(sub, "preamp/trim") == 0 && a0.type == OSC_ARG_FLOAT &&
             (kind == STRIP_CH || kind == STRIP_AUX)) {
    float v = a0.f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    float *curTrim = (kind == STRIP_CH) ? &st.chCfg[idx].trim : &st.auxCfg[idx].trim;
    if (std::fabs(*curTrim - v) > 0.0005f) {
      *curTrim = v;
      dirty = DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "preamp/invert") == 0 && a0.type == OSC_ARG_INT &&
             (kind == STRIP_CH || kind == STRIP_AUX)) {
    const bool newInv = (a0.i != 0);
    bool *curInv = (kind == STRIP_CH) ? &st.chCfg[idx].invert : &st.auxCfg[idx].invert;
    if (*curInv != newInv) {
      *curInv = newInv;
      dirty = DIRTY_OVERVIEW | DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "preamp/hpon") == 0 && a0.type == OSC_ARG_INT && kind == STRIP_CH) {
    const bool newHpon = (a0.i != 0);
    if (st.chCfg[idx].hpon != newHpon) {
      st.chCfg[idx].hpon = newHpon;
      dirty = DIRTY_OVERVIEW | DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "preamp/hpf") == 0 && a0.type == OSC_ARG_FLOAT && kind == STRIP_CH) {
    float v = a0.f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (std::fabs(st.chCfg[idx].hpf - v) > 0.0005f) {
      st.chCfg[idx].hpf = v;
      dirty = DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "mix/st") == 0 && a0.type == OSC_ARG_INT &&
             (kind == STRIP_CH || kind == STRIP_AUX)) {
    const bool newSt = (a0.i != 0);
    bool *curSt = (kind == STRIP_CH) ? &st.chCfg[idx].mainSt : &st.auxCfg[idx].mainSt;
    if (*curSt != newSt) {
      *curSt = newSt;
      dirty = DIRTY_CONFIG;
    }
  } else if (std::strcmp(sub, "mix/pan") == 0 && a0.type == OSC_ARG_FLOAT &&
             (kind == STRIP_CH || kind == STRIP_AUX)) {
    float v = a0.f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    float *curPan = (kind == STRIP_CH) ? &st.chCfg[idx].pan : &st.auxCfg[idx].pan;
    if (std::fabs(*curPan - v) > 0.0005f) {
      *curPan = v;
      dirty = DIRTY_CONFIG;
    }
  } else if ((std::strcmp(sub, "mix/fader") == 0 || std::strcmp(sub, "fader") == 0) &&
             a0.type == OSC_ARG_FLOAT) {
    if (std::fabs(strip->fader - a0.f) > 0.0005f) {
      strip->fader = a0.f;
      dirty = DIRTY_FADERS | DIRTY_OVERVIEW;
    }
  } else if ((std::strcmp(sub, "mix/on") == 0 || std::strcmp(sub, "on") == 0) &&
             a0.type == OSC_ARG_INT) {
    const bool newMuted = (a0.i == 0);
    if (strip->muted != newMuted) {
      strip->muted = newMuted;
      dirty = DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_SCRIBBLE;
    }
  } else if (std::strcmp(sub, "grp/dca") == 0 && a0.type == OSC_ARG_INT) {
    const uint8_t newDca = static_cast<uint8_t>(a0.i & 0xFF);
    if (strip->dcaMask != newDca) {
      strip->dcaMask = newDca;
      dirty = DIRTY_MATRIX;
    }
  } else if (std::strcmp(sub, "grp/mute") == 0 && a0.type == OSC_ARG_INT) {
    const uint8_t newMute = static_cast<uint8_t>(a0.i & 0x3F);
    if (strip->muteMask != newMute) {
      strip->muteMask = newMute;
      dirty = DIRTY_MATRIX;
    }
  } else if (kind == STRIP_CH && std::strncmp(sub, "mix/", 4) == 0 && std::strlen(sub) >= 8) {
    // e.g. mix/01/level or mix/01/on
    const int busNum = std::atoi(sub + 4);
    const char *slash2 = std::strchr(sub + 4, '/');
    if (busNum >= 1 && busNum <= NUM_BUSES && slash2) {
      const int bIdx = busNum - 1;
      if (std::strcmp(slash2, "/level") == 0 && a0.type == OSC_ARG_FLOAT) {
        if (std::fabs(st.chSendLevel[idx][bIdx] - a0.f) > 0.0005f) {
          st.chSendLevel[idx][bIdx] = a0.f;
          dirty = DIRTY_FADERS;
        }
      } else if (std::strcmp(slash2, "/on") == 0 && a0.type == OSC_ARG_INT) {
        const bool newSendOn = (a0.i != 0);
        if (st.chSendOn[idx][bIdx] != newSendOn) {
          st.chSendOn[idx][bIdx] = newSendOn;
          dirty = DIRTY_FADERS;
        }
      }
    }
  }

  if (dirty != DIRTY_NONE) {
    mixerMarkDirty(dirty);
  }
  mixerStateUnlock();
  if (queryHaIdx >= 0) {
    queryHeadampIfValid(queryHaIdx);
  }
}

void handleIncomingOsc(const OscPacketView &pkt, const IPAddress &remoteIp) {
  const char *addr = pkt.addr;
  if (!addr) return;

  if (std::strcmp(addr, "/xinfo") == 0) {
    recordDiscoveredConsole(remoteIp, pkt);
    return;
  }

  // Only treat packets from target console (or mark connected when matching)
  const bool fromTarget = s_hasTarget && (remoteIp == s_targetIp);
  if (fromTarget) {
    bool cameOnline = false;
    int8_t activeBus = -1;
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      st.lastX32RxMs = millis();
      if (!st.x32Connected) {
        cameOnline = true;
        activeBus = st.activeBusSend;
        st.x32Connected = true;
        st.x32Connecting = false;
        std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Connected to %s (%s)",
                      st.x32Name[0] ? st.x32Name : "X32", st.x32Ip);
        mixerMarkDirty(DIRTY_HEADER | DIRTY_NETWORK);
      }
      mixerStateUnlock();
    }
    if (cameOnline) {
      finishConsoleCameOnline(activeBus, true);
    }
  }
  if (!fromTarget) return;

  if (std::strcmp(addr, "/meters/1") == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_BLOB) {
    uint8_t pct[32] = {0};
    uint32_t clipMask = 0;
    if (x32_parse_meters1_blob(pkt.args[0].blob, pkt.args[0].blob_len, pct, &clipMask)) {
      if (mixerStateLock(20)) {
        MixerState &st = mixerStateUnsafe();
        uint16_t sum = 0;
        for (int i = 0; i < NUM_CHANNELS; ++i) {
          st.ch[i].meterPct = pct[i];
          st.ch[i].clip = ((clipMask >> i) & 1u) != 0;
          sum += st.ch[i].muted ? 0 : pct[i];
        }
        uint16_t mainEst = (sum / 10u);
        if (mainEst > 100) mainEst = 100;
        if (st.mainLr.muted) mainEst = 0;
        st.mainLr.meterPct = static_cast<uint8_t>(mainEst);
        mixerMarkDirty(DIRTY_METERS);
        mixerStateUnlock();
      }
    }
    return;
  }

  if (std::strncmp(addr, "/ch/", 4) == 0 && std::strlen(addr) > 7) {
    const int ch = std::atoi(addr + 4);
    const char *sub = std::strchr(addr + 4, '/');
    if (ch >= 1 && ch <= NUM_CHANNELS && sub) {
      applyStripParam(STRIP_CH, ch - 1, sub + 1, pkt);
    }
    return;
  }

  if (std::strncmp(addr, "/auxin/", 7) == 0 && std::strlen(addr) > 10) {
    const int aux = std::atoi(addr + 7);
    const char *sub = std::strchr(addr + 7, '/');
    if (aux >= 1 && aux <= NUM_AUX && sub) {
      applyStripParam(STRIP_AUX, aux - 1, sub + 1, pkt);
    }
    return;
  }

  if (std::strncmp(addr, "/bus/", 5) == 0 && std::strlen(addr) > 8) {
    const int bus = std::atoi(addr + 5);
    const char *sub = std::strchr(addr + 5, '/');
    if (bus >= 1 && bus <= NUM_BUSES && sub) {
      applyStripParam(STRIP_BUS, bus - 1, sub + 1, pkt);
    }
    return;
  }

  if (std::strncmp(addr, "/dca/", 5) == 0 && std::strlen(addr) > 7) {
    const int dca = std::atoi(addr + 5);
    const char *sub = std::strchr(addr + 5, '/');
    if (dca >= 1 && dca <= NUM_DCAS && sub) {
      applyStripParam(STRIP_DCA, dca - 1, sub + 1, pkt);
    }
    return;
  }

  if (std::strncmp(addr, "/main/st/", 9) == 0) {
    applyStripParam(STRIP_MAIN, 0, addr + 9, pkt);
    return;
  }

  if (std::strncmp(addr, "/headamp/", 9) == 0 && pkt.num_args >= 1) {
    const int ha = std::atoi(addr + 9);
    const char *slash = std::strchr(addr + 9, '/');
    const char *prop = slash ? (slash + 1) : "";
    if (ha >= 0 && ha < 128 && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      if (std::strcmp(prop, "gain") == 0 && pkt.args[0].type == OSC_ARG_FLOAT) {
        float v = pkt.args[0].f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        if (std::fabs(st.headampGain[ha] - v) > 0.0005f) {
          st.headampGain[ha] = v;
          mixerMarkDirty(DIRTY_CONFIG);
        }
      } else if (std::strcmp(prop, "phantom") == 0 && pkt.args[0].type == OSC_ARG_INT) {
        const bool ph = (pkt.args[0].i != 0);
        if (st.headampPhantom[ha] != ph) {
          st.headampPhantom[ha] = ph;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
        }
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/config/routing/IN/", 19) == 0 && pkt.num_args >= 1 &&
      pkt.args[0].type == OSC_ARG_INT) {
    const char *blk = addr + 19;
    int rIdx = -1;
    if (std::strcmp(blk, "1-8") == 0) rIdx = 0;
    else if (std::strcmp(blk, "9-16") == 0) rIdx = 1;
    else if (std::strcmp(blk, "17-24") == 0) rIdx = 2;
    else if (std::strcmp(blk, "25-32") == 0) rIdx = 3;
    else if (std::strcmp(blk, "AUX") == 0) rIdx = 4;
    if (rIdx >= 0 && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      const uint8_t newVal = static_cast<uint8_t>(pkt.args[0].i < 0 ? 0 : pkt.args[0].i);
      if (st.inRouting[rIdx] != newVal) {
        st.inRouting[rIdx] = newVal;
        mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strcmp(addr, "/-prefs/haflags") == 0 && pkt.num_args >= 1 &&
      pkt.args[0].type == OSC_ARG_INT) {
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      const uint8_t nf = static_cast<uint8_t>(pkt.args[0].i & 0x0F);
      if (st.haFlags != nf) {
        st.haFlags = nf;
        mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/config/mute/", 13) == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_INT) {
    const int mg = std::atoi(addr + 13);
    if (mg >= 1 && mg <= NUM_MUTE_GRPS && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      const bool v = (pkt.args[0].i != 0);
      if (st.muteGroupEngaged[mg - 1] != v) {
        st.muteGroupEngaged[mg - 1] = v;
        mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_MATRIX);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/config/chlink/", 15) == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_INT) {
    const int first = std::atoi(addr + 15);
    if (first >= 1 && first <= 31 && (first % 2) == 1 && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      const bool v = (pkt.args[0].i != 0);
      if (st.chLinks[(first - 1) / 2] != v) {
        st.chLinks[(first - 1) / 2] = v;
        mixerMarkDirty(DIRTY_MATRIX | DIRTY_FADERS | DIRTY_CONFIG);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/config/buslink/", 16) == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_INT) {
    const int first = std::atoi(addr + 16);
    if (first >= 1 && first <= 15 && (first % 2) == 1 && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      const bool v = (pkt.args[0].i != 0);
      if (st.busLinks[(first - 1) / 2] != v) {
        st.busLinks[(first - 1) / 2] = v;
        mixerMarkDirty(DIRTY_MATRIX);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strcmp(addr, "/-stat/selidx") == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_INT) {
    StripKind k;
    int idx = 0;
    if (x32_selidx_to_strip(pkt.args[0].i, &k, &idx) && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      if (st.followConsoleSelect &&
          (st.selectedKind != k || st.selectedIdx != static_cast<int16_t>(idx))) {
        st.selectedKind = k;
        st.selectedIdx = static_cast<int16_t>(idx);
        mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG | DIRTY_SCRIBBLE | DIRTY_LIBRARY | DIRTY_HEADER);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/-stat/solosw/", 14) == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_INT) {
    const int sw = std::atoi(addr + 14);
    StripKind k;
    int idx = 0;
    if (soloSwitchIndexToStrip(sw, &k, &idx) && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      StripState *s = mixerGetStripMutable(st, k, idx);
      const bool v = (pkt.args[0].i != 0);
      if (s && s->solo != v) {
        s->solo = v;
        mixerMarkDirty(DIRTY_HEADER | DIRTY_OVERVIEW | DIRTY_FADERS);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strcmp(addr, "/-show/prepos/current") == 0 && pkt.num_args >= 1 && pkt.args[0].type == OSC_ARG_INT) {
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      const int16_t v = static_cast<int16_t>(pkt.args[0].i);
      if (st.currentScene != v) {
        st.currentScene = v;
        mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_HEADER | DIRTY_LIBRARY);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/-show/showfile/scene/", 22) == 0 && pkt.num_args >= 1) {
    const int sc = std::atoi(addr + 22);
    const char *slash = std::strchr(addr + 22, '/');
    const char *prop = slash ? (slash + 1) : "";
    if (sc >= 0 && sc < NUM_SCENES && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      uint32_t dirty = DIRTY_NONE;
      if (std::strcmp(prop, "name") == 0 && pkt.args[0].type == OSC_ARG_STR) {
        if (std::strncmp(st.scenes[sc].name, pkt.args[0].s, sizeof(st.scenes[sc].name) - 1) != 0) {
          std::snprintf(st.scenes[sc].name, sizeof(st.scenes[sc].name), "%s", pkt.args[0].s);
          dirty |= DIRTY_OVERVIEW | DIRTY_LIBRARY;
        }
        if (pkt.args[0].s[0] != '\0' && !st.scenes[sc].hasData) {
          st.scenes[sc].hasData = true;
          dirty |= DIRTY_LIBRARY;
        }
      } else if (std::strcmp(prop, "notes") == 0 && pkt.args[0].type == OSC_ARG_STR) {
        if (std::strncmp(st.scenes[sc].notes, pkt.args[0].s, sizeof(st.scenes[sc].notes) - 1) != 0) {
          std::snprintf(st.scenes[sc].notes, sizeof(st.scenes[sc].notes), "%s", pkt.args[0].s);
          dirty |= DIRTY_LIBRARY;
        }
      } else if (std::strcmp(prop, "hasdata") == 0 && pkt.args[0].type == OSC_ARG_INT) {
        const bool hd = (pkt.args[0].i != 0);
        if (st.scenes[sc].hasData != hd) {
          st.scenes[sc].hasData = hd;
          dirty |= DIRTY_LIBRARY;
        }
      }
      if (dirty != DIRTY_NONE) {
        mixerMarkDirty(dirty);
      }
      mixerStateUnlock();
    }
    return;
  }

  if (std::strncmp(addr, "/-libs/ch/", 10) == 0 && pkt.num_args >= 1) {
    const int pr = std::atoi(addr + 10) - 1;
    const char *slash = std::strchr(addr + 10, '/');
    const char *prop = slash ? (slash + 1) : "";
    if (pr >= 0 && pr < NUM_CH_PRESETS && mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      uint32_t dirty = DIRTY_NONE;
      if (std::strcmp(prop, "name") == 0 && pkt.args[0].type == OSC_ARG_STR) {
        if (std::strncmp(st.chPresets[pr].name, pkt.args[0].s, sizeof(st.chPresets[pr].name) - 1) != 0) {
          std::snprintf(st.chPresets[pr].name, sizeof(st.chPresets[pr].name), "%s", pkt.args[0].s);
          dirty |= DIRTY_LIBRARY;
        }
        if (pkt.args[0].s[0] != '\0' && !st.chPresets[pr].hasData) {
          st.chPresets[pr].hasData = true;
          dirty |= DIRTY_LIBRARY;
        }
      } else if (std::strcmp(prop, "hasdata") == 0 && pkt.args[0].type == OSC_ARG_INT) {
        const bool hd = (pkt.args[0].i != 0);
        if (st.chPresets[pr].hasData != hd) {
          st.chPresets[pr].hasData = hd;
          dirty |= DIRTY_LIBRARY;
        }
      }
      if (dirty != DIRTY_NONE) {
        mixerMarkDirty(dirty);
      }
      mixerStateUnlock();
    }
    return;
  }

  if ((std::strcmp(addr, "/save") == 0 || std::strcmp(addr, "/load") == 0) &&
      pkt.num_args >= 2 && pkt.args[1].type == OSC_ARG_INT && pkt.args[1].i == 0) {
    if (mixerStateLock(20)) {
      MixerState &st = mixerStateUnsafe();
      std::snprintf(st.statusMsg, sizeof(st.statusMsg), "X32 rejected %s", addr);
      mixerMarkDirty(DIRTY_HEADER | DIRTY_LIBRARY);
      mixerStateUnlock();
    }
  }
}

void ensureUdpBound() {
  if (s_udpBound) return;
  if (s_udp.begin(X32_LOCAL_UDP_PORT)) {
    s_udpBound = true;
    Serial.printf("[X32] UDP client bound on local port %u (target port %u)\n",
                  X32_LOCAL_UDP_PORT, X32_OSC_PORT);
  }
}

void sendDiscoveryBroadcast(bool updateStatusMsg) {
  if (WiFi.status() != WL_CONNECTED) return;
  ensureUdpBound();
  const size_t n = osc_build_query(s_txBuf, sizeof(s_txBuf), "/xinfo");
  IPAddress globalBcast(255, 255, 255, 255);
  sendOscBytes(globalBcast, s_txBuf, n);

  IPAddress local = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress subnetBcast(
      static_cast<uint8_t>(local[0] | ~mask[0]),
      static_cast<uint8_t>(local[1] | ~mask[1]),
      static_cast<uint8_t>(local[2] | ~mask[2]),
      static_cast<uint8_t>(local[3] | ~mask[3]));
  sendOscBytes(subnetBcast, s_txBuf, n);
  s_lastDiscoverMs = millis();

  if (updateStatusMsg && mixerStateLock(30)) {
    MixerState &st = mixerStateUnsafe();
    std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Scanning LAN for X32/M32...");
    mixerMarkDirty(DIRTY_NETWORK);
    mixerStateUnlock();
  }
}

void screenPowerTick(uint32_t now) {
  if (!elapsedMs(now, s_lastScreenCheckMs, 100)) return;
  s_lastScreenCheckMs = now;

  const uint32_t seq = boardGetActivitySeq();
  if (seq != s_lastSeenActivitySeq) {
    s_lastSeenActivitySeq = seq;
    s_lastScreenActivityMs = now;
  }

  const bool hwBl = boardGetBacklight();
  bool x32Conn = false;
  uint16_t timeoutSec = DEFAULT_SCREEN_TIMEOUT_SEC;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    x32Conn = st.x32Connected;
    timeoutSec = st.screenTimeoutSec;
    if (st.backlightOn != hwBl) {
      st.backlightOn = hwBl;
      mixerMarkUiOnlyDirty(DIRTY_NETWORK);
    }
    mixerStateUnlock();
  }

  if (x32Conn) {
    s_lastScreenActivityMs = now;
    return;
  }

  if (hwBl && timeoutSec > 0 &&
      elapsedMs(now, s_lastScreenActivityMs, static_cast<uint32_t>(timeoutSec) * 1000u)) {
    boardSetBacklight(false);
    if (mixerStateLock(20)) {
      mixerStateUnsafe().backlightOn = false;
      mixerMarkUiOnlyDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    Serial.printf("[SCREEN] Auto-shutoff after %u s without X32 connection\n",
                  static_cast<unsigned>(timeoutSec));
  } else if (elapsedMs(now, s_lastScreenActivityMs, 3600000u)) {
    // Clamp old timestamp so int32_t elapsedMs math stays safe across multi-month uptimes
    s_lastScreenActivityMs = now - 3600000u;
  }
}

}  // namespace

void x32ClientInit() {
  ensureUdpBound();
  const uint32_t now = millis();
  s_lastScreenCheckMs = now;
  s_lastScreenActivityMs = now;
  s_lastSeenActivitySeq = boardGetActivitySeq();
}

void x32ClientOnWifiLost() {
  s_fullSyncActive = false;
  s_busSendSyncActive = false;
  if (s_udpBound) {
    s_udp.stop();
    s_udpBound = false;
  }
}

void x32ClientOnWifiConnected() {
  if (s_udpBound) {
    s_udp.stop();
    s_udpBound = false;
  }
  ensureUdpBound();
}

void x32ClientDiscover() {
  sendDiscoveryBroadcast(true);
}

void x32ClientConnect(const char *ip) {
  if (!ip || ip[0] == '\0') return;
  IPAddress parsed;
  if (!parsed.fromString(ip)) {
    if (mixerStateLock(30)) {
      MixerState &st = mixerStateUnsafe();
      std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Invalid X32 IP: %s", ip);
      mixerMarkDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    return;
  }

  static uint32_t s_lastConnectMs = 0;
  const uint32_t nowMs = millis();
  if (s_hasTarget && parsed == s_targetIp && s_lastConnectMs != 0 &&
      !elapsedMs(nowMs, s_lastConnectMs, 500)) {
    return;
  }
  s_lastConnectMs = nowMs;

  s_fullSyncActive = false;
  s_busSendSyncActive = false;
  ensureUdpBound();
  s_targetIp = parsed;
  s_hasTarget = true;
  s_connectAttemptStartMs = nowMs;
  s_lastProbeMs = nowMs;
  s_fastRetriesLeft = 2;
  wifiManagerSaveX32Ip(ip);

  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    std::snprintf(st.x32Ip, sizeof(st.x32Ip), "%s", ip);
    st.x32Connecting = true;
    st.x32Connected = false;
    st.syncProgress = 0;
    st.lastX32RxMs = nowMs;
    std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Connecting to X32 at %s...", ip);
    mixerMarkDirty(DIRTY_NETWORK | DIRTY_HEADER);
    mixerStateUnlock();
  }

  if (WiFi.status() == WL_CONNECTED) {
    sendQueryToTarget("/xinfo");
  }
}

void x32ClientStartFullSync() {
  if (!s_hasTarget) return;
  s_fullSyncActive = true;
  s_syncStep = 0;
  s_syncPass = 0;
  s_lastSyncTickMs = millis();
  if (mixerStateLock(20)) {
    mixerStateUnsafe().syncProgress = 0;
    mixerMarkDirty(DIRTY_NETWORK);
    mixerStateUnlock();
  }
}

void x32ClientRequestBusSends(int busIdx) {
  if (!s_hasTarget || busIdx < 0 || busIdx >= NUM_BUSES) return;
  s_busSendSyncActive = true;
  s_busSendSyncTarget = busIdx;
  s_busSendSyncCh = 0;
}

void x32ClientProcessCommand(const AppCommand &cmd) {
  char addr[64];
  char prefix[24];

  if (cmd.type == CMD_WIFI_SCAN) {
    wifiManagerStartScan();
    return;
  }
  if (cmd.type == CMD_WIFI_CONNECT) {
    wifiManagerConnect(cmd.text, cmd.text2);
    return;
  }
  if (cmd.type == CMD_X32_DISCOVER) {
    x32ClientDiscover();
    return;
  }
  if (cmd.type == CMD_X32_CONNECT) {
    x32ClientConnect(cmd.text);
    return;
  }
  if (cmd.type == CMD_X32_SYNC_ALL) {
    if (s_hasTarget && WiFi.status() == WL_CONNECTED) {
      bool isConn = false;
      if (mixerStateLock(30)) {
        MixerState &st = mixerStateUnsafe();
        isConn = st.x32Connected;
        if (!isConn) {
          st.x32Connecting = true;
          mixerMarkDirty(DIRTY_HEADER | DIRTY_NETWORK);
        }
        mixerStateUnlock();
      }
      if (isConn) {
        sendXremoteAndMeters();
        x32ClientStartFullSync();
      } else {
        s_connectAttemptStartMs = millis();
        s_fastRetriesLeft = 2;
        s_lastProbeMs = s_connectAttemptStartMs;
        sendQueryToTarget("/xinfo");
      }
    }
    return;
  }
  if (cmd.type == CMD_SET_BACKLIGHT) {
    boardSetBacklight(cmd.bval);
    if (cmd.bval) {
      s_lastScreenActivityMs = millis();
    }
    if (mixerStateLock(30)) {
      mixerStateUnsafe().backlightOn = boardGetBacklight();
      mixerMarkUiOnlyDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    return;
  }
  if (cmd.type == CMD_SET_SCREEN_TIMEOUT) {
    wifiManagerSaveScreenTimeoutSec(static_cast<uint16_t>(cmd.u32val));
    const uint16_t sec = wifiManagerLoadScreenTimeoutSec();
    s_lastScreenActivityMs = millis();
    if (mixerStateLock(30)) {
      mixerStateUnsafe().screenTimeoutSec = sec;
      mixerMarkUiOnlyDirty(DIRTY_NETWORK);
      mixerStateUnlock();
    }
    return;
  }
  if (cmd.type == CMD_REGEN_WEB_PASSKEY) {
    wifiManagerRegenPasskey();
    return;
  }
  if (cmd.type == CMD_REVOKE_WEB_SESSIONS) {
    wifiManagerRevokeAllSessions();
    return;
  }
  if (cmd.type == CMD_OTA_CHECK) {
    otaUpdaterRequestCheck();
    return;
  }
  if (cmd.type == CMD_OTA_INSTALL) {
    otaUpdaterRequestInstall();
    return;
  }

  // Update local MixerState optimistically first, then emit OSC if target is online
  bool sendOsc = false;
  bool needFullSync = false;
  int queryHaIdx = -1;
  if (mixerStateLock(50)) {
    MixerState &st = mixerStateUnsafe();
    sendOsc = s_hasTarget && st.x32Connected && (WiFi.status() == WL_CONNECTED);
    StripState *strip = mixerGetStripMutable(st, cmd.kind, cmd.idx);

    switch (cmd.type) {
      case CMD_SET_SCRIBBLE: {
        if (strip) {
          std::snprintf(strip->name, sizeof(strip->name), "%.12s", cmd.text);
          strip->color = cmd.color & 0x0F;
          strip->icon = cmd.icon ? cmd.icon : 1;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_FADERS | DIRTY_MATRIX | DIRTY_LIBRARY | DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/config/name", prefix);
            sendStrToTarget(addr, strip->name);
            std::snprintf(addr, sizeof(addr), "%s/config/color", prefix);
            sendIntToTarget(addr, strip->color);
            std::snprintf(addr, sizeof(addr), "%s/config/icon", prefix);
            sendIntToTarget(addr, strip->icon);
          }
        }
        break;
      }
      case CMD_SET_NAME: {
        if (strip) {
          std::snprintf(strip->name, sizeof(strip->name), "%.12s", cmd.text);
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_FADERS | DIRTY_MATRIX | DIRTY_LIBRARY | DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/config/name", prefix);
            sendStrToTarget(addr, strip->name);
          }
        }
        break;
      }
      case CMD_SET_COLOR: {
        if (strip) {
          strip->color = cmd.color & 0x0F;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_FADERS | DIRTY_MATRIX | DIRTY_LIBRARY | DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/config/color", prefix);
            sendIntToTarget(addr, strip->color);
          }
        }
        break;
      }
      case CMD_SET_ICON: {
        if (strip) {
          strip->icon = cmd.icon ? cmd.icon : 1;
          mixerMarkDirty(DIRTY_SCRIBBLE | DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/config/icon", prefix);
            sendIntToTarget(addr, strip->icon);
          }
        }
        break;
      }
      case CMD_SET_FADER: {
        if (strip) {
          float f = cmd.fval;
          if (f < 0.0f) f = 0.0f;
          if (f > 1.0f) f = 1.0f;
          strip->fader = f;
          mixerMarkDirty(DIRTY_FADERS | DIRTY_OVERVIEW);
          if (sendOsc) {
            sendStripFaderOsc(cmd.kind, cmd.idx, f);
          }
        }
        break;
      }
      case CMD_SET_MUTE: {
        if (strip) {
          strip->muted = cmd.bval;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_SCRIBBLE);
          if (sendOsc) {
            sendStripMuteOsc(cmd.kind, cmd.idx, strip->muted);
          }
        }
        break;
      }
      case CMD_SET_SOLO: {
        if (strip) {
          strip->solo = cmd.bval;
          mixerMarkDirty(DIRTY_HEADER | DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_SCRIBBLE);
          if (sendOsc) {
            int sw = stripToSoloSwitchIndex(cmd.kind, cmd.idx);
            if (sw > 0) {
              std::snprintf(addr, sizeof(addr), "/-stat/solosw/%02d", sw);
              sendIntToTarget(addr, strip->solo ? 1 : 0);
            }
          }
        }
        break;
      }
      case CMD_CLEAR_SOLO: {
        for (int i = 0; i < NUM_CHANNELS; ++i) st.ch[i].solo = false;
        for (int i = 0; i < NUM_AUX; ++i) st.aux[i].solo = false;
        for (int i = 0; i < NUM_BUSES; ++i) st.bus[i].solo = false;
        for (int i = 0; i < NUM_DCAS; ++i) st.dca[i].solo = false;
        st.mainLr.solo = false;
        mixerMarkDirty(DIRTY_HEADER | DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_SCRIBBLE);
        if (sendOsc) {
          sendIntToTarget("/-stat/clearsolo", 1);
        }
        break;
      }
      case CMD_SET_DCA_MASK: {
        if (strip && (cmd.kind == STRIP_CH || cmd.kind == STRIP_AUX)) {
          strip->dcaMask = static_cast<uint8_t>(cmd.u32val & 0xFFu);
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_MATRIX);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/grp/dca", prefix);
            sendIntToTarget(addr, strip->dcaMask);
          }
        }
        break;
      }
      case CMD_SET_MUTE_MASK: {
        if (strip && (cmd.kind == STRIP_CH || cmd.kind == STRIP_AUX)) {
          strip->muteMask = static_cast<uint8_t>(cmd.u32val & 0x3Fu);
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_MATRIX);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/grp/mute", prefix);
            sendIntToTarget(addr, strip->muteMask);
          }
        }
        break;
      }
      case CMD_SET_MUTE_GRP: {
        if (cmd.idx >= 0 && cmd.idx < NUM_MUTE_GRPS) {
          st.muteGroupEngaged[cmd.idx] = cmd.bval;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_FADERS | DIRTY_MATRIX);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/config/mute/%d", cmd.idx + 1);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_CH_LINK: {
        if (cmd.idx >= 0 && cmd.idx < (NUM_CHANNELS / 2)) {
          st.chLinks[cmd.idx] = cmd.bval;
          mixerMarkDirty(DIRTY_MATRIX | DIRTY_FADERS | DIRTY_CONFIG);
          if (sendOsc) {
            int first = cmd.idx * 2 + 1;
            std::snprintf(addr, sizeof(addr), "/config/chlink/%d-%d", first, first + 1);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_BUS_LINK: {
        if (cmd.idx >= 0 && cmd.idx < (NUM_BUSES / 2)) {
          st.busLinks[cmd.idx] = cmd.bval;
          mixerMarkDirty(DIRTY_MATRIX);
          if (sendOsc) {
            int first = cmd.idx * 2 + 1;
            std::snprintf(addr, sizeof(addr), "/config/buslink/%d-%d", first, first + 1);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_SEND_LEVEL: {
        if (cmd.idx >= 0 && cmd.idx < NUM_CHANNELS && cmd.bus >= 0 && cmd.bus < NUM_BUSES) {
          float f = cmd.fval;
          if (f < 0.0f) f = 0.0f;
          if (f > 1.0f) f = 1.0f;
          st.chSendLevel[cmd.idx][cmd.bus] = f;
          mixerMarkDirty(DIRTY_FADERS);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/ch/%02d/mix/%02d/level", cmd.idx + 1, cmd.bus + 1);
            sendFloatToTarget(addr, f);
          }
        }
        break;
      }
      case CMD_SET_SEND_ON: {
        if (cmd.idx >= 0 && cmd.idx < NUM_CHANNELS && cmd.bus >= 0 && cmd.bus < NUM_BUSES) {
          st.chSendOn[cmd.idx][cmd.bus] = cmd.bval;
          mixerMarkDirty(DIRTY_FADERS);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/ch/%02d/mix/%02d/on", cmd.idx + 1, cmd.bus + 1);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SELECT_STRIP: {
        if (strip) {
          st.selectedKind = cmd.kind;
          st.selectedIdx = cmd.idx;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG | DIRTY_SCRIBBLE | DIRTY_LIBRARY | DIRTY_HEADER);
          if (sendOsc && cmd.bval) {
            int selidx = strip_to_x32_selidx(cmd.kind, cmd.idx);
            if (selidx >= 0) {
              sendIntToTarget("/-stat/selidx", selidx);
            }
          }
        }
        break;
      }
      case CMD_SET_ACTIVE_BUS_SEND: {
        int b = cmd.bus;
        if (b < -1 || b >= NUM_BUSES) b = -1;
        st.activeBusSend = static_cast<int8_t>(b);
        mixerMarkDirty(DIRTY_FADERS);
        if (sendOsc && b >= 0) {
          x32ClientRequestBusSends(b);
        }
        break;
      }
      case CMD_SET_OVERVIEW_MODE: {
        st.overviewQuickMute = cmd.bval;
        mixerMarkDirty(DIRTY_OVERVIEW);
        break;
      }
      case CMD_SET_FOLLOW_SELECT: {
        st.followConsoleSelect = cmd.bval;
        mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_CONFIG);
        break;
      }
      case CMD_RECALL_SCENE: {
        const int16_t sc = (cmd.slot >= 0 && cmd.slot < NUM_SCENES) ? cmd.slot : cmd.idx;
        if (sc >= 0 && sc < NUM_SCENES) {
          st.currentScene = sc;
          if (st.scenes[sc].name[0]) {
            std::snprintf(st.statusMsg, sizeof(st.statusMsg),
                          "Loaded Scene %02d: %.28s", sc, st.scenes[sc].name);
          } else {
            std::snprintf(st.statusMsg, sizeof(st.statusMsg), "Loaded Scene %02d", sc);
          }
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_HEADER | DIRTY_LIBRARY);
          if (sendOsc) {
            const size_t n = osc_build_load_scene(s_txBuf, sizeof(s_txBuf), sc);
            sendOscBytes(s_targetIp, s_txBuf, n);
            needFullSync = true;
          }
        }
        break;
      }
      case CMD_SAVE_SCENE: {
        const int16_t sc = (cmd.slot >= 0 && cmd.slot < NUM_SCENES) ? cmd.slot : cmd.idx;
        if (sc >= 0 && sc < NUM_SCENES) {
          if (cmd.text[0] != '\0') {
            std::snprintf(st.scenes[sc].name, sizeof(st.scenes[sc].name), "%.23s", cmd.text);
          } else if (st.scenes[sc].name[0] == '\0') {
            std::snprintf(st.scenes[sc].name, sizeof(st.scenes[sc].name), "Scene %02d", sc);
          }
          std::snprintf(st.scenes[sc].notes, sizeof(st.scenes[sc].notes), "%.35s", cmd.text2);
          st.scenes[sc].hasData = true;
          st.currentScene = sc;
          std::snprintf(st.statusMsg, sizeof(st.statusMsg),
                        "Saved Scene %02d: %.28s", sc, st.scenes[sc].name);
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_HEADER | DIRTY_LIBRARY);
          if (sendOsc) {
            const size_t n = osc_build_save_scene(s_txBuf, sizeof(s_txBuf), sc,
                                                  st.scenes[sc].name, st.scenes[sc].notes);
            sendOscBytes(s_targetIp, s_txBuf, n);
            std::snprintf(addr, sizeof(addr), "/-show/showfile/scene/%03d/name", sc);
            sendQueryToTarget(addr);
            std::snprintf(addr, sizeof(addr), "/-show/showfile/scene/%03d/notes", sc);
            sendQueryToTarget(addr);
            std::snprintf(addr, sizeof(addr), "/-show/showfile/scene/%03d/hasdata", sc);
            sendQueryToTarget(addr);
          }
        }
        break;
      }
      case CMD_SAVE_CH_PRESET: {
        const int16_t slot = cmd.slot;
        const int libCh = stripToLibChanIdx(cmd.kind, cmd.idx);
        if (slot >= 0 && slot < NUM_CH_PRESETS && libCh >= 0) {
          if (cmd.text[0] != '\0') {
            std::snprintf(st.chPresets[slot].name, sizeof(st.chPresets[slot].name), "%.23s", cmd.text);
          } else if (strip && strip->name[0] != '\0') {
            std::snprintf(st.chPresets[slot].name, sizeof(st.chPresets[slot].name), "%.23s", strip->name);
          } else {
            std::snprintf(st.chPresets[slot].name, sizeof(st.chPresets[slot].name), "Preset %02d", slot + 1);
          }
          st.chPresets[slot].hasData = true;
          std::snprintf(st.statusMsg, sizeof(st.statusMsg),
                        "Saved P%02d: %.32s", slot + 1, st.chPresets[slot].name);
          mixerMarkDirty(DIRTY_LIBRARY | DIRTY_HEADER);
          if (sendOsc) {
            const size_t n = osc_build_save_libchan(s_txBuf, sizeof(s_txBuf), slot,
                                                    st.chPresets[slot].name, libCh);
            sendOscBytes(s_targetIp, s_txBuf, n);
            std::snprintf(addr, sizeof(addr), "/-libs/ch/%03d/name", slot + 1);
            sendQueryToTarget(addr);
            std::snprintf(addr, sizeof(addr), "/-libs/ch/%03d/hasdata", slot + 1);
            sendQueryToTarget(addr);
          }
        }
        break;
      }
      case CMD_LOAD_CH_PRESET: {
        const int16_t slot = cmd.slot;
        const int libCh = stripToLibChanIdx(cmd.kind, cmd.idx);
        if (slot >= 0 && slot < NUM_CH_PRESETS && libCh >= 0) {
          const uint8_t scope = cmd.scope & 0x3Fu;
          std::snprintf(st.statusMsg, sizeof(st.statusMsg),
                        "Loaded P%02d (%s) -> strip",
                        slot + 1,
                        st.chPresets[slot].name[0] ? st.chPresets[slot].name : "Preset");
          mixerMarkDirty(DIRTY_LIBRARY | DIRTY_HEADER);
          if (sendOsc) {
            const size_t n = osc_build_load_libchan(s_txBuf, sizeof(s_txBuf), slot, libCh, scope);
            sendOscBytes(s_targetIp, s_txBuf, n);
            querySingleStripParams(cmd.kind, cmd.idx);
            if ((scope & 0x20u) != 0 && cmd.kind == STRIP_CH && st.activeBusSend >= 0) {
              x32ClientRequestBusSends(st.activeBusSend);
            }
          }
        }
        break;
      }
      case CMD_SET_SOURCE: {
        if ((cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) ||
            (cmd.kind == STRIP_AUX && cmd.idx >= 0 && cmd.idx < NUM_AUX)) {
          const uint8_t src = static_cast<uint8_t>(cmd.u32val > 64u ? 0u : cmd.u32val);
          if (cmd.kind == STRIP_CH) st.chCfg[cmd.idx].source = src;
          else st.auxCfg[cmd.idx].source = src;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/config/source", prefix);
            sendIntToTarget(addr, src);
            queryHaIdx = x32_source_to_headamp_idx(src, st.inRouting);
          }
        }
        break;
      }
      case CMD_SET_GAIN: {
        if (cmd.idx >= 0 && cmd.idx < 128) {
          float f = cmd.fval;
          if (f < 0.0f) f = 0.0f;
          if (f > 1.0f) f = 1.0f;
          st.headampGain[cmd.idx] = f;
          mixerMarkDirty(DIRTY_CONFIG);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/headamp/%03d/gain", cmd.idx);
            sendFloatToTarget(addr, f);
          }
        }
        break;
      }
      case CMD_SET_PHANTOM: {
        if (cmd.idx >= 0 && cmd.idx < 128) {
          st.headampPhantom[cmd.idx] = cmd.bval;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/headamp/%03d/phantom", cmd.idx);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_TRIM: {
        if ((cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) ||
            (cmd.kind == STRIP_AUX && cmd.idx >= 0 && cmd.idx < NUM_AUX)) {
          float f = cmd.fval;
          if (f < 0.0f) f = 0.0f;
          if (f > 1.0f) f = 1.0f;
          if (cmd.kind == STRIP_CH) st.chCfg[cmd.idx].trim = f;
          else st.auxCfg[cmd.idx].trim = f;
          mixerMarkDirty(DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/preamp/trim", prefix);
            sendFloatToTarget(addr, f);
          }
        }
        break;
      }
      case CMD_SET_INVERT: {
        if ((cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) ||
            (cmd.kind == STRIP_AUX && cmd.idx >= 0 && cmd.idx < NUM_AUX)) {
          if (cmd.kind == STRIP_CH) st.chCfg[cmd.idx].invert = cmd.bval;
          else st.auxCfg[cmd.idx].invert = cmd.bval;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/preamp/invert", prefix);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_HPON: {
        if (cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) {
          st.chCfg[cmd.idx].hpon = cmd.bval;
          mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_CONFIG);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/ch/%02d/preamp/hpon", cmd.idx + 1);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_HPF: {
        if (cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) {
          float f = cmd.fval;
          if (f < 0.0f) f = 0.0f;
          if (f > 1.0f) f = 1.0f;
          st.chCfg[cmd.idx].hpf = f;
          mixerMarkDirty(DIRTY_CONFIG);
          if (sendOsc) {
            std::snprintf(addr, sizeof(addr), "/ch/%02d/preamp/hpf", cmd.idx + 1);
            sendFloatToTarget(addr, f);
          }
        }
        break;
      }
      case CMD_SET_MAIN_ST: {
        if ((cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) ||
            (cmd.kind == STRIP_AUX && cmd.idx >= 0 && cmd.idx < NUM_AUX)) {
          if (cmd.kind == STRIP_CH) st.chCfg[cmd.idx].mainSt = cmd.bval;
          else st.auxCfg[cmd.idx].mainSt = cmd.bval;
          mixerMarkDirty(DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/mix/st", prefix);
            sendIntToTarget(addr, cmd.bval ? 1 : 0);
          }
        }
        break;
      }
      case CMD_SET_PAN: {
        if ((cmd.kind == STRIP_CH && cmd.idx >= 0 && cmd.idx < NUM_CHANNELS) ||
            (cmd.kind == STRIP_AUX && cmd.idx >= 0 && cmd.idx < NUM_AUX)) {
          float f = cmd.fval;
          if (f < 0.0f) f = 0.0f;
          if (f > 1.0f) f = 1.0f;
          if (cmd.kind == STRIP_CH) st.chCfg[cmd.idx].pan = f;
          else st.auxCfg[cmd.idx].pan = f;
          mixerMarkDirty(DIRTY_CONFIG);
          if (sendOsc) {
            formatStripPrefix(cmd.kind, cmd.idx, prefix, sizeof(prefix));
            std::snprintf(addr, sizeof(addr), "%s/mix/pan", prefix);
            sendFloatToTarget(addr, f);
          }
        }
        break;
      }
      default:
        break;
    }

    mixerStateUnlock();
  }
  if (queryHaIdx >= 0) {
    queryHeadampIfValid(queryHaIdx);
  }
  if (needFullSync) {
    x32ClientStartFullSync();
  }
}

void x32ClientTick() {
  if (WiFi.status() == WL_CONNECTED) {
    ensureUdpBound();
  }

  // Drain incoming UDP OSC packets
  if (s_udpBound) {
    for (int iter = 0; iter < 64; ++iter) {
      int pktSize = s_udp.parsePacket();
      if (pktSize <= 0) break;
      if (pktSize > static_cast<int>(sizeof(s_rxBuf))) {
        s_udp.flush();
        continue;
      }
      int n = s_udp.read(s_rxBuf, sizeof(s_rxBuf));
      if (n > 0) {
        OscPacketView view;
        if (osc_parse_packet(s_rxBuf, static_cast<size_t>(n), &view)) {
          handleIncomingOsc(view, s_udp.remoteIP());
        }
      }
    }
  }

  // Sample now AFTER draining UDP so lastX32RxMs set inside handleIncomingOsc() can never exceed now
  const uint32_t now = millis();

  // Periodic keepalive, offline probes & sync pumping
  if (WiFi.status() == WL_CONNECTED) {
    if (s_hasTarget) {
      bool isConnected = false;
      bool isConnecting = false;
      uint32_t lastRx = 0;
      if (mixerStateLock(20)) {
        const MixerState &st = mixerStateUnsafe();
        isConnected = st.x32Connected;
        isConnecting = st.x32Connecting;
        lastRx = st.lastX32RxMs;
        mixerStateUnlock();
      }

      if (isConnected) {
        if (elapsedMs(now, s_lastXremoteMs, XREMOTE_INTERVAL_MS)) {
          size_t n = osc_build_query(s_txBuf, sizeof(s_txBuf), "/xremote");
          sendOscBytes(s_targetIp, s_txBuf, n);
          s_lastXremoteMs = now;
        }
        if (elapsedMs(now, s_lastMetersMs, METERS_INTERVAL_MS)) {
          size_t n = osc_build_meters_req(s_txBuf, sizeof(s_txBuf), "/meters/1");
          sendOscBytes(s_targetIp, s_txBuf, n);
          s_lastMetersMs = now;
        }

        if (elapsedMs(now, lastRx, CONSOLE_TIMEOUT_MS)) {
          markTargetConsoleOffline("no response for 10s");
          sendQueryToTarget("/xinfo");
          s_lastProbeMs = now;
        } else if (elapsedMs(now, lastRx, SILENCE_PROBE_MS) &&
                   elapsedMs(now, s_lastProbeMs, SILENCE_PROBE_STEP_MS)) {
          sendQueryToTarget("/xinfo");
          s_lastProbeMs = now;
        }

        if (s_fullSyncActive && elapsedMs(now, s_lastSyncTickMs, SYNC_STEP_INTERVAL_MS)) {
          s_lastSyncTickMs = now;
          int totalSteps = 1;
          for (int i = 0; i < SYNC_BATCH_SIZE; ++i) {
            if (!emitSyncStep(s_syncStep, &totalSteps)) {
              s_syncPass++;
              if (s_syncPass < SYNC_TOTAL_PASSES) {
                s_syncStep = 0;
              } else {
                s_fullSyncActive = false;
              }
              break;
            }
            s_syncStep++;
          }
          const int doneSteps = s_syncPass * totalSteps + (s_fullSyncActive ? s_syncStep : 0);
          int pct = (doneSteps * 100) / (totalSteps * SYNC_TOTAL_PASSES);
          if (pct > 100 || !s_fullSyncActive) pct = 100;
          if (mixerStateLock(20)) {
            MixerState &st = mixerStateUnsafe();
            const uint8_t newPct = static_cast<uint8_t>(pct);
            if (st.syncProgress != newPct) {
              st.syncProgress = newPct;
              mixerMarkDirty(DIRTY_NETWORK);
            }
            mixerStateUnlock();
          }
        }

        if (s_busSendSyncActive && elapsedMs(now, s_lastSyncTickMs, SYNC_STEP_INTERVAL_MS)) {
          s_lastSyncTickMs = now;
          for (int i = 0; i < 2; ++i) {
            if (s_busSendSyncCh >= NUM_CHANNELS) {
              s_busSendSyncActive = false;
              break;
            }
            char addr[48];
            std::snprintf(addr, sizeof(addr), "/ch/%02d/mix/%02d/level",
                          s_busSendSyncCh + 1, s_busSendSyncTarget + 1);
            sendQueryToTarget(addr);
            std::snprintf(addr, sizeof(addr), "/ch/%02d/mix/%02d/on",
                          s_busSendSyncCh + 1, s_busSendSyncTarget + 1);
            sendQueryToTarget(addr);
            s_busSendSyncCh++;
          }
        }
      } else {
        if (isConnecting && elapsedMs(now, s_connectAttemptStartMs, CONNECTING_BADGE_MS)) {
          if (mixerStateLock(20)) {
            MixerState &st = mixerStateUnsafe();
            if (st.x32Connecting) {
              st.x32Connecting = false;
              std::snprintf(st.statusMsg, sizeof(st.statusMsg),
                            "Waiting for X32 at %s...", st.x32Ip);
              mixerMarkDirty(DIRTY_HEADER | DIRTY_NETWORK);
            }
            mixerStateUnlock();
          }
        }

        const uint32_t probeInterval = (s_fastRetriesLeft > 0) ? 400u : OFFLINE_PROBE_INTERVAL_MS;
        if (elapsedMs(now, s_lastProbeMs, probeInterval)) {
          if (s_fastRetriesLeft > 0) --s_fastRetriesLeft;
          s_lastProbeMs = now;
          sendQueryToTarget("/xinfo");
        }
        if (elapsedMs(now, s_lastDiscoverMs, OFFLINE_DISCOVER_INTERVAL_MS)) {
          sendDiscoveryBroadcast(false);
        }
      }
    } else if (s_lastDiscoverMs == 0 || elapsedMs(now, s_lastDiscoverMs, OFFLINE_DISCOVER_INTERVAL_MS)) {
      sendDiscoveryBroadcast(s_lastDiscoverMs == 0);
    }
  }

  screenPowerTick(now);
}
