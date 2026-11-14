#pragma once

#include <cstddef>
#include <cstdint>
#include "osc_codec.h"

static constexpr int NUM_CHANNELS = 32;
static constexpr int NUM_AUX = 8;
static constexpr int NUM_BUSES = 16;
static constexpr int NUM_DCAS = 8;
static constexpr int NUM_MUTE_GRPS = 6;
static constexpr int NUM_SCENES = 32;
static constexpr int NUM_CH_PRESETS = 32;
static constexpr int MAX_WIFI_SCAN = 20;
static constexpr int MAX_DISCOVERED_X32 = 6;
static constexpr uint16_t DEFAULT_SCREEN_TIMEOUT_SEC = 60;

enum StripKind : uint8_t {
  STRIP_CH = 0,
  STRIP_AUX = 1,
  STRIP_BUS = 2,
  STRIP_DCA = 3,
  STRIP_MAIN = 4
};

struct StripState {
  char name[16];
  uint8_t color;
  uint8_t icon;
  float fader;
  bool muted;  // Unified UI polarity: true == muted
  bool solo;
  uint8_t dcaMask;
  uint8_t muteMask;
  uint8_t meterPct;
  bool clip;
};

struct ChConfigState {
  uint8_t source;
  float trim;
  bool invert;
  bool hpon;
  float hpf;
  bool mainSt;
  float pan;
};

struct AuxConfigState {
  uint8_t source;
  float trim;
  bool invert;
  bool mainSt;
  float pan;
};

struct WifiScanEntry {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

struct DiscoveredConsole {
  char ip[20];
  char name[32];
  char model[24];
  char firmware[24];
  uint32_t lastSeenMs;
};

struct SceneSummary {
  char name[24];
  char notes[36];
  bool hasData;
};

struct ChPresetSummary {
  char name[24];
  bool hasData;
};

struct MixerState {
  StripState ch[NUM_CHANNELS];
  StripState aux[NUM_AUX];
  StripState bus[NUM_BUSES];
  StripState dca[NUM_DCAS];
  StripState mainLr;

  float chSendLevel[NUM_CHANNELS][NUM_BUSES];
  bool chSendOn[NUM_CHANNELS][NUM_BUSES];

  bool muteGroupEngaged[NUM_MUTE_GRPS];
  bool chLinks[NUM_CHANNELS / 2];
  bool busLinks[NUM_BUSES / 2];

  ChConfigState chCfg[NUM_CHANNELS];
  AuxConfigState auxCfg[NUM_AUX];
  uint8_t inRouting[5];  // /config/routing/IN/{1-8,9-16,17-24,25-32,AUX}
  uint8_t haFlags;       // /-prefs/haflags bitmask
  float headampGain[128];
  bool headampPhantom[128];

  SceneSummary scenes[NUM_SCENES];
  int16_t currentScene;
  ChPresetSummary chPresets[NUM_CH_PRESETS];

  StripKind selectedKind;
  int16_t selectedIdx;
  bool followConsoleSelect;
  bool overviewQuickMute;
  int8_t activeBusSend;  // -1 = Main LR, 0..15 = Bus 1..16

  // Network & X32 session status
  bool wifiConnected;
  bool wifiConnecting;
  bool wifiScanning;
  bool apActive;
  int8_t wifiRssi;
  char wifiSsid[33];
  char savedWifiSsid[33];
  char staIp[20];
  char apIp[20];

  bool x32Connected;
  bool x32Connecting;
  char x32Ip[20];
  char x32Name[32];
  char x32Model[24];
  char x32Firmware[24];
  uint8_t syncProgress;
  uint32_t lastX32RxMs;
  uint8_t wsClientCount;
  bool backlightOn;
  uint16_t screenTimeoutSec;
  char webPasskey[8];
  char statusMsg[64];

  uint8_t otaPhase;
  uint8_t otaProgressPct;
  char otaLatestVer[24];
  char otaStatusMsg[64];

  WifiScanEntry wifiScan[MAX_WIFI_SCAN];
  uint8_t wifiScanCount;
  bool wifiScanDone;
  char wifiStatusMsg[64];

  DiscoveredConsole consoles[MAX_DISCOVERED_X32];
  uint8_t consoleCount;

