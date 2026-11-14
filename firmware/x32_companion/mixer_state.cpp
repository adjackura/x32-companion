#include "mixer_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

MixerState g_state;
SemaphoreHandle_t g_stateMutex = nullptr;
QueueHandle_t g_cmdQueue = nullptr;
uint32_t g_uiDirty = DIRTY_ALL;
uint32_t g_wsDirty = DIRTY_ALL;

void initStripDefaults(StripState &s, const char *name, uint8_t color, uint8_t icon, float fader = 0.0f) {
  std::memset(&s, 0, sizeof(s));
  std::snprintf(s.name, sizeof(s.name), "%s", name ? name : "");
  s.color = color & 0x0Fu;
  s.icon = icon ? icon : 1;
  s.fader = fader;
  s.muted = false;
  s.solo = false;
}

const char *kindToStr(StripKind k) {
  switch (k) {
    case STRIP_CH: return "ch";
    case STRIP_AUX: return "aux";
    case STRIP_BUS: return "bus";
    case STRIP_DCA: return "dca";
    case STRIP_MAIN: return "main";
    default: return "ch";
  }
}

StripKind strToKind(const char *s) {
  if (!s) return STRIP_CH;
  if (std::strcmp(s, "aux") == 0) return STRIP_AUX;
  if (std::strcmp(s, "bus") == 0) return STRIP_BUS;
  if (std::strcmp(s, "dca") == 0) return STRIP_DCA;
  if (std::strcmp(s, "main") == 0) return STRIP_MAIN;
  return STRIP_CH;
}

}  // namespace

static void initBlankState(MixerState &st) {
  const float zeroGainF = x32_db_to_gain(0.0f);
  const float defaultHpfF = x32_hz_to_hpf(80.0f);
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    char nm[16];
    std::snprintf(nm, sizeof(nm), "Ch %02d", i + 1);
    initStripDefaults(st.ch[i], nm, 0, 1, 0.0f);
    st.chCfg[i].source = static_cast<uint8_t>(i + 1);
    st.chCfg[i].trim = 0.5f;
    st.chCfg[i].invert = false;
    st.chCfg[i].hpon = false;
    st.chCfg[i].hpf = defaultHpfF;
    st.chCfg[i].mainSt = true;
    st.chCfg[i].pan = 0.5f;
  }
  for (int i = 0; i < NUM_AUX; ++i) {
    char nm[16];
    std::snprintf(nm, sizeof(nm), "Aux %d", i + 1);
    initStripDefaults(st.aux[i], nm, 0, 1, 0.0f);
    st.auxCfg[i].source = static_cast<uint8_t>(33 + i);
    st.auxCfg[i].trim = 0.5f;
    st.auxCfg[i].invert = false;
    st.auxCfg[i].mainSt = true;
    st.auxCfg[i].pan = 0.5f;
  }
  for (int i = 0; i < NUM_BUSES; ++i) {
    char nm[16];
    std::snprintf(nm, sizeof(nm), "Bus %02d", i + 1);
    initStripDefaults(st.bus[i], nm, 0, 1, 0.0f);
  }
  for (int i = 0; i < NUM_DCAS; ++i) {
    char nm[16];
    std::snprintf(nm, sizeof(nm), "DCA %d", i + 1);
    initStripDefaults(st.dca[i], nm, 0, 70, 0.0f);
  }
  initStripDefaults(st.mainLr, "MAIN LR", 7, 64, 0.0f);

  st.inRouting[0] = 0;
  st.inRouting[1] = 1;
  st.inRouting[2] = 2;
  st.inRouting[3] = 3;
  st.inRouting[4] = 0;
  st.haFlags = 0;
  for (int i = 0; i < 128; ++i) {
    st.headampGain[i] = zeroGainF;
    st.headampPhantom[i] = false;
  }

  for (int c = 0; c < NUM_CHANNELS; ++c) {
    for (int b = 0; b < NUM_BUSES; ++b) {
      st.chSendLevel[c][b] = 0.0f;
      st.chSendOn[c][b] = false;
    }
  }
  std::memset(st.muteGroupEngaged, 0, sizeof(st.muteGroupEngaged));
  std::memset(st.chLinks, 0, sizeof(st.chLinks));
  std::memset(st.busLinks, 0, sizeof(st.busLinks));
  for (int s = 0; s < NUM_SCENES; ++s) {
    st.scenes[s].name[0] = '\0';
    st.scenes[s].notes[0] = '\0';
    st.scenes[s].hasData = false;
  }
  st.currentScene = 0;
  for (int p = 0; p < NUM_CH_PRESETS; ++p) {
    st.chPresets[p].name[0] = '\0';
    st.chPresets[p].hasData = false;
  }
}