  uint32_t stateRevision;
};

enum OtaPhase : uint8_t {
  OTA_IDLE = 0,
  OTA_CHECKING = 1,
  OTA_UP_TO_DATE = 2,
  OTA_AVAILABLE = 3,
  OTA_DOWNLOADING = 4,
  OTA_VERIFYING = 5,
  OTA_REBOOTING = 6,
  OTA_ERROR = 7
};

enum DirtyFlag : uint32_t {
  DIRTY_NONE = 0,
  DIRTY_HEADER = (1u << 0),
  DIRTY_OVERVIEW = (1u << 1),
  DIRTY_SCRIBBLE = (1u << 2),
  DIRTY_MATRIX = (1u << 3),
  DIRTY_FADERS = (1u << 4),
  DIRTY_NETWORK = (1u << 5),
  DIRTY_METERS = (1u << 6),
  DIRTY_LIBRARY = (1u << 7),
  DIRTY_CONFIG = (1u << 8),
  DIRTY_ALL = 0x1FFu
};

enum AppCmdType : uint8_t {
  CMD_NONE = 0,
  CMD_SET_SCRIBBLE,
  CMD_SET_NAME,
  CMD_SET_COLOR,
  CMD_SET_ICON,
  CMD_SET_FADER,
  CMD_SET_MUTE,
  CMD_SET_SOLO,
  CMD_CLEAR_SOLO,
  CMD_SET_DCA_MASK,
  CMD_SET_MUTE_MASK,
  CMD_SET_MUTE_GRP,
  CMD_SET_CH_LINK,
  CMD_SET_BUS_LINK,
  CMD_SET_SEND_LEVEL,
  CMD_SET_SEND_ON,
  CMD_SELECT_STRIP,
  CMD_SET_ACTIVE_BUS_SEND,
  CMD_SET_OVERVIEW_MODE,
  CMD_SET_FOLLOW_SELECT,
  CMD_SET_BACKLIGHT,
  CMD_SET_SCREEN_TIMEOUT,
  CMD_REGEN_WEB_PASSKEY,
  CMD_REVOKE_WEB_SESSIONS,
  CMD_WIFI_SCAN,
  CMD_WIFI_CONNECT,
  CMD_X32_DISCOVER,
  CMD_X32_CONNECT,
  CMD_X32_SYNC_ALL,
  CMD_RECALL_SCENE,
  CMD_SAVE_SCENE,
  CMD_SAVE_CH_PRESET,
  CMD_LOAD_CH_PRESET,
  CMD_SET_SOURCE,
  CMD_SET_GAIN,
  CMD_SET_PHANTOM,
  CMD_SET_TRIM,
  CMD_SET_INVERT,
  CMD_SET_HPON,
  CMD_SET_HPF,
  CMD_SET_MAIN_ST,
  CMD_SET_PAN,
  CMD_OTA_CHECK,
  CMD_OTA_INSTALL
};

struct AppCommand {
  AppCmdType type = CMD_NONE;
  StripKind kind = STRIP_CH;
  int16_t idx = 0;
  int16_t slot = 0;
  int8_t bus = -1;
  uint8_t color = 0;
  uint8_t icon = 1;
  uint8_t scope = 0x3Eu;  // libchan recall scope mask (bits 0..5 = HA, Cfg, Gate, Dyn, EQ, Sends)
  uint32_t u32val = 0;
  float fval = 0.0f;
  bool bval = false;
  char text[64] = {0};
  char text2[64] = {0};
};

// Global state lifecycle and locking
void mixerStateInit();
bool mixerStateLock(uint32_t timeoutMs = 50);
void mixerStateUnlock();
MixerState &mixerStateUnsafe();

// Dirty tracking for Core 1 LVGL and Core 0 WebSocket broadcaster
void mixerMarkDirty(uint32_t flags);
void mixerMarkUiOnlyDirty(uint32_t flags);
uint32_t mixerTakeUiDirtyFlags();
uint32_t mixerTakeWsDirtyFlags();
void mixerRestoreWsDirtyFlags(uint32_t flags);

// Command queue shared by LVGL (Core 1) and WebUI HTTP/WS (Core 0)
bool enqueueAppCommand(const AppCommand &cmd);
bool dequeueAppCommand(AppCommand *out, uint32_t timeoutMs = 0);

// Strip & selidx helpers
bool x32_selidx_to_strip(int selidx, StripKind *outKind, int *outIdx);
int strip_to_x32_selidx(StripKind kind, int idx);
int strip_kind_count(StripKind kind);
const StripState *mixerGetStripConst(const MixerState &st, StripKind kind, int idx);
StripState *mixerGetStripMutable(MixerState &st, StripKind kind, int idx);
void mixerFormatStripTag(StripKind kind, int idx, char *buf, size_t len);
bool mixerAnySoloActive(const MixerState &st);
bool mixerIsStripEffectivelyMuted(const MixerState &st, StripKind kind, int idx);
bool mixerIsStripGroupMutedOnly(const MixerState &st, StripKind kind, int idx);
void mixerClearMetersLocked(MixerState &st);

// JSON serializers for Web UI (caller supplies buffer, typically PSRAM)
size_t mixerBuildFullStateJson(char *outBuf, size_t cap);
size_t mixerBuildMetersJson(char *outBuf, size_t cap);
bool mixerParseJsonCommand(const char *jsonStr, size_t len, AppCommand *outCmd);