void mixerStateInit() {
  if (!g_stateMutex) {
    g_stateMutex = xSemaphoreCreateMutex();
  }
  if (!g_cmdQueue) {
    g_cmdQueue = xQueueCreate(64, sizeof(AppCommand));
  }
  if (mixerStateLock(1000)) {
    std::memset(&g_state, 0, sizeof(g_state));
    initBlankState(g_state);
    g_state.selectedKind = STRIP_CH;
    g_state.selectedIdx = 0;
    g_state.followConsoleSelect = true;
    g_state.overviewQuickMute = false;
    g_state.activeBusSend = -1;
    g_state.backlightOn = true;
    g_state.screenTimeoutSec = DEFAULT_SCREEN_TIMEOUT_SEC;
    std::snprintf(g_state.apIp, sizeof(g_state.apIp), "192.168.4.1");
    std::snprintf(g_state.statusMsg, sizeof(g_state.statusMsg), "No X32 connected (Connect Wi-Fi / X32)");
    g_state.wifiScanDone = false;
    g_state.wifiStatusMsg[0] = '\0';
    g_state.otaPhase = OTA_IDLE;
    g_state.otaProgressPct = 0;
    g_state.otaLatestVer[0] = '\0';
    std::snprintf(g_state.otaStatusMsg, sizeof(g_state.otaStatusMsg), "Manual GitHub OTA ready");
    g_state.stateRevision = 1;
    g_uiDirty = DIRTY_ALL;
    g_wsDirty = DIRTY_ALL;
    mixerStateUnlock();
  }
}

bool mixerStateLock(uint32_t timeoutMs) {
  if (!g_stateMutex) return false;
  return xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void mixerStateUnlock() {
  if (g_stateMutex) {
    xSemaphoreGive(g_stateMutex);
  }
}

MixerState &mixerStateUnsafe() {
  return g_state;
}

void mixerMarkDirty(uint32_t flags) {
  g_uiDirty |= flags;
  g_wsDirty |= flags;
  if (flags != DIRTY_METERS) {
    g_state.stateRevision++;
  }
}

void mixerMarkUiOnlyDirty(uint32_t flags) {
  g_uiDirty |= flags;
}

uint32_t mixerTakeUiDirtyFlags() {
  uint32_t flags = 0;
  if (mixerStateLock(20)) {
    flags = g_uiDirty;
    g_uiDirty = DIRTY_NONE;
    mixerStateUnlock();
  }
  return flags;
}

uint32_t mixerTakeWsDirtyFlags() {
  uint32_t flags = 0;
  if (mixerStateLock(20)) {
    flags = g_wsDirty;
    g_wsDirty = DIRTY_NONE;
    mixerStateUnlock();
  }
  return flags;
}

void mixerRestoreWsDirtyFlags(uint32_t flags) {
  if (flags == DIRTY_NONE) return;
  if (mixerStateLock(20)) {
    g_wsDirty |= flags;
    mixerStateUnlock();
  }
}

bool enqueueAppCommand(const AppCommand &cmd) {
  if (!g_cmdQueue) return false;
  return xQueueSend(g_cmdQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool dequeueAppCommand(AppCommand *out, uint32_t timeoutMs) {
  if (!g_cmdQueue || !out) return false;
  return xQueueReceive(g_cmdQueue, out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool x32_selidx_to_strip(int selidx, StripKind *outKind, int *outIdx) {
  if (!outKind || !outIdx) return false;
  if (selidx >= 0 && selidx <= 31) {
    *outKind = STRIP_CH;
    *outIdx = selidx;
    return true;
  }
  if (selidx >= 32 && selidx <= 39) {
    *outKind = STRIP_AUX;
    *outIdx = selidx - 32;
    return true;
  }
  if (selidx >= 48 && selidx <= 63) {
    *outKind = STRIP_BUS;
    *outIdx = selidx - 48;
    return true;
  }
  if (selidx == 70) {
    *outKind = STRIP_MAIN;
    *outIdx = 0;
    return true;
  }
  return false;
}

int strip_to_x32_selidx(StripKind kind, int idx) {
  switch (kind) {
    case STRIP_CH: return (idx >= 0 && idx < NUM_CHANNELS) ? idx : -1;
    case STRIP_AUX: return (idx >= 0 && idx < NUM_AUX) ? (32 + idx) : -1;
    case STRIP_BUS: return (idx >= 0 && idx < NUM_BUSES) ? (48 + idx) : -1;
    case STRIP_MAIN: return 70;
    case STRIP_DCA:
    default: return -1;
  }
}

int strip_kind_count(StripKind kind) {
  switch (kind) {
    case STRIP_CH: return NUM_CHANNELS;
    case STRIP_AUX: return NUM_AUX;
    case STRIP_BUS: return NUM_BUSES;
    case STRIP_DCA: return NUM_DCAS;
    case STRIP_MAIN: return 1;
    default: return 0;
  }
}

const StripState *mixerGetStripConst(const MixerState &st, StripKind kind, int idx) {
  switch (kind) {
    case STRIP_CH: return (idx >= 0 && idx < NUM_CHANNELS) ? &st.ch[idx] : nullptr;
    case STRIP_AUX: return (idx >= 0 && idx < NUM_AUX) ? &st.aux[idx] : nullptr;
    case STRIP_BUS: return (idx >= 0 && idx < NUM_BUSES) ? &st.bus[idx] : nullptr;
    case STRIP_DCA: return (idx >= 0 && idx < NUM_DCAS) ? &st.dca[idx] : nullptr;
    case STRIP_MAIN: return (idx == 0) ? &st.mainLr : nullptr;
    default: return nullptr;
  }
}

StripState *mixerGetStripMutable(MixerState &st, StripKind kind, int idx) {
  switch (kind) {
    case STRIP_CH: return (idx >= 0 && idx < NUM_CHANNELS) ? &st.ch[idx] : nullptr;
    case STRIP_AUX: return (idx >= 0 && idx < NUM_AUX) ? &st.aux[idx] : nullptr;
    case STRIP_BUS: return (idx >= 0 && idx < NUM_BUSES) ? &st.bus[idx] : nullptr;
    case STRIP_DCA: return (idx >= 0 && idx < NUM_DCAS) ? &st.dca[idx] : nullptr;
    case STRIP_MAIN: return (idx == 0) ? &st.mainLr : nullptr;
    default: return nullptr;
  }
}

void mixerFormatStripTag(StripKind kind, int idx, char *buf, size_t len) {
  if (!buf || len == 0) return;
  switch (kind) {
    case STRIP_CH: std::snprintf(buf, len, "CH %02d", idx + 1); break;
    case STRIP_AUX: std::snprintf(buf, len, "AUX %02d", idx + 1); break;
    case STRIP_BUS: std::snprintf(buf, len, "BUS %02d", idx + 1); break;
    case STRIP_DCA: std::snprintf(buf, len, "DCA %d", idx + 1); break;
    case STRIP_MAIN: std::snprintf(buf, len, "MAIN LR"); break;
    default: std::snprintf(buf, len, "STRIP"); break;
  }
}

bool mixerAnySoloActive(const MixerState &st) {
  for (int i = 0; i < NUM_CHANNELS; ++i) if (st.ch[i].solo) return true;
  for (int i = 0; i < NUM_AUX; ++i) if (st.aux[i].solo) return true;
  for (int i = 0; i < NUM_BUSES; ++i) if (st.bus[i].solo) return true;
  for (int i = 0; i < NUM_DCAS; ++i) if (st.dca[i].solo) return true;
  if (st.mainLr.solo) return true;
  return false;
}

static bool isGroupMutedInternal(const MixerState &st, const StripState *s, StripKind kind) {
  if (!s || (kind != STRIP_CH && kind != STRIP_AUX)) return false;
  for (int m = 0; m < NUM_MUTE_GRPS; ++m) {
    if (st.muteGroupEngaged[m] && ((s->muteMask & (1u << m)) != 0)) return true;
  }
  for (int d = 0; d < NUM_DCAS; ++d) {
    if (st.dca[d].muted && ((s->dcaMask & (1u << d)) != 0)) return true;
  }
  return false;
}

bool mixerIsStripEffectivelyMuted(const MixerState &st, StripKind kind, int idx) {
  const StripState *s = mixerGetStripConst(st, kind, idx);
  if (!s) return false;
  if (s->muted) return true;
  return isGroupMutedInternal(st, s, kind);
}

bool mixerIsStripGroupMutedOnly(const MixerState &st, StripKind kind, int idx) {
  const StripState *s = mixerGetStripConst(st, kind, idx);
  if (!s || s->muted) return false;
  return isGroupMutedInternal(st, s, kind);
}

void mixerClearMetersLocked(MixerState &st) {
  auto clearArr = [](StripState *arr, int n) {
    for (int i = 0; i < n; ++i) {
      arr[i].meterPct = 0;
      arr[i].clip = false;
    }
  };
  clearArr(st.ch, NUM_CHANNELS);
  clearArr(st.aux, NUM_AUX);
  clearArr(st.bus, NUM_BUSES);
  clearArr(st.dca, NUM_DCAS);
  st.mainLr.meterPct = 0;
  st.mainLr.clip = false;
}

namespace {

void writeStripArray(JsonArray arr,
                     const StripState *strips,
                     int count,
                     const ChConfigState *chCfg = nullptr,
                     const AuxConfigState *auxCfg = nullptr) {
  for (int i = 0; i < count; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["n"] = strips[i].name;
    o["c"] = strips[i].color;
    o["ic"] = strips[i].icon;
    o["f"] = serialized(String(strips[i].fader, 4));
    o["m"] = strips[i].muted ? 1 : 0;
    o["s"] = strips[i].solo ? 1 : 0;
    o["dm"] = strips[i].dcaMask;
    o["mm"] = strips[i].muteMask;
    o["mt"] = strips[i].meterPct;
    if (chCfg) {
      o["sr"] = chCfg[i].source;
      o["tr"] = serialized(String(chCfg[i].trim, 4));
      o["iv"] = chCfg[i].invert ? 1 : 0;
      o["ho"] = chCfg[i].hpon ? 1 : 0;
      o["hf"] = serialized(String(chCfg[i].hpf, 4));
      o["st"] = chCfg[i].mainSt ? 1 : 0;
      o["pn"] = serialized(String(chCfg[i].pan, 3));
    } else if (auxCfg) {
      o["sr"] = auxCfg[i].source;
      o["tr"] = serialized(String(auxCfg[i].trim, 4));
      o["iv"] = auxCfg[i].invert ? 1 : 0;
      o["st"] = auxCfg[i].mainSt ? 1 : 0;
      o["pn"] = serialized(String(auxCfg[i].pan, 3));
    }
  }
}

}  // namespace

size_t mixerBuildFullStateJson(char *outBuf, size_t cap) {
  if (!outBuf || cap < 64) return 0;
  JsonDocument doc;
  if (!mixerStateLock(50)) return 0;
  const MixerState &st = g_state;

  doc["type"] = "state";
  doc["rev"] = st.stateRevision;
  doc["selKind"] = kindToStr(st.selectedKind);
  doc["selIdx"] = st.selectedIdx;
  doc["follow"] = st.followConsoleSelect;
  doc["quickMute"] = st.overviewQuickMute;
  doc["activeBus"] = st.activeBusSend;
  doc["anySolo"] = mixerAnySoloActive(st);
  doc["curScene"] = st.currentScene;

  JsonObject net = doc["net"].to<JsonObject>();
  net["wifiConn"] = st.wifiConnected;
  net["apActive"] = st.apActive;
  net["rssi"] = st.wifiRssi;
  net["staIp"] = st.staIp;
  net["apIp"] = st.apIp;
  net["x32Conn"] = st.x32Connected;
  net["x32Connecting"] = st.x32Connecting;
  net["x32Ip"] = st.x32Ip;
  net["x32Name"] = st.x32Name;
  net["x32Model"] = st.x32Model;
  net["x32Fw"] = st.x32Firmware;
  net["syncPct"] = st.syncProgress;
  net["wsClients"] = st.wsClientCount;
  net["lcdOn"] = st.backlightOn;
  net["idleOffSec"] = st.screenTimeoutSec;
  net["msg"] = st.statusMsg;

  writeStripArray(doc["ch"].to<JsonArray>(), st.ch, NUM_CHANNELS, st.chCfg, nullptr);
  writeStripArray(doc["aux"].to<JsonArray>(), st.aux, NUM_AUX, nullptr, st.auxCfg);
  writeStripArray(doc["bus"].to<JsonArray>(), st.bus, NUM_BUSES);
  writeStripArray(doc["dca"].to<JsonArray>(), st.dca, NUM_DCAS);

  JsonObject mainObj = doc["main"].to<JsonObject>();
  mainObj["n"] = st.mainLr.name;
  mainObj["c"] = st.mainLr.color;
  mainObj["ic"] = st.mainLr.icon;
  mainObj["f"] = serialized(String(st.mainLr.fader, 4));
  mainObj["m"] = st.mainLr.muted ? 1 : 0;
  mainObj["s"] = st.mainLr.solo ? 1 : 0;
  mainObj["mt"] = st.mainLr.meterPct;

  JsonArray mg = doc["mg"].to<JsonArray>();
  for (int i = 0; i < NUM_MUTE_GRPS; ++i) mg.add(st.muteGroupEngaged[i] ? 1 : 0);

  JsonArray cl = doc["chLink"].to<JsonArray>();
  for (int i = 0; i < NUM_CHANNELS / 2; ++i) cl.add(st.chLinks[i] ? 1 : 0);

  JsonArray bl = doc["busLink"].to<JsonArray>();
  for (int i = 0; i < NUM_BUSES / 2; ++i) bl.add(st.busLinks[i] ? 1 : 0);

  JsonArray inRt = doc["inRt"].to<JsonArray>();
  for (int i = 0; i < 5; ++i) inRt.add(st.inRouting[i]);
  doc["haFl"] = st.haFlags;

  JsonArray haG = doc["haG"].to<JsonArray>();
  for (int i = 0; i < 128; ++i) {
    haG.add(serialized(String(st.headampGain[i], 3)));
  }
  uint32_t phWords[4] = {0, 0, 0, 0};
  for (int i = 0; i < 128; ++i) {
    if (st.headampPhantom[i]) {
      phWords[i >> 5] |= (uint32_t(1u) << (i & 31));
    }
  }
  JsonArray haPh = doc["haPh"].to<JsonArray>();
  for (int w = 0; w < 4; ++w) haPh.add(phWords[w]);

  JsonArray scArr = doc["scenes"].to<JsonArray>();
  for (int i = 0; i < NUM_SCENES; ++i) {
    JsonObject o = scArr.add<JsonObject>();
    o["n"] = st.scenes[i].name;
    o["nt"] = st.scenes[i].notes;
    o["h"] = st.scenes[i].hasData ? 1 : 0;
  }

  JsonArray prArr = doc["presets"].to<JsonArray>();
  for (int i = 0; i < NUM_CH_PRESETS; ++i) {
    JsonObject o = prArr.add<JsonObject>();
    o["n"] = st.chPresets[i].name;
    o["h"] = st.chPresets[i].hasData ? 1 : 0;
  }

  if (st.activeBusSend >= 0 && st.activeBusSend < NUM_BUSES) {
    const int b = st.activeBusSend;
    JsonArray sf = doc["sendsF"].to<JsonArray>();
    JsonArray so = doc["sendsOn"].to<JsonArray>();
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      sf.add(serialized(String(st.chSendLevel[i][b], 4)));
      so.add(st.chSendOn[i][b] ? 1 : 0);
    }
  }

  mixerStateUnlock();
  return serializeJson(doc, outBuf, cap);
}

size_t mixerBuildMetersJson(char *outBuf, size_t cap) {
  if (!outBuf || cap < 64) return 0;
  JsonDocument doc;
  if (!mixerStateLock(20)) return 0;
  const MixerState &st = g_state;
  doc["type"] = "meters";
  JsonArray chM = doc["ch"].to<JsonArray>();
  for (int i = 0; i < NUM_CHANNELS; ++i) chM.add(st.ch[i].meterPct);
  JsonArray busM = doc["bus"].to<JsonArray>();
  for (int i = 0; i < NUM_BUSES; ++i) busM.add(st.bus[i].meterPct);
  doc["main"] = st.mainLr.meterPct;
  mixerStateUnlock();
  return serializeJson(doc, outBuf, cap);
}

bool mixerParseJsonCommand(const char *jsonStr, size_t len, AppCommand *outCmd) {
  if (!jsonStr || !outCmd || len == 0) return false;
  JsonDocument doc;
  if (deserializeJson(doc, jsonStr, len) != DeserializationError::Ok) return false;

  const char *cmd = doc["cmd"] | "";
  *outCmd = AppCommand{};
  outCmd->kind = strToKind(doc["kind"] | "ch");
  outCmd->idx = doc["idx"] | 0;
  outCmd->bus = doc["bus"] | -1;

  if (std::strcmp(cmd, "setScribble") == 0) {
    outCmd->type = CMD_SET_SCRIBBLE;
    std::snprintf(outCmd->text, sizeof(outCmd->text), "%s", doc["name"] | "");
    outCmd->color = static_cast<uint8_t>(doc["color"] | 0);
    outCmd->icon = static_cast<uint8_t>(doc["icon"] | 1);
  } else if (std::strcmp(cmd, "setName") == 0) {
    outCmd->type = CMD_SET_NAME;
    std::snprintf(outCmd->text, sizeof(outCmd->text), "%s", doc["name"] | "");
  } else if (std::strcmp(cmd, "setColor") == 0) {
    outCmd->type = CMD_SET_COLOR;
    outCmd->color = static_cast<uint8_t>(doc["color"] | 0);
  } else if (std::strcmp(cmd, "setIcon") == 0) {
    outCmd->type = CMD_SET_ICON;
    outCmd->icon = static_cast<uint8_t>(doc["icon"] | 1);
  } else if (std::strcmp(cmd, "setFader") == 0) {
    outCmd->type = CMD_SET_FADER;
    outCmd->fval = doc["val"] | 0.0f;
  } else if (std::strcmp(cmd, "setMute") == 0) {
    outCmd->type = CMD_SET_MUTE;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setSolo") == 0) {
    outCmd->type = CMD_SET_SOLO;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "clearSolo") == 0) {
    outCmd->type = CMD_CLEAR_SOLO;
  } else if (std::strcmp(cmd, "setDcaMask") == 0) {
    outCmd->type = CMD_SET_DCA_MASK;
    outCmd->u32val = doc["mask"] | 0u;
  } else if (std::strcmp(cmd, "setMuteMask") == 0) {
    outCmd->type = CMD_SET_MUTE_MASK;
    outCmd->u32val = doc["mask"] | 0u;
  } else if (std::strcmp(cmd, "setMuteGrp") == 0) {
    outCmd->type = CMD_SET_MUTE_GRP;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setChLink") == 0) {
    outCmd->type = CMD_SET_CH_LINK;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setBusLink") == 0) {
    outCmd->type = CMD_SET_BUS_LINK;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setSendLevel") == 0) {
    outCmd->type = CMD_SET_SEND_LEVEL;
    outCmd->fval = doc["val"] | 0.0f;
  } else if (std::strcmp(cmd, "setSendOn") == 0) {
    outCmd->type = CMD_SET_SEND_ON;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "selectStrip") == 0) {
    outCmd->type = CMD_SELECT_STRIP;
    outCmd->bval = (doc["toX32"] | 0) != 0;
  } else if (std::strcmp(cmd, "setActiveBus") == 0) {
    outCmd->type = CMD_SET_ACTIVE_BUS_SEND;
    outCmd->bus = doc["bus"] | -1;
  } else if (std::strcmp(cmd, "setQuickMute") == 0) {
    outCmd->type = CMD_SET_OVERVIEW_MODE;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setFollow") == 0) {
    outCmd->type = CMD_SET_FOLLOW_SELECT;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "x32Sync") == 0) {
    outCmd->type = CMD_X32_SYNC_ALL;
  } else if (std::strcmp(cmd, "recallScene") == 0) {
    outCmd->type = CMD_RECALL_SCENE;
    outCmd->slot = doc["slot"] | outCmd->idx;
  } else if (std::strcmp(cmd, "saveScene") == 0) {
    outCmd->type = CMD_SAVE_SCENE;
    outCmd->slot = doc["slot"] | outCmd->idx;
    std::snprintf(outCmd->text, sizeof(outCmd->text), "%s", doc["name"] | "");
    std::snprintf(outCmd->text2, sizeof(outCmd->text2), "%s", doc["notes"] | "");
  } else if (std::strcmp(cmd, "savePreset") == 0) {
    outCmd->type = CMD_SAVE_CH_PRESET;
    outCmd->slot = doc["slot"] | 0;
    std::snprintf(outCmd->text, sizeof(outCmd->text), "%s", doc["name"] | "");
  } else if (std::strcmp(cmd, "loadPreset") == 0) {
    outCmd->type = CMD_LOAD_CH_PRESET;
    outCmd->slot = doc["slot"] | 0;
    outCmd->scope = static_cast<uint8_t>(doc["scope"] | 0x3Eu);
  } else if (std::strcmp(cmd, "setSource") == 0) {
    outCmd->type = CMD_SET_SOURCE;
    const int v = doc["val"] | 0;
    outCmd->u32val = static_cast<uint32_t>(v < 0 ? 0 : (v > 64 ? 64 : v));
  } else if (std::strcmp(cmd, "setGain") == 0) {
    outCmd->type = CMD_SET_GAIN;
    float v = doc["val"] | 0.0f;
    outCmd->fval = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  } else if (std::strcmp(cmd, "setPhantom") == 0) {
    outCmd->type = CMD_SET_PHANTOM;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setTrim") == 0) {
    outCmd->type = CMD_SET_TRIM;
    float v = doc["val"] | 0.5f;
    outCmd->fval = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  } else if (std::strcmp(cmd, "setInvert") == 0) {
    outCmd->type = CMD_SET_INVERT;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setHpon") == 0) {
    outCmd->type = CMD_SET_HPON;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setHpf") == 0) {
    outCmd->type = CMD_SET_HPF;
    float v = doc["val"] | 0.0f;
    outCmd->fval = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  } else if (std::strcmp(cmd, "setMainSt") == 0) {
    outCmd->type = CMD_SET_MAIN_ST;
    outCmd->bval = (doc["val"] | 0) != 0;
  } else if (std::strcmp(cmd, "setPan") == 0) {
    outCmd->type = CMD_SET_PAN;
    float v = doc["val"] | 0.5f;
    outCmd->fval = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  } else {
    return false;
  }

  return true;
}
