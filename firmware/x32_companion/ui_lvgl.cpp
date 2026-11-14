#include "ui_lvgl.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "mixer_state.h"
#include "osc_codec.h"
#include "ota_updater.h"
#include "x32_icons.h"

namespace {

enum UiTabId : uint8_t {
  TAB_OVERVIEW = 0,
  TAB_CONFIG = 1,
  TAB_SCRIBBLE = 2,
  TAB_MATRIX = 3,
  TAB_FADERS = 4,
  TAB_LIBRARY = 5,
  TAB_NETWORK = 6,
  TAB_COUNT = 7
};

enum CfgCat : uint8_t {
  CFG_CH1 = 0,
  CFG_CH2 = 1,
  CFG_AUX = 2
};

enum ScCategory : uint8_t {
  SC_CH1 = 0,
  SC_CH2 = 1,
  SC_DCA = 2,
  SC_BUS = 3,
  SC_AUX = 4,
  SC_MAIN = 5
};

enum MxMode : uint8_t {
  MX_DCA = 0,
  MX_MUTE = 1,
  MX_LINK = 2
};

enum FaderBankId : uint8_t {
  FB_CH1 = 0,
  FB_CH2 = 1,
  FB_CH3 = 2,
  FB_CH4 = 3,
  FB_AUX = 4,
  FB_BUS1 = 5,
  FB_BUS2 = 6,
  FB_DCA = 7
};

enum NetKbTarget : uint8_t {
  NET_KB_SSID = 0,
  NET_KB_PASS = 1,
  NET_KB_X32IP = 2
};

// Theme palette
static constexpr uint32_t COL_BG = 0x0D1117;
static constexpr uint32_t COL_PANEL = 0x151B23;
static constexpr uint32_t COL_PANEL2 = 0x1E2633;
static constexpr uint32_t COL_BORDER = 0x2F3B4D;
static constexpr uint32_t COL_ACCENT = 0x2F81F7;
static constexpr uint32_t COL_CYAN = 0x39C5CF;
static constexpr uint32_t COL_GREEN = 0x2EA043;
static constexpr uint32_t COL_RED = 0xDA3633;
static constexpr uint32_t COL_GRP_MUTE = 0x7A2E0E;
static constexpr uint32_t COL_AMBER = 0xD29922;
static constexpr uint32_t COL_SOLO = 0xE3B341;
static constexpr uint32_t COL_TEXT = 0xE6EDF3;
static constexpr uint32_t COL_MUTED = 0x8B949E;

UiTabId s_activeTab = TAB_OVERVIEW;
CfgCat s_cfgCat = CFG_CH1;
ScCategory s_scCategory = SC_CH1;
MxMode s_mxMode = MX_DCA;
uint8_t s_mxBank = 0;  // 0..3 Ch banks, 4 Aux
FaderBankId s_faderBank = FB_CH1;

lv_obj_t *s_tabBtns[TAB_COUNT] = {};
lv_obj_t *s_tabViews[TAB_COUNT] = {};

// Header widgets
lv_obj_t *s_connPill = nullptr;
lv_obj_t *s_connLbl = nullptr;
lv_obj_t *s_clearSoloBtn = nullptr;
lv_obj_t *s_netLbl = nullptr;

// Tab 1: Overview widgets
lv_obj_t *s_ovModeSw = nullptr;
lv_obj_t *s_ovModeLbl = nullptr;
lv_obj_t *s_ovStatusLbl = nullptr;
lv_obj_t *s_ovTile[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileHead[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileNum[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileSrc[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileName[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileVu[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileMute[NUM_CHANNELS] = {};
lv_obj_t *s_ovTileSolo[NUM_CHANNELS] = {};

lv_obj_t *s_selFollowSw = nullptr;
lv_obj_t *s_selBanner = nullptr;
lv_obj_t *s_selTagLbl = nullptr;
lv_obj_t *s_selDbLbl = nullptr;
lv_obj_t *s_selNameLbl = nullptr;
lv_obj_t *s_selSlider = nullptr;
lv_obj_t *s_selMuteBtn = nullptr;
lv_obj_t *s_selSoloBtn = nullptr;
lv_obj_t *s_selCfgBtn = nullptr;

lv_obj_t *s_ovDcaBtn[NUM_DCAS] = {};
lv_obj_t *s_ovDcaDot[NUM_DCAS] = {};
lv_obj_t *s_ovDcaLbl[NUM_DCAS] = {};
lv_obj_t *s_ovMgBtn[NUM_MUTE_GRPS] = {};

lv_obj_t *s_ovMainDbLbl = nullptr;
lv_obj_t *s_ovMainVu = nullptr;
lv_obj_t *s_ovMainSlider = nullptr;
lv_obj_t *s_ovMainMuteBtn = nullptr;

// Tab 2: Scribble & Name widgets
lv_obj_t *s_scCatBtns[6] = {};
lv_obj_t *s_scFollowSw = nullptr;
lv_obj_t *s_scStripRow = nullptr;
lv_obj_t *s_scPickBtn[16] = {};
lv_obj_t *s_scPickLbl[16] = {};
lv_obj_t *s_scPrevBox = nullptr;
lv_obj_t *s_scPrevTag = nullptr;
lv_obj_t *s_scPrevCode = nullptr;
lv_obj_t *s_scPrevName = nullptr;
lv_obj_t *s_scPrevIconBtn = nullptr;
lv_obj_t *s_scPrevIconLbl = nullptr;
lv_obj_t *s_scIconOpenBtn = nullptr;
lv_obj_t *s_scNameTa = nullptr;
lv_obj_t *s_scSwatchBtn[8] = {};
lv_obj_t *s_scInvertBtn = nullptr;
lv_obj_t *s_scInvertLbl = nullptr;
lv_obj_t *s_scKeyboard = nullptr;

// Visual Categorized 74-Icon Picker Overlay (Tab 2)
static constexpr int ICON_GRID_SLOTS = 21;  // 7 cols x 3 rows
lv_obj_t *s_scIconModal = nullptr;
lv_obj_t *s_scIconCatBtns[6] = {};
lv_obj_t *s_scIconPageLbl = nullptr;
lv_obj_t *s_scIconPrevBtn = nullptr;
lv_obj_t *s_scIconNextBtn = nullptr;
lv_obj_t *s_scIconTiles[ICON_GRID_SLOTS] = {};
lv_obj_t *s_scIconTileGlyph[ICON_GRID_SLOTS] = {};
lv_obj_t *s_scIconTileLbl[ICON_GRID_SLOTS] = {};
uint8_t s_scIconTileId[ICON_GRID_SLOTS] = {};
uint8_t s_scIconCat = 0;   // 0=All, 1=Drums, 2=Gtr/Bass, 3=Keys/Inst, 4=Vox/Mic, 5=I/O & PA
uint8_t s_scIconPage = 0;
uint8_t s_scPrevIconVal = 1;

StripKind s_scLastKind = STRIP_CH;
int16_t s_scLastIdx = -1;
bool s_scSyncingTa = false;
uint32_t s_scLastTypeMs = 0;

// Tab 3: Matrix widgets
lv_obj_t *s_mxModeBtns[3] = {};
lv_obj_t *s_mxBankBtns[5] = {};
lv_obj_t *s_mxMatrixPanel = nullptr;
lv_obj_t *s_mxColHdrBtn[8] = {};
lv_obj_t *s_mxColHdrLbl[8] = {};
lv_obj_t *s_mxRowLbl[8] = {};
lv_obj_t *s_mxCellBtn[8][8] = {};
lv_obj_t *s_mxCellLbl[8][8] = {};
lv_obj_t *s_mxLinksPanel = nullptr;
lv_obj_t *s_mxLinkBtn[24] = {};
lv_obj_t *s_mxLinkLbl[24] = {};

// Tab 4: Faders & Sends widgets
lv_obj_t *s_fbBtns[8] = {};
lv_obj_t *s_busDropdown = nullptr;
lv_obj_t *s_exitSofBtn = nullptr;
lv_obj_t *s_fStripBox[9] = {};
lv_obj_t *s_fCapBtn[9] = {};
lv_obj_t *s_fCapTag[9] = {};
lv_obj_t *s_fCapName[9] = {};
lv_obj_t *s_fDbLbl[9] = {};
lv_obj_t *s_fVuBar[9] = {};
lv_obj_t *s_fSlider[9] = {};
lv_obj_t *s_fSoloBtn[9] = {};
lv_obj_t *s_fMuteBtn[9] = {};

// Tab 5: Wi-Fi & X32 widgets
lv_obj_t *s_netWifiStatusLbl = nullptr;
lv_obj_t *s_netScanBtn = nullptr;
lv_obj_t *s_netScanLbl = nullptr;
lv_obj_t *s_netWifiList = nullptr;
lv_obj_t *s_netSsidTa = nullptr;
lv_obj_t *s_netPassTa = nullptr;
lv_obj_t *s_netX32StatusLbl = nullptr;
lv_obj_t *s_netConsoleList = nullptr;
lv_obj_t *s_netX32IpTa = nullptr;
lv_obj_t *s_netBlBtn = nullptr;
lv_obj_t *s_netBlLbl = nullptr;
lv_obj_t *s_netIdleBox = nullptr;
lv_obj_t *s_netIdleLbl = nullptr;
lv_obj_t *s_netPasskeyBox = nullptr;
lv_obj_t *s_netPasskeyLbl = nullptr;
lv_obj_t *s_netWebUrlLbl = nullptr;
lv_obj_t *s_netOtaBox = nullptr;
lv_obj_t *s_netOtaVerLbl = nullptr;
lv_obj_t *s_netOtaStatusLbl = nullptr;
lv_obj_t *s_netOtaCheckBtn = nullptr;
lv_obj_t *s_netOtaCheckLbl = nullptr;
lv_obj_t *s_netOtaInstallBtn = nullptr;
lv_obj_t *s_netOtaInstallLbl = nullptr;
lv_obj_t *s_netOtaBar = nullptr;
bool s_otaInstallArmed = false;
uint32_t s_otaInstallArmUntilMs = 0;

// Network Tab Fullscreen Keyboard Sheet
lv_obj_t *s_netKbModal = nullptr;
lv_obj_t *s_netKbTitleLbl = nullptr;
lv_obj_t *s_netModalTa = nullptr;
lv_obj_t *s_netModalShowBtn = nullptr;
lv_obj_t *s_netModalShowLbl = nullptr;
lv_obj_t *s_netModalIp1Btn = nullptr;
lv_obj_t *s_netModalIp2Btn = nullptr;
lv_obj_t *s_netModalModeBtn = nullptr;
lv_obj_t *s_netModalModeLbl = nullptr;
lv_obj_t *s_netModalCommitBtn = nullptr;
lv_obj_t *s_netModalCommitLbl = nullptr;
lv_obj_t *s_netKb = nullptr;
NetKbTarget s_netKbTarget = NET_KB_SSID;
bool s_netKbNumMode = false;

// Tab 5: Presets & Scenes (TAB_LIBRARY) widgets
enum LibMode : uint8_t {
  LIB_SCENES = 0,
  LIB_PRESETS = 1
};
LibMode s_libMode = LIB_SCENES;
uint8_t s_libBank = 0;
int16_t s_libSelSlot = 0;
uint8_t s_libScope = 0x3Eu;  // Cfg + Gate + Dyn + EQ + Sends (HA off by default)
bool s_libLoadArmed = false;
bool s_libKbNumMode = false;

lv_obj_t *s_libModeBtns[2] = {};
lv_obj_t *s_libBankBtns[4] = {};
lv_obj_t *s_libInfoLbl = nullptr;
lv_obj_t *s_libCard[8] = {};
lv_obj_t *s_libCardNum[8] = {};
lv_obj_t *s_libCardName[8] = {};
lv_obj_t *s_libCardSub[8] = {};
lv_obj_t *s_libSelTitleLbl = nullptr;
lv_obj_t *s_libSelSubLbl = nullptr;
lv_obj_t *s_libTargetBar = nullptr;
lv_obj_t *s_libTargetLbl = nullptr;
lv_obj_t *s_libScopeBar = nullptr;
lv_obj_t *s_libScopeBtns[6] = {};
lv_obj_t *s_libLoadBtn = nullptr;
lv_obj_t *s_libLoadLbl = nullptr;
lv_obj_t *s_libSaveBtn = nullptr;
lv_obj_t *s_libSaveLbl = nullptr;

lv_obj_t *s_libKbModal = nullptr;
lv_obj_t *s_libKbTitleLbl = nullptr;
lv_obj_t *s_libModalNameTa = nullptr;
lv_obj_t *s_libModalNotesTa = nullptr;
lv_obj_t *s_libModalModeLbl = nullptr;
lv_obj_t *s_libActiveTa = nullptr;
lv_obj_t *s_libKb = nullptr;

// Tab 1: Channel Configuration (TAB_CONFIG) state & widgets
uint8_t s_cfgSrcTab = 0;  // 0=In 1-16, 1=In 17-32, 2=Aux/USB/FX, 3=Bus 1-16
bool s_cfgForceTrim = false;
bool s_cfgPhantomArmed = false;
uint32_t s_cfgPhantomArmMs = 0;
int16_t s_cfgPhantomArmHa = -1;
StripKind s_cfgLastKind = STRIP_CH;
int s_cfgLastIdx = 0;

lv_obj_t *s_cfgCatBtns[3] = {};
lv_obj_t *s_cfgBanner = nullptr;
lv_obj_t *s_cfgTagLbl = nullptr;
lv_obj_t *s_cfgNameLbl = nullptr;
lv_obj_t *s_cfgFollowSw = nullptr;
lv_obj_t *s_cfgPickBtn[16] = {};
lv_obj_t *s_cfgPickLbl[16] = {};
lv_obj_t *s_cfgSrcTabBtns[4] = {};
lv_obj_t *s_cfgSrcSubLbl = nullptr;
lv_obj_t *s_cfgSrcGridBtns[16] = {};
lv_obj_t *s_cfgSrcGridLbls[16] = {};
lv_obj_t *s_cfgSrcOffBtn = nullptr;
lv_obj_t *s_cfgSrcDefBtn = nullptr;
lv_obj_t *s_cfgWarnLbl = nullptr;
lv_obj_t *s_cfgPreTitleLbl = nullptr;
lv_obj_t *s_cfgGainDbLbl = nullptr;
lv_obj_t *s_cfgGainSlider = nullptr;
lv_obj_t *s_cfgPhantomBtn = nullptr;
lv_obj_t *s_cfgPhantomLbl = nullptr;
lv_obj_t *s_cfgPolBtn = nullptr;
lv_obj_t *s_cfgPolLbl = nullptr;
lv_obj_t *s_cfgModeTrimBtn = nullptr;
lv_obj_t *s_cfgModeTrimLbl = nullptr;
lv_obj_t *s_cfgHpfBox = nullptr;
lv_obj_t *s_cfgHpfEmptyLbl = nullptr;
lv_obj_t *s_cfgHpfOnBtn = nullptr;
lv_obj_t *s_cfgHpfOnLbl = nullptr;
lv_obj_t *s_cfgHpfFreqLbl = nullptr;
lv_obj_t *s_cfgHpfSlider = nullptr;
lv_obj_t *s_cfgHpfPreBtns[4] = {};
lv_obj_t *s_cfgPanValLbl = nullptr;
lv_obj_t *s_cfgPanSlider = nullptr;
lv_obj_t *s_cfgMainStBtn = nullptr;
lv_obj_t *s_cfgMainStLbl = nullptr;
lv_obj_t *s_cfgLinkBtn = nullptr;
lv_obj_t *s_cfgLinkLbl = nullptr;

// ============================================================
// Shadow Caches to avoid redundant LVGL invalidations
// ============================================================

struct HdrCache {
  bool init = false;
  uint8_t mode = 255;
  char x32Name[32] = {0};
  bool anySolo = false;
  bool wifiConn = false;
  bool apActive = false;
  char netIp[20] = {0};
  int8_t rssi = 0;
} s_hdrCache;

struct OvTileCache {
  bool init = false;
  bool isSel = false;
  uint8_t color = 255;
  uint8_t src = 255;
  char name[16] = {0};
  uint8_t meterPct = 255;
  bool effMuted = false;
  bool grpMutedOnly = false;
  bool solo = false;
} s_ovTileCache[NUM_CHANNELS];

struct OvPanelCache {
  bool init = false;
  char statusMsg[64] = {0};
  bool quickMute = false;
  bool follow = false;
  StripKind selKind = STRIP_CH;
  int16_t selIdx = -1;
  uint8_t selColor = 255;
  char selTagText[36] = {0};
  char selName[16] = {0};
  char selDb[16] = {0};
  bool selMuted = false;
  bool selSolo = false;
  int32_t selFaderInt = -1;
  uint8_t dcaCol[NUM_DCAS] = {};
  bool dcaMuted[NUM_DCAS] = {};
  char dcaName[NUM_DCAS][16] = {};
  bool mg[NUM_MUTE_GRPS] = {};
  char mainDb[16] = {0};
  uint8_t mainVu = 255;
  int32_t mainFaderInt = -1;
  bool mainMuted = false;
} s_ovPanelCache;

struct CfgCache {
  bool init = false;
  bool follow = false;
  CfgCat cat = CFG_CH1;
  StripKind kind = STRIP_CH;
  int16_t idx = -1;
  uint8_t color = 255;
  char name[16] = {0};
  uint8_t pickCol[16] = {};
  char pickName[16][16] = {};
  uint8_t pickSrc[16] = {};
  bool pickVis[16] = {};
  uint8_t srcTab = 255;
  uint8_t source = 255;
  uint8_t inRt[5] = {};
  uint8_t haFlags = 0;
  uint32_t srcShareHash = 0;
  int16_t haIdx = -2;
  bool useHa = false;
  int32_t gainStep = -1;
  bool phantom = false;
  bool phantomArmed = false;
  bool invert = false;
  bool hpon = false;
  int32_t hpfInt = -1;
  int32_t panPct = -999;
  bool mainSt = false;
  bool chLink = false;
} s_cfgCache;

struct ScCache {
  bool init = false;
  bool follow = false;
  ScCategory cat = SC_CH1;
  StripKind selKind = STRIP_CH;
  int16_t selIdx = -1;
  bool btnVis[16] = {};
  uint8_t btnCol[16] = {};
  bool btnSel[16] = {};
  char btnName[16][16] = {};
  uint8_t prevColor = 255;
  uint8_t prevIcon = 255;
  char prevName[16] = {0};
} s_scCache;

struct MxCache {
  bool init = false;
  MxMode mode = MX_DCA;
  uint8_t bank = 255;
  uint8_t dcaCol[NUM_DCAS] = {};
  char dcaName[NUM_DCAS][16] = {};
  bool mg[NUM_MUTE_GRPS] = {};
  char rowName[8][16] = {};
  uint8_t cellMask[8] = {};
  bool chLinks[NUM_CHANNELS / 2] = {};
  bool busLinks[NUM_BUSES / 2] = {};
} s_mxCache;

struct FaderSlotCache {
  bool init = false;
  StripKind kind = STRIP_CH;
  int idx = -1;
  bool isSend = false;
  uint8_t color = 255;
  char name[16] = {0};
  char db[16] = {0};
  uint8_t meterPct = 255;
  int32_t faderInt = -1;
  bool effMuted = false;
  bool grpMutedOnly = false;
  bool solo = false;
} s_fSlotCache[9];

struct NetCache {
  bool init = false;
  uint32_t wifiHash = 0;
  uint32_t consoleHash = 0;
  char wifiStatus[96] = {0};
  char x32Status[96] = {0};
  char savedSsid[33] = {0};
  char x32Ip[20] = {0};
  bool backlightOn = true;
  uint16_t screenTimeoutSec = 65535;
  char webPasskey[8] = {0};
  char webUrlText[128] = {0};
  bool wifiConnected = false;
  bool wifiScanning = false;
  uint32_t wifiStatusCol = 0;
  uint8_t otaPhase = OTA_IDLE;
  uint8_t otaProgressPct = 0;
  char otaLatestVer[24] = {0};
  char otaStatusMsg[64] = {0};
  bool otaInstallArmed = false;
} s_netCache;

struct LibCache {
  bool init = false;
  uint8_t mode = 255;
  uint8_t bank = 255;
  int16_t selSlot = -1;
  int16_t curScene = -1;
  uint8_t scope = 255;
  bool armed = false;
  StripKind selKind = STRIP_CH;
  int16_t selIdx = -1;
  uint8_t selStripCol = 255;
  char selStripName[16] = {0};
  char slotName[8][24] = {};
  char slotSub[8][36] = {};
  bool slotHas[8] = {};
} s_libCache;

// ============================================================
// UI Styling & Diff Helpers
// ============================================================

void placeTopLeft(lv_obj_t *obj, int x, int y, int w, int h) {
  lv_obj_set_align(obj, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
}

void drawX32VectorIcon(lv_draw_ctx_t *drawCtx, const lv_area_t &box, uint8_t iconId, lv_color_t col) {
  if (!drawCtx || iconId < 1 || iconId > 74) return;
  const lv_coord_t bw = lv_area_get_width(&box);
  const lv_coord_t bh = lv_area_get_height(&box);
  if (bw < 12 || bh < 12) return;

  if (iconId == 1) {
    // None (subtle badge outline + slash)
    const lv_coord_t side = ((bw < bh ? bw : bh) >= 64) ? 44 : ((bw < bh ? bw : bh) - 8);
    const lv_coord_t ox = box.x1 + (bw - side) / 2;
    const lv_coord_t oy = box.y1 + (bh - side) / 2;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.border_color = col;
    rd.border_width = 1;
    rd.border_opa = LV_OPA_50;
    rd.bg_opa = LV_OPA_TRANSP;
    rd.radius = 6;
    lv_area_t r = {ox, oy, static_cast<lv_coord_t>(ox + side - 1), static_cast<lv_coord_t>(oy + side - 1)};
    lv_draw_rect(drawCtx, &rd, &r);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = col;
    ld.opa = LV_OPA_50;
    ld.width = 1;
    ld.round_start = 1;
    ld.round_end = 1;
    const lv_coord_t inset = side / 4;
    lv_point_t p1 = {static_cast<lv_coord_t>(ox + inset), static_cast<lv_coord_t>(oy + side - 1 - inset)};
    lv_point_t p2 = {static_cast<lv_coord_t>(ox + side - 1 - inset), static_cast<lv_coord_t>(oy + inset)};
    lv_draw_line(drawCtx, &ld, &p1, &p2);
    return;
  }

  const uint8_t *bmp = kX32IconBitmaps[iconId - 2];
  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.radius = 0;
  rd.border_width = 0;
  rd.bg_opa = LV_OPA_COVER;
  rd.bg_color = col;

  if (bw >= 64 && bh >= 64) {
    const lv_coord_t ox = box.x1 + (bw - 64) / 2;
    const lv_coord_t oy = box.y1 + (bh - 64) / 2;
    for (int y = 0; y < 64; ++y) {
      const uint8_t *row = bmp + (y * 8);
      int runStart = -1;
      for (int x = 0; x < 64; ++x) {
        const bool on = (row[x >> 3] & (0x80u >> (x & 7))) != 0;
        if (on && runStart < 0) {
          runStart = x;
        } else if (!on && runStart >= 0) {
          lv_area_t span = {
              static_cast<lv_coord_t>(ox + runStart),
              static_cast<lv_coord_t>(oy + y),
              static_cast<lv_coord_t>(ox + x - 1),
              static_cast<lv_coord_t>(oy + y),
          };
          lv_draw_rect(drawCtx, &rd, &span);
          runStart = -1;
        }
      }
      if (runStart >= 0) {
        lv_area_t span = {
            static_cast<lv_coord_t>(ox + runStart),
            static_cast<lv_coord_t>(oy + y),
            static_cast<lv_coord_t>(ox + 63),
            static_cast<lv_coord_t>(oy + y),
        };
        lv_draw_rect(drawCtx, &rd, &span);
      }
    }
    return;
  }

  // Scaled crisp max-pooled 1-bit rendering for smaller preview containers (e.g., 52x52 -> 48x48)
  const lv_coord_t side = (bw < bh ? bw : bh) - 4;
  if (side < 8) return;
  const lv_coord_t ox = box.x1 + (bw - side) / 2;
  const lv_coord_t oy = box.y1 + (bh - side) / 2;
  for (int dy = 0; dy < side; ++dy) {
    const int sy0 = (dy * 64) / side;
    int sy1 = ((dy + 1) * 64) / side;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > 64) sy1 = 64;

    int runStart = -1;
    for (int dx = 0; dx < side; ++dx) {
      const int sx0 = (dx * 64) / side;
      int sx1 = ((dx + 1) * 64) / side;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      if (sx1 > 64) sx1 = 64;

      bool on = false;
      for (int sy = sy0; sy < sy1 && !on; ++sy) {
        const uint8_t *row = bmp + (sy * 8);
        for (int sx = sx0; sx < sx1; ++sx) {
          if (row[sx >> 3] & (0x80u >> (sx & 7))) {
            on = true;
            break;
          }
        }
      }

      if (on && runStart < 0) {
        runStart = dx;
      } else if (!on && runStart >= 0) {
        lv_area_t span = {
            static_cast<lv_coord_t>(ox + runStart),
            static_cast<lv_coord_t>(oy + dy),
            static_cast<lv_coord_t>(ox + dx - 1),
            static_cast<lv_coord_t>(oy + dy),
        };
        lv_draw_rect(drawCtx, &rd, &span);
        runStart = -1;
      }
    }
    if (runStart >= 0) {
      lv_area_t span = {
          static_cast<lv_coord_t>(ox + runStart),
          static_cast<lv_coord_t>(oy + dy),
          static_cast<lv_coord_t>(ox + side - 1),
          static_cast<lv_coord_t>(oy + dy),
      };
      lv_draw_rect(drawCtx, &rd, &span);
    }
  }
}

void onVectorIconDrawPost(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *drawCtx = lv_event_get_draw_ctx(e);
  if (!obj || !drawCtx) return;

  uintptr_t slotTag = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  uint8_t iconId = 1;
  lv_color_t fg = lv_color_hex(COL_TEXT);

  if (slotTag == 0xFFFFu) {
    iconId = s_scPrevIconVal;
    fg = lv_obj_get_style_text_color(s_scPrevName, 0);
  } else if (slotTag < ICON_GRID_SLOTS) {
    iconId = s_scIconTileId[slotTag];
    fg = lv_color_hex((iconId == s_scPrevIconVal) ? COL_CYAN : COL_TEXT);
  }
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  drawX32VectorIcon(drawCtx, coords, iconId, fg);
}

uint32_t fnv1aUpdate(uint32_t h, const void *data, size_t len) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

void setLabelTextIfChanged(lv_obj_t *lbl, const char *txt) {
  if (!lbl || !txt) return;
  const char *cur = lv_label_get_text(lbl);
  if (!cur || std::strcmp(cur, txt) != 0) {
    lv_label_set_text(lbl, txt);
  }
}

void setSwitchStateIfChanged(lv_obj_t *sw, bool checked) {
  if (!sw) return;
  const bool has = lv_obj_has_state(sw, LV_STATE_CHECKED);
  if (checked && !has) lv_obj_add_state(sw, LV_STATE_CHECKED);
  else if (!checked && has) lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

void setVisibleIfChanged(lv_obj_t *obj, bool visible) {
  if (!obj) return;
  const bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  if (visible && hidden) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else if (!visible && !hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void styleKeyboard(lv_obj_t *kb, const lv_font_t *font) {
  lv_obj_set_style_bg_color(kb, lv_color_hex(0x0D1117), 0);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(kb, 3, 0);
  lv_obj_set_style_pad_row(kb, 4, 0);
  lv_obj_set_style_pad_column(kb, 4, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(COL_PANEL2), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, lv_color_hex(COL_ACCENT), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(kb, lv_color_hex(0x28364A), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(kb, lv_color_hex(COL_BORDER), LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_hex(COL_TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, font, LV_PART_ITEMS);
}

lv_obj_t *makeBtn(lv_obj_t *parent, const char *txt, int w, int h, uint32_t bgHex) {
  lv_obj_t *b = lv_btn_create(parent);
  if (w > 0 && h > 0) lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(bgHex), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  if (txt) {
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(l);
  }
  return b;
}

void applyX32BannerStyle(lv_obj_t *box, lv_obj_t *lbl1, lv_obj_t *lbl2, uint8_t colorId) {
  const X32ColorInfo c = x32_get_color_info(colorId);
  lv_obj_set_style_bg_color(box, lv_color_hex(c.bg_rgb), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  if (c.inverted) {
    lv_obj_set_style_border_color(box, lv_color_hex(c.accent_rgb), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_TOP, 0);
  } else {
    lv_obj_set_style_border_width(box, 0, 0);
  }
  if (lbl1) lv_obj_set_style_text_color(lbl1, lv_color_hex(c.fg_rgb), 0);
  if (lbl2) lv_obj_set_style_text_color(lbl2, lv_color_hex(c.fg_rgb), 0);
}

void applyMuteBtnStyle(lv_obj_t *btn, bool muted, bool grpMutedOnly) {
  const uint32_t bg = muted ? COL_RED : (grpMutedOnly ? COL_GRP_MUTE : COL_PANEL2);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  if (lbl) {
    lv_obj_set_style_text_color(lbl, lv_color_hex((muted || grpMutedOnly) ? 0xFFFFFF : COL_TEXT), 0);
  }
}

void applySoloBtnStyle(lv_obj_t *btn, bool solo) {
  lv_obj_set_style_bg_color(btn, lv_color_hex(solo ? COL_SOLO : COL_PANEL2), 0);
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  if (lbl) {
    lv_obj_set_style_text_color(lbl, lv_color_hex(solo ? 0x11151C : COL_TEXT), 0);
  }
}

void focusScTextArea() {
  if (!s_scNameTa) return;
  lv_obj_add_state(s_scNameTa, LV_STATE_FOCUSED);
  lv_textarea_set_cursor_pos(s_scNameTa, LV_TEXTAREA_CURSOR_LAST);
}

void setScTextProgrammatic(const char *txt) {
  if (!s_scNameTa) return;
  s_scSyncingTa = true;
  lv_textarea_set_text(s_scNameTa, txt ? txt : "");
  lv_textarea_set_cursor_pos(s_scNameTa, LV_TEXTAREA_CURSOR_LAST);
  s_scSyncingTa = false;
}

void syncHeader(const MixerState &st);
void syncOverview(const MixerState &st);
void syncConfig(const MixerState &st);
void syncScribble(const MixerState &st);
void syncMatrix(const MixerState &st);
void syncFaders(const MixerState &st);
void syncLibrary(const MixerState &st);
void syncNetwork(const MixerState &st);
void syncMetersOnly(const MixerState &st);

void requestImmediateRefresh() {
  lv_disp_t *disp = lv_disp_get_default();
  if (disp && disp->refr_timer) {
    lv_timer_ready(disp->refr_timer);
  }
}

void attachRepeatCallbacks(lv_obj_t *btn, lv_event_cb_t cb, int dir) {
  void *ud = reinterpret_cast<void *>(static_cast<intptr_t>(dir));
  lv_obj_add_event_cb(btn, cb, LV_EVENT_SHORT_CLICKED, ud);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_LONG_PRESSED, ud);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_LONG_PRESSED_REPEAT, ud);
}

void selectStripOptimistic(StripKind kind, int idx, bool sendToX32) {
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    st.selectedKind = kind;
    st.selectedIdx = static_cast<int16_t>(idx);
    mixerStateUnlock();
  }
  mixerMarkDirty(DIRTY_OVERVIEW | DIRTY_SCRIBBLE | DIRTY_CONFIG | DIRTY_LIBRARY);

  AppCommand cmd;
  cmd.type = CMD_SELECT_STRIP;
  cmd.kind = kind;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.bval = sendToX32;
  enqueueAppCommand(cmd);
}

void syncActiveTabImmediate() {
  uint32_t tabDirty = DIRTY_OVERVIEW;
  switch (s_activeTab) {
    case TAB_OVERVIEW: tabDirty = DIRTY_OVERVIEW | DIRTY_METERS; break;
    case TAB_CONFIG:   tabDirty = DIRTY_CONFIG; break;
    case TAB_SCRIBBLE: tabDirty = DIRTY_SCRIBBLE; break;
    case TAB_MATRIX:   tabDirty = DIRTY_MATRIX; break;
    case TAB_FADERS:   tabDirty = DIRTY_FADERS | DIRTY_METERS; break;
    case TAB_LIBRARY:  tabDirty = DIRTY_LIBRARY; break;
    case TAB_NETWORK:  tabDirty = DIRTY_NETWORK; break;
    default: break;
  }
  if (!mixerStateLock(20)) {
    mixerMarkUiOnlyDirty(tabDirty);
    return;
  }
  const MixerState &st = mixerStateUnsafe();
  switch (s_activeTab) {
    case TAB_OVERVIEW: syncOverview(st); syncMetersOnly(st); break;
    case TAB_CONFIG:   syncConfig(st); break;
    case TAB_SCRIBBLE: syncScribble(st); break;
    case TAB_MATRIX:   syncMatrix(st); break;
    case TAB_FADERS:   syncFaders(st); syncMetersOnly(st); break;
    case TAB_LIBRARY:  syncLibrary(st); break;
    case TAB_NETWORK:  syncNetwork(st); break;
    default: break;
  }
  mixerStateUnlock();
}

void switchTab(UiTabId tab) {
  s_libLoadArmed = false;
  s_cfgPhantomArmed = false;
  s_otaInstallArmed = false;
  if (tab == s_activeTab) {
    if (tab != TAB_NETWORK && s_netKbModal) {
      setVisibleIfChanged(s_netKbModal, false);
    }
    if (tab != TAB_LIBRARY && s_libKbModal) {
      setVisibleIfChanged(s_libKbModal, false);
    }
    if (tab != TAB_SCRIBBLE && s_scIconModal) {
      setVisibleIfChanged(s_scIconModal, false);
    }
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  s_activeTab = tab;
  for (int i = 0; i < TAB_COUNT; ++i) {
    setVisibleIfChanged(s_tabViews[i], i == tab);
    if (s_tabBtns[i]) {
      lv_obj_set_style_bg_color(
          s_tabBtns[i], lv_color_hex(i == tab ? COL_ACCENT : COL_PANEL2), 0);
    }
  }
  if (tab != TAB_NETWORK && s_netKbModal) {
    lv_obj_add_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);
  }
  if (tab != TAB_LIBRARY && s_libKbModal) {
    lv_obj_add_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
  }
  if (tab != TAB_SCRIBBLE && s_scIconModal) {
    lv_obj_add_flag(s_scIconModal, LV_OBJ_FLAG_HIDDEN);
  }
  if (tab == TAB_SCRIBBLE) {
    focusScTextArea();
  }
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void getScCategoryRange(ScCategory cat, StripKind *outKind, int *outStart, int *outCount) {
  switch (cat) {
    case SC_CH1: *outKind = STRIP_CH; *outStart = 0; *outCount = 16; break;
    case SC_CH2: *outKind = STRIP_CH; *outStart = 16; *outCount = 16; break;
    case SC_DCA: *outKind = STRIP_DCA; *outStart = 0; *outCount = 8; break;
    case SC_BUS: *outKind = STRIP_BUS; *outStart = 0; *outCount = 16; break;
    case SC_AUX: *outKind = STRIP_AUX; *outStart = 0; *outCount = 8; break;
    case SC_MAIN:
    default: *outKind = STRIP_MAIN; *outStart = 0; *outCount = 1; break;
  }
}

void getFaderBankRange(FaderBankId fb, StripKind *outKind, int *outStart) {
  switch (fb) {
    case FB_CH1: *outKind = STRIP_CH; *outStart = 0; break;
    case FB_CH2: *outKind = STRIP_CH; *outStart = 8; break;
    case FB_CH3: *outKind = STRIP_CH; *outStart = 16; break;
    case FB_CH4: *outKind = STRIP_CH; *outStart = 24; break;
    case FB_AUX: *outKind = STRIP_AUX; *outStart = 0; break;
    case FB_BUS1: *outKind = STRIP_BUS; *outStart = 0; break;
    case FB_BUS2: *outKind = STRIP_BUS; *outStart = 8; break;
    case FB_DCA:
    default: *outKind = STRIP_DCA; *outStart = 0; break;
  }
}

void getCfgCategoryRange(CfgCat cat, StripKind *outKind, int *outStart, int *outCount) {
  switch (cat) {
    case CFG_CH1: *outKind = STRIP_CH; *outStart = 0; *outCount = 16; break;
    case CFG_CH2: *outKind = STRIP_CH; *outStart = 16; *outCount = 16; break;
    case CFG_AUX:
    default:      *outKind = STRIP_AUX; *outStart = 0; *outCount = NUM_AUX; break;
  }
}

void getActiveConfigTarget(const MixerState &st, StripKind *outKind, int *outIdx) {
  if (st.selectedKind == STRIP_CH && st.selectedIdx >= 0 && st.selectedIdx < NUM_CHANNELS) {
    s_cfgLastKind = STRIP_CH;
    s_cfgLastIdx = st.selectedIdx;
  } else if (st.selectedKind == STRIP_AUX && st.selectedIdx >= 0 && st.selectedIdx < NUM_AUX) {
    s_cfgLastKind = STRIP_AUX;
    s_cfgLastIdx = st.selectedIdx;
  }
  if (s_cfgLastKind == STRIP_AUX) {
    if (s_cfgLastIdx < 0 || s_cfgLastIdx >= NUM_AUX) s_cfgLastIdx = 0;
    s_cfgCat = CFG_AUX;
  } else {
    s_cfgLastKind = STRIP_CH;
    if (s_cfgLastIdx < 0 || s_cfgLastIdx >= NUM_CHANNELS) s_cfgLastIdx = 0;
    s_cfgCat = (s_cfgLastIdx < 16) ? CFG_CH1 : CFG_CH2;
  }
  *outKind = s_cfgLastKind;
  *outIdx = s_cfgLastIdx;
}

void jumpToRenameStrip(StripKind kind, int idx) {
  if (kind == STRIP_CH) s_scCategory = (idx < 16) ? SC_CH1 : SC_CH2;
  else if (kind == STRIP_DCA) s_scCategory = SC_DCA;
  else if (kind == STRIP_BUS) s_scCategory = SC_BUS;
  else if (kind == STRIP_AUX) s_scCategory = SC_AUX;
  else s_scCategory = SC_MAIN;

  selectStripOptimistic(kind, idx, kind != STRIP_DCA);
  s_scLastIdx = -1;
  switchTab(TAB_SCRIBBLE);
}

void jumpToConfigStrip(StripKind kind, int idx) {
  if (kind != STRIP_CH && kind != STRIP_AUX) {
    kind = s_cfgLastKind;
    idx = s_cfgLastIdx;
  }
  if (kind == STRIP_AUX) {
    if (idx < 0 || idx >= NUM_AUX) idx = 0;
    s_cfgCat = CFG_AUX;
  } else {
    kind = STRIP_CH;
    if (idx < 0 || idx >= NUM_CHANNELS) idx = 0;
    s_cfgCat = (idx < 16) ? CFG_CH1 : CFG_CH2;
  }
  s_cfgLastKind = kind;
  s_cfgLastIdx = idx;
  s_cfgPhantomArmed = false;
  selectStripOptimistic(kind, idx, true);
  switchTab(TAB_CONFIG);
}

// ============================================================
// Event Callbacks
// ============================================================

void onTabBtnClick(lv_event_t *e) {
  int idx = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  switchTab(static_cast<UiTabId>(idx));
}

void onConnPillClick(lv_event_t *e) {
  (void)e;
  switchTab(TAB_NETWORK);
}

void onClearSoloClick(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_CLEAR_SOLO;
  enqueueAppCommand(cmd);
}

void onOvModeChanged(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  AppCommand cmd;
  cmd.type = CMD_SET_OVERVIEW_MODE;
  cmd.bval = lv_obj_has_state(sw, LV_STATE_CHECKED);
  enqueueAppCommand(cmd);
}

void onFollowChanged(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  AppCommand cmd;
  cmd.type = CMD_SET_FOLLOW_SELECT;
  cmd.bval = lv_obj_has_state(sw, LV_STATE_CHECKED);
  enqueueAppCommand(cmd);
}

void onOvTileShortClick(lv_event_t *e) {
  int ch = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  bool quickMute = false;
  bool curMuted = false;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    quickMute = st.overviewQuickMute;
    curMuted = st.ch[ch].muted;
    mixerStateUnlock();
  }
  if (quickMute) {
    AppCommand cmd;
    cmd.type = CMD_SET_MUTE;
    cmd.kind = STRIP_CH;
    cmd.idx = ch;
    cmd.bval = !curMuted;
    enqueueAppCommand(cmd);
  } else {
    selectStripOptimistic(STRIP_CH, ch, true);
    syncActiveTabImmediate();
    requestImmediateRefresh();
  }
}

void onOvTileLongPress(lv_event_t *e) {
  int ch = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  jumpToRenameStrip(STRIP_CH, ch);
}

void onSelLevelStep(lv_event_t *e) {
  const int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (dir == 0) return;

  AppCommand cmd;
  float nextF = 0.0f;
  bool haveStrip = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    StripState *s = mixerGetStripMutable(st, st.selectedKind, st.selectedIdx);
    if (s) {
      nextF = x32_step_fader_db(s->fader, dir);
      s->fader = nextF;
      cmd.type = CMD_SET_FADER;
      cmd.kind = st.selectedKind;
      cmd.idx = st.selectedIdx;
      cmd.fval = nextF;
      haveStrip = true;
    }
    mixerStateUnlock();
  }
  if (!haveStrip) return;

  const int32_t fInt = static_cast<int32_t>(nextF * 1000.0f + 0.5f);
  char dbBuf[16];
  x32_format_db(nextF, dbBuf, sizeof(dbBuf));
  s_ovPanelCache.selFaderInt = fInt;
  std::snprintf(s_ovPanelCache.selDb, sizeof(s_ovPanelCache.selDb), "%s", dbBuf);
  lv_slider_set_value(s_selSlider, fInt, LV_ANIM_OFF);
  setLabelTextIfChanged(s_selDbLbl, dbBuf);

  enqueueAppCommand(cmd);
  requestImmediateRefresh();
}

void onSelSliderChanged(lv_event_t *e) {
  lv_obj_t *sl = lv_event_get_target(e);
  float val = lv_slider_get_value(sl) / 1000.0f;
  char dbBuf[16];
  x32_format_db(val, dbBuf, sizeof(dbBuf));
  setLabelTextIfChanged(s_selDbLbl, dbBuf);

  AppCommand cmd;
  if (mixerStateLock(15)) {
    const MixerState &st = mixerStateUnsafe();
    cmd.type = CMD_SET_FADER;
    cmd.kind = st.selectedKind;
    cmd.idx = st.selectedIdx;
    cmd.fval = val;
    mixerStateUnlock();
  }
  if (cmd.type != CMD_NONE) enqueueAppCommand(cmd);
}

void onSelMuteClick(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    const StripState *s = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
    if (s) {
      cmd.type = CMD_SET_MUTE;
      cmd.kind = st.selectedKind;
      cmd.idx = st.selectedIdx;
      cmd.bval = !s->muted;
    }
    mixerStateUnlock();
  }
  if (cmd.type != CMD_NONE) enqueueAppCommand(cmd);
}

void onSelSoloClick(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    const StripState *s = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
    if (s) {
      cmd.type = CMD_SET_SOLO;
      cmd.kind = st.selectedKind;
      cmd.idx = st.selectedIdx;
      cmd.bval = !s->solo;
    }
    mixerStateUnlock();
  }
  if (cmd.type != CMD_NONE) enqueueAppCommand(cmd);
}

void onJumpRenameSelClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    k = mixerStateUnsafe().selectedKind;
    idx = mixerStateUnsafe().selectedIdx;
    mixerStateUnlock();
  }
  jumpToRenameStrip(k, idx);
}

void onJumpConfigSelClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    k = mixerStateUnsafe().selectedKind;
    idx = mixerStateUnsafe().selectedIdx;
    mixerStateUnlock();
  }
  jumpToConfigStrip(k, idx);
}

void onOvDcaClick(lv_event_t *e) {
  int dca = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  bool curMuted = false;
  if (mixerStateLock(20)) {
    curMuted = mixerStateUnsafe().dca[dca].muted;
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_MUTE;
  cmd.kind = STRIP_DCA;
  cmd.idx = dca;
  cmd.bval = !curMuted;
  enqueueAppCommand(cmd);
}

void onOvDcaLongPress(lv_event_t *e) {
  int dca = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  jumpToRenameStrip(STRIP_DCA, dca);
}

void onOvMgClick(lv_event_t *e) {
  int mg = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  bool curOn = false;
  if (mixerStateLock(20)) {
    curOn = mixerStateUnsafe().muteGroupEngaged[mg];
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_MUTE_GRP;
  cmd.idx = mg;
  cmd.bval = !curOn;
  enqueueAppCommand(cmd);
}

void onOvMainSlider(lv_event_t *e) {
  lv_obj_t *sl = lv_event_get_target(e);
  float val = lv_slider_get_value(sl) / 1000.0f;
  char mainDb[16];
  x32_format_db(val, mainDb, sizeof(mainDb));
  setLabelTextIfChanged(s_ovMainDbLbl, mainDb);

  AppCommand cmd;
  cmd.type = CMD_SET_FADER;
  cmd.kind = STRIP_MAIN;
  cmd.idx = 0;
  cmd.fval = val;
  enqueueAppCommand(cmd);
}

void onOvMainMute(lv_event_t *e) {
  (void)e;
  bool m = false;
  if (mixerStateLock(20)) {
    m = mixerStateUnsafe().mainLr.muted;
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_MUTE;
  cmd.kind = STRIP_MAIN;
  cmd.idx = 0;
  cmd.bval = !m;
  enqueueAppCommand(cmd);
}

void onScCatClick(lv_event_t *e) {
  int c = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_scCategory = static_cast<ScCategory>(c);
  StripKind k;
  int start = 0, cnt = 0;
  getScCategoryRange(s_scCategory, &k, &start, &cnt);

  selectStripOptimistic(k, start, k != STRIP_DCA);
  s_scLastIdx = -1;
  focusScTextArea();
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onScPickStripClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  StripKind k;
  int start = 0, cnt = 0;
  getScCategoryRange(s_scCategory, &k, &start, &cnt);
  if (slot >= cnt) return;

  selectStripOptimistic(k, start + slot, k != STRIP_DCA);
  s_scLastIdx = -1;
  focusScTextArea();
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void commitScNameOnly(const char *txt) {
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    k = mixerStateUnsafe().selectedKind;
    idx = mixerStateUnsafe().selectedIdx;
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_NAME;
  cmd.kind = k;
  cmd.idx = idx;
  std::snprintf(cmd.text, sizeof(cmd.text), "%.12s", txt ? txt : "");
  enqueueAppCommand(cmd);
}

void saveCurrentScNameAndStep(int stepDir) {
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    k = mixerStateUnsafe().selectedKind;
    idx = mixerStateUnsafe().selectedIdx;
    mixerStateUnlock();
  }
  const char *txt = lv_textarea_get_text(s_scNameTa);
  AppCommand cmd;
  cmd.type = CMD_SET_NAME;
  cmd.kind = k;
  cmd.idx = idx;
  std::snprintf(cmd.text, sizeof(cmd.text), "%.12s", txt ? txt : "");
  enqueueAppCommand(cmd);

  if (stepDir != 0) {
    int cnt = strip_kind_count(k);
    int next = (idx + stepDir + cnt) % cnt;
    if (k == STRIP_CH) {
      s_scCategory = (next < 16) ? SC_CH1 : SC_CH2;
    }
    selectStripOptimistic(k, next, k != STRIP_DCA);
    s_scLastIdx = -1;
    syncActiveTabImmediate();
    requestImmediateRefresh();
  }
  focusScTextArea();
}

void onScTaValueChanged(lv_event_t *e) {
  (void)e;
  if (s_scSyncingTa) return;
  const char *txt = lv_textarea_get_text(s_scNameTa);
  s_scLastTypeMs = millis();
  setLabelTextIfChanged(s_scPrevName, (txt && txt[0]) ? txt : "(unnamed)");
  commitScNameOnly(txt);
}

void onScTaReady(lv_event_t *e) {
  (void)e;
  saveCurrentScNameAndStep(1);
}

void onScKeyboardCancel(lv_event_t *e) {
  (void)e;
  focusScTextArea();
}

void onScPrevBtn(lv_event_t *e) { (void)e; saveCurrentScNameAndStep(-1); }
void onScNextBtn(lv_event_t *e) { (void)e; saveCurrentScNameAndStep(1); }

void onScClearBtn(lv_event_t *e) {
  (void)e;
  setScTextProgrammatic("");
  s_scLastTypeMs = millis();
  setLabelTextIfChanged(s_scPrevName, "(unnamed)");
  commitScNameOnly("");
  focusScTextArea();
}

void onScSwatchClick(lv_event_t *e) {
  int baseCid = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))) & 0x07;
  AppCommand cmd;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    const StripState *s = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
    const uint8_t curInv = s ? (s->color & 0x08) : 0;
    cmd.type = CMD_SET_COLOR;
    cmd.kind = st.selectedKind;
    cmd.idx = st.selectedIdx;
    cmd.color = static_cast<uint8_t>(curInv | baseCid);
    mixerStateUnlock();
  }
  enqueueAppCommand(cmd);
  focusScTextArea();
}

void onScInvertClick(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    const StripState *s = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
    const uint8_t cur = s ? s->color : 0;
    cmd.type = CMD_SET_COLOR;
    cmd.kind = st.selectedKind;
    cmd.idx = st.selectedIdx;
    cmd.color = static_cast<uint8_t>((cur ^ 0x08) & 0x0F);
    mixerStateUnlock();
  }
  enqueueAppCommand(cmd);
  focusScTextArea();
}

void getIconCatRange(uint8_t cat, uint8_t *outStart, uint8_t *outEnd) {
  switch (cat) {
    case 1: *outStart = 1;  *outEnd = 16; break;  // Drums & Perc
    case 2: *outStart = 17; *outEnd = 26; break;  // Guitars & Amps
    case 3: *outStart = 27; *outEnd = 40; break;  // Keys & Horns/Strings
    case 4: *outStart = 41; *outEnd = 53; break;  // Vocals & Mics
    case 5: *outStart = 54; *outEnd = 74; break;  // I/O, PA & Groups
    case 0:
    default: *outStart = 1; *outEnd = 74; break;  // All 74
  }
}

void refreshScIconPicker(uint8_t activeIconId) {
  if (!s_scIconModal) return;
  for (int i = 0; i < 6; ++i) {
    lv_obj_set_style_bg_color(
        s_scIconCatBtns[i], lv_color_hex(i == s_scIconCat ? COL_ACCENT : COL_PANEL2), 0);
  }

  uint8_t startId = 1, endId = 74;
  getIconCatRange(s_scIconCat, &startId, &endId);
  const int total = (endId - startId) + 1;
  const int pages = (total + ICON_GRID_SLOTS - 1) / ICON_GRID_SLOTS;
  if (s_scIconPage >= pages) s_scIconPage = 0;

  const int firstOff = s_scIconPage * ICON_GRID_SLOTS;
  for (int slot = 0; slot < ICON_GRID_SLOTS; ++slot) {
    const int iconId = startId + firstOff + slot;
    const bool vis = (iconId <= endId);
    setVisibleIfChanged(s_scIconTiles[slot], vis);
    if (!vis) continue;

    s_scIconTileId[slot] = static_cast<uint8_t>(iconId);
    const bool isCur = (iconId == activeIconId);
    lv_obj_set_style_bg_color(s_scIconTiles[slot], lv_color_hex(isCur ? 0x1A2E48 : COL_PANEL2), 0);
    lv_obj_set_style_border_color(
        s_scIconTiles[slot], lv_color_hex(isCur ? COL_CYAN : COL_BORDER), 0);
    lv_obj_set_style_border_width(s_scIconTiles[slot], isCur ? 2 : 1, 0);
    lv_obj_set_style_text_color(
        s_scIconTileLbl[slot], lv_color_hex(isCur ? COL_CYAN : COL_TEXT), 0);

    char lblTxt[24];
    std::snprintf(lblTxt, sizeof(lblTxt), "#%d %s", iconId, x32_get_icon_name(static_cast<uint8_t>(iconId)));
    setLabelTextIfChanged(s_scIconTileLbl[slot], lblTxt);
    lv_obj_invalidate(s_scIconTileGlyph[slot]);
  }

  char pg[28];
  std::snprintf(pg, sizeof(pg), "%d / %d", s_scIconPage + 1, pages);
  setLabelTextIfChanged(s_scIconPageLbl, pg);
  setVisibleIfChanged(s_scIconPrevBtn, pages > 1);
  setVisibleIfChanged(s_scIconNextBtn, pages > 1);
}

void openScIconModal(lv_event_t *e) {
  (void)e;
  if (!s_scIconModal) return;
  uint8_t curIcon = s_scPrevIconVal;
  if (curIcon >= 1 && curIcon <= 16) s_scIconCat = 1;
  else if (curIcon <= 26) s_scIconCat = 2;
  else if (curIcon <= 40) s_scIconCat = 3;
  else if (curIcon <= 53) s_scIconCat = 4;
  else s_scIconCat = 5;
  s_scIconPage = 0;
  refreshScIconPicker(curIcon);
  lv_obj_clear_flag(s_scIconModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_scIconModal);
  requestImmediateRefresh();
}

void closeScIconModal(lv_event_t *e) {
  (void)e;
  if (s_scIconModal) lv_obj_add_flag(s_scIconModal, LV_OBJ_FLAG_HIDDEN);
  focusScTextArea();
  requestImmediateRefresh();
}

void onScIconCatClick(lv_event_t *e) {
  s_scIconCat = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_scIconPage = 0;
  refreshScIconPicker(s_scPrevIconVal);
  requestImmediateRefresh();
}

void onScIconPageClick(lv_event_t *e) {
  int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  uint8_t startId = 1, endId = 74;
  getIconCatRange(s_scIconCat, &startId, &endId);
  const int total = (endId - startId) + 1;
  const int pages = (total + ICON_GRID_SLOTS - 1) / ICON_GRID_SLOTS;
  if (pages <= 1) return;
  s_scIconPage = static_cast<uint8_t>((s_scIconPage + dir + pages) % pages);
  refreshScIconPicker(s_scPrevIconVal);
  requestImmediateRefresh();
}

void onScIconTileClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  if (slot < 0 || slot >= ICON_GRID_SLOTS) return;
  uint8_t chosen = s_scIconTileId[slot];
  if (chosen < 1 || chosen > 74) return;
  AppCommand cmd;
  if (mixerStateLock(20)) {
    cmd.type = CMD_SET_ICON;
    cmd.kind = mixerStateUnsafe().selectedKind;
    cmd.idx = mixerStateUnsafe().selectedIdx;
    cmd.icon = chosen;
    mixerStateUnlock();
  }
  enqueueAppCommand(cmd);
  closeScIconModal(nullptr);
}

// Matrix callbacks
void onMxModeClick(lv_event_t *e) {
  int m = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_mxMode = static_cast<MxMode>(m);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onMxBankClick(lv_event_t *e) {
  int b = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_mxBank = static_cast<uint8_t>(b);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onMxColHdrClick(lv_event_t *e) {
  int col = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  if (s_mxMode == MX_DCA) {
    jumpToRenameStrip(STRIP_DCA, col);
  } else if (s_mxMode == MX_MUTE && col < NUM_MUTE_GRPS) {
    bool cur = false;
    if (mixerStateLock(20)) {
      cur = mixerStateUnsafe().muteGroupEngaged[col];
      mixerStateUnlock();
    }
    AppCommand cmd;
    cmd.type = CMD_SET_MUTE_GRP;
    cmd.idx = col;
    cmd.bval = !cur;
    enqueueAppCommand(cmd);
  }
}

void onMxCellClick(lv_event_t *e) {
  uint32_t packed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  const int row = static_cast<int>((packed >> 8) & 0xFF);
  const int col = static_cast<int>(packed & 0xFF);
  StripKind kind = (s_mxBank == 4) ? STRIP_AUX : STRIP_CH;
  int idx = (s_mxBank == 4) ? row : (s_mxBank * 8 + row);

  AppCommand cmd;
  if (mixerStateLock(20)) {
    const StripState *s = mixerGetStripConst(mixerStateUnsafe(), kind, idx);
    if (s) {
      cmd.kind = kind;
      cmd.idx = idx;
      if (s_mxMode == MX_DCA) {
        cmd.type = CMD_SET_DCA_MASK;
        cmd.u32val = s->dcaMask ^ (1u << col);
      } else {
        cmd.type = CMD_SET_MUTE_MASK;
        cmd.u32val = s->muteMask ^ (1u << col);
      }
    }
    mixerStateUnlock();
  }
  if (cmd.type != CMD_NONE) enqueueAppCommand(cmd);
}

void onMxLinkClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  AppCommand cmd;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    if (slot < 16) {
      cmd.type = CMD_SET_CH_LINK;
      cmd.idx = slot;
      cmd.bval = !st.chLinks[slot];
    } else {
      int b = slot - 16;
      cmd.type = CMD_SET_BUS_LINK;
      cmd.idx = b;
      cmd.bval = !st.busLinks[b];
    }
    mixerStateUnlock();
  }
  enqueueAppCommand(cmd);
}

// Faders tab callbacks
void onFaderBankClick(lv_event_t *e) {
  int fb = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_faderBank = static_cast<FaderBankId>(fb);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onBusDropdownChanged(lv_event_t *e) {
  lv_obj_t *dd = lv_event_get_target(e);
  int sel = static_cast<int>(lv_dropdown_get_selected(dd));
  AppCommand cmd;
  cmd.type = CMD_SET_ACTIVE_BUS_SEND;
  cmd.bus = static_cast<int8_t>(sel - 1);
  enqueueAppCommand(cmd);
}

void onExitSofClick(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_SET_ACTIVE_BUS_SEND;
  cmd.bus = -1;
  enqueueAppCommand(cmd);
}

void resolveFaderSlot(int slot, StripKind *outKind, int *outIdx, bool *outIsSend, int *outBus) {
  int activeBus = -1;
  if (mixerStateLock(10)) {
    activeBus = mixerStateUnsafe().activeBusSend;
    mixerStateUnlock();
  }
  if (slot == 8) {
    *outKind = (activeBus >= 0) ? STRIP_BUS : STRIP_MAIN;
    *outIdx = (activeBus >= 0) ? activeBus : 0;
    *outIsSend = false;
    *outBus = -1;
    return;
  }
  StripKind k;
  int start = 0;
  getFaderBankRange(s_faderBank, &k, &start);
  *outKind = k;
  *outIdx = start + slot;
  *outIsSend = (activeBus >= 0 && k == STRIP_CH);
  *outBus = activeBus;
}

void onFaderCapClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  StripKind k;
  int idx = 0, bus = -1;
  bool isSend = false;
  resolveFaderSlot(slot, &k, &idx, &isSend, &bus);
  jumpToRenameStrip(k, idx);
}

void onFaderUnitySnapClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  StripKind k;
  int idx = 0, bus = -1;
  bool isSend = false;
  resolveFaderSlot(slot, &k, &idx, &isSend, &bus);

  AppCommand cmd;
  if (isSend) {
    cmd.type = CMD_SET_SEND_LEVEL;
    cmd.idx = idx;
    cmd.bus = static_cast<int8_t>(bus);
    cmd.fval = 0.75f;
  } else {
    cmd.type = CMD_SET_FADER;
    cmd.kind = k;
    cmd.idx = idx;
    cmd.fval = 0.75f;
  }
  enqueueAppCommand(cmd);
}

void onFaderSliderChanged(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  lv_obj_t *sl = lv_event_get_target(e);
  float val = lv_slider_get_value(sl) / 1000.0f;

  char dbBuf[16];
  x32_format_db(val, dbBuf, sizeof(dbBuf));
  setLabelTextIfChanged(s_fDbLbl[slot], dbBuf);

  StripKind k;
  int idx = 0, bus = -1;
  bool isSend = false;
  resolveFaderSlot(slot, &k, &idx, &isSend, &bus);

  AppCommand cmd;
  if (isSend) {
    cmd.type = CMD_SET_SEND_LEVEL;
    cmd.idx = idx;
    cmd.bus = static_cast<int8_t>(bus);
    cmd.fval = val;
  } else {
    cmd.type = CMD_SET_FADER;
    cmd.kind = k;
    cmd.idx = idx;
    cmd.fval = val;
  }
  enqueueAppCommand(cmd);
}

void onFaderSoloClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  StripKind k;
  int idx = 0, bus = -1;
  bool isSend = false;
  resolveFaderSlot(slot, &k, &idx, &isSend, &bus);
  bool cur = false;
  if (mixerStateLock(20)) {
    const StripState *s = mixerGetStripConst(mixerStateUnsafe(), k, idx);
    if (s) cur = s->solo;
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_SOLO;
  cmd.kind = k;
  cmd.idx = idx;
  cmd.bval = !cur;
  enqueueAppCommand(cmd);
}

void onFaderMuteClick(lv_event_t *e) {
  int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  StripKind k;
  int idx = 0, bus = -1;
  bool isSend = false;
  resolveFaderSlot(slot, &k, &idx, &isSend, &bus);
  AppCommand cmd;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    if (isSend) {
      cmd.type = CMD_SET_SEND_ON;
      cmd.idx = idx;
      cmd.bus = static_cast<int8_t>(bus);
      cmd.bval = !st.chSendOn[idx][bus];
    } else {
      const StripState *s = mixerGetStripConst(st, k, idx);
      if (s) {
        cmd.type = CMD_SET_MUTE;
        cmd.kind = k;
        cmd.idx = idx;
        cmd.bval = !s->muted;
      }
    }
    mixerStateUnlock();
  }
  if (cmd.type != CMD_NONE) enqueueAppCommand(cmd);
}

// ============================================================
// Network Tab & Dedicated Keyboard Sheet Callbacks
// ============================================================

void openNetKbFor(NetKbTarget target) {
  s_netKbTarget = target;
  const char *srcTxt = "";
  if (target == NET_KB_SSID) {
    srcTxt = lv_textarea_get_text(s_netSsidTa);
    lv_label_set_text(s_netKbTitleLbl, "Enter Wi-Fi SSID (2.4 GHz Network Name)");
    lv_textarea_set_password_mode(s_netModalTa, false);
    lv_textarea_set_max_length(s_netModalTa, 32);
    lv_textarea_set_placeholder_text(s_netModalTa, "Type Wi-Fi SSID...");
    lv_label_set_text(s_netModalCommitLbl, "Done");
    s_netKbNumMode = false;
    lv_keyboard_set_mode(s_netKb, LV_KEYBOARD_MODE_TEXT_LOWER);
  } else if (target == NET_KB_PASS) {
    srcTxt = lv_textarea_get_text(s_netPassTa);
    lv_label_set_text(s_netKbTitleLbl, "Enter Wi-Fi Password");
    lv_textarea_set_password_mode(s_netModalTa, true);
    lv_textarea_set_max_length(s_netModalTa, 63);
    lv_textarea_set_placeholder_text(s_netModalTa, "Type Wi-Fi password...");
    lv_label_set_text(s_netModalShowLbl, "Show");
    lv_label_set_text(s_netModalCommitLbl, "Save & Join");
    s_netKbNumMode = false;
    lv_keyboard_set_mode(s_netKb, LV_KEYBOARD_MODE_TEXT_LOWER);
  } else {
    srcTxt = lv_textarea_get_text(s_netX32IpTa);
    lv_label_set_text(s_netKbTitleLbl, "Enter Behringer X32 IP Address");
    lv_textarea_set_password_mode(s_netModalTa, false);
    lv_textarea_set_max_length(s_netModalTa, 32);
    lv_textarea_set_placeholder_text(s_netModalTa, "192.168.1.50");
    lv_label_set_text(s_netModalCommitLbl, "Connect X32");
    s_netKbNumMode = true;
    lv_label_set_text(s_netModalModeLbl, "ABC");
    lv_keyboard_set_mode(s_netKb, LV_KEYBOARD_MODE_NUMBER);
  }

  setVisibleIfChanged(s_netModalShowBtn, target == NET_KB_PASS);
  setVisibleIfChanged(s_netModalIp1Btn, target == NET_KB_X32IP);
  setVisibleIfChanged(s_netModalIp2Btn, target == NET_KB_X32IP);
  setVisibleIfChanged(s_netModalModeBtn, target == NET_KB_X32IP);

  lv_textarea_set_text(s_netModalTa, srcTxt ? srcTxt : "");
  lv_textarea_set_cursor_pos(s_netModalTa, LV_TEXTAREA_CURSOR_LAST);
  lv_obj_add_state(s_netModalTa, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_netKb, s_netModalTa);

  lv_obj_clear_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_netKbModal);
  requestImmediateRefresh();
}

void commitNetModal() {
  const char *val = lv_textarea_get_text(s_netModalTa);
  if (!val) val = "";
  if (s_netKbTarget == NET_KB_SSID) {
    lv_textarea_set_text(s_netSsidTa, val);
  } else if (s_netKbTarget == NET_KB_PASS) {
    lv_textarea_set_text(s_netPassTa, val);
    const char *ssid = lv_textarea_get_text(s_netSsidTa);
    if (ssid && ssid[0] != '\0') {
      AppCommand cmd;
      cmd.type = CMD_WIFI_CONNECT;
      std::snprintf(cmd.text, sizeof(cmd.text), "%s", ssid);
      std::snprintf(cmd.text2, sizeof(cmd.text2), "%s", val);
      enqueueAppCommand(cmd);
    }
  } else {
    lv_textarea_set_text(s_netX32IpTa, val);
    if (val[0] != '\0') {
      AppCommand cmd;
      cmd.type = CMD_X32_CONNECT;
      std::snprintf(cmd.text, sizeof(cmd.text), "%s", val);
      enqueueAppCommand(cmd);
    }
  }
  lv_obj_add_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);
  requestImmediateRefresh();
}

void onNetTaClick(lv_event_t *e) {
  int tgt = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  openNetKbFor(static_cast<NetKbTarget>(tgt));
}

void onNetModalCommit(lv_event_t *e) {
  (void)e;
  commitNetModal();
}

void onNetModalCancel(lv_event_t *e) {
  (void)e;
  lv_obj_add_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);
  requestImmediateRefresh();
}

void onNetModalClear(lv_event_t *e) {
  (void)e;
  lv_textarea_set_text(s_netModalTa, "");
  lv_obj_add_state(s_netModalTa, LV_STATE_FOCUSED);
}

void onNetModalShowToggle(lv_event_t *e) {
  (void)e;
  const bool next = !lv_textarea_get_password_mode(s_netModalTa);
  lv_textarea_set_password_mode(s_netModalTa, next);
  lv_label_set_text(s_netModalShowLbl, next ? "Show" : "Hide");
  lv_obj_add_state(s_netModalTa, LV_STATE_FOCUSED);
}

void onNetModalIpPrefix(lv_event_t *e) {
  const char *prefix = static_cast<const char *>(lv_event_get_user_data(e));
  if (!prefix) return;
  lv_textarea_set_text(s_netModalTa, prefix);
  lv_textarea_set_cursor_pos(s_netModalTa, LV_TEXTAREA_CURSOR_LAST);
  lv_obj_add_state(s_netModalTa, LV_STATE_FOCUSED);
}

void onNetModalKbModeToggle(lv_event_t *e) {
  (void)e;
  s_netKbNumMode = !s_netKbNumMode;
  lv_keyboard_set_mode(
      s_netKb, s_netKbNumMode ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_label_set_text(s_netModalModeLbl, s_netKbNumMode ? "ABC" : "123");
  lv_obj_add_state(s_netModalTa, LV_STATE_FOCUSED);
}

void onNetWifiScan(lv_event_t *e) {
  (void)e;
  if (s_netScanBtn && lv_obj_has_state(s_netScanBtn, LV_STATE_DISABLED)) return;
  AppCommand cmd;
  cmd.type = CMD_WIFI_SCAN;
  enqueueAppCommand(cmd);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onNetWifiItemClick(lv_event_t *e) {
  int idx = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  char ssid[33] = {0};
  bool sec = false;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    if (idx < st.wifiScanCount) {
      std::snprintf(ssid, sizeof(ssid), "%s", st.wifiScan[idx].ssid);
      sec = st.wifiScan[idx].secure;
    }
    mixerStateUnlock();
  }
  if (ssid[0] != '\0') {
    lv_textarea_set_text(s_netSsidTa, ssid);
    if (sec) {
      openNetKbFor(NET_KB_PASS);
    } else {
      AppCommand cmd;
      cmd.type = CMD_WIFI_CONNECT;
      std::snprintf(cmd.text, sizeof(cmd.text), "%s", ssid);
      enqueueAppCommand(cmd);
    }
  }
}

void onNetWifiConnect(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_WIFI_CONNECT;
  std::snprintf(cmd.text, sizeof(cmd.text), "%s", lv_textarea_get_text(s_netSsidTa));
  std::snprintf(cmd.text2, sizeof(cmd.text2), "%s", lv_textarea_get_text(s_netPassTa));
  enqueueAppCommand(cmd);
}

void onNetX32Discover(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_X32_DISCOVER;
  enqueueAppCommand(cmd);
}

void onNetX32ConsoleClick(lv_event_t *e) {
  int idx = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  char ip[20] = {0};
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    if (idx < st.consoleCount) {
      std::snprintf(ip, sizeof(ip), "%s", st.consoles[idx].ip);
    }
    mixerStateUnlock();
  }
  if (ip[0] != '\0') {
    lv_textarea_set_text(s_netX32IpTa, ip);
    AppCommand cmd;
    cmd.type = CMD_X32_CONNECT;
    std::snprintf(cmd.text, sizeof(cmd.text), "%s", ip);
    enqueueAppCommand(cmd);
  }
}

void onNetX32Connect(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_X32_CONNECT;
  std::snprintf(cmd.text, sizeof(cmd.text), "%s", lv_textarea_get_text(s_netX32IpTa));
  enqueueAppCommand(cmd);
}

void onNetX32Sync(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_X32_SYNC_ALL;
  enqueueAppCommand(cmd);
}

void onNetBacklightToggle(lv_event_t *e) {
  (void)e;
  bool bl = true;
  if (mixerStateLock(20)) {
    bl = mixerStateUnsafe().backlightOn;
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_BACKLIGHT;
  cmd.bval = !bl;
  enqueueAppCommand(cmd);
}

void formatScreenTimeoutLabel(uint16_t sec, char *buf, size_t cap) {
  if (sec == 0) {
    std::snprintf(buf, cap, "No-X32 Off: Never");
  } else if (sec < 60) {
    std::snprintf(buf, cap, "No-X32 Off: %us", static_cast<unsigned>(sec));
  } else if (sec == 60) {
    std::snprintf(buf, cap, "No-X32 Off: 1 min");
  } else {
    std::snprintf(buf, cap, "No-X32 Off: %u min", static_cast<unsigned>(sec / 60u));
  }
}

void onNetIdleTimeoutStep(lv_event_t *e) {
  static constexpr uint16_t kTimeoutSteps[] = {0, 15, 30, 60, 120, 300, 600};
  static constexpr int kStepCount = sizeof(kTimeoutSteps) / sizeof(kTimeoutSteps[0]);
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));

  uint16_t cur = DEFAULT_SCREEN_TIMEOUT_SEC;
  if (mixerStateLock(20)) {
    cur = mixerStateUnsafe().screenTimeoutSec;
    mixerStateUnlock();
  }
  int idx = 3;  // 60s default
  for (int i = 0; i < kStepCount; ++i) {
    if (kTimeoutSteps[i] == cur) {
      idx = i;
      break;
    }
  }
  idx += delta;
  if (idx < 0) idx = 0;
  if (idx >= kStepCount) idx = kStepCount - 1;

  AppCommand cmd;
  cmd.type = CMD_SET_SCREEN_TIMEOUT;
  cmd.u32val = kTimeoutSteps[idx];
  enqueueAppCommand(cmd);
}

void onNetRegenPasskey(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_REGEN_WEB_PASSKEY;
  enqueueAppCommand(cmd);
}

void onNetRevokeSessions(lv_event_t *e) {
  (void)e;
  AppCommand cmd;
  cmd.type = CMD_REVOKE_WEB_SESSIONS;
  enqueueAppCommand(cmd);
}

void onNetOtaCheck(lv_event_t *e) {
  (void)e;
  if (s_netOtaCheckBtn && lv_obj_has_state(s_netOtaCheckBtn, LV_STATE_DISABLED)) return;
  bool wifiOk = false;
  bool busy = false;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    wifiOk = st.wifiConnected;
    busy = (st.otaPhase == OTA_CHECKING || st.otaPhase == OTA_DOWNLOADING ||
            st.otaPhase == OTA_VERIFYING || st.otaPhase == OTA_REBOOTING);
    mixerStateUnlock();
  }
  s_otaInstallArmed = false;
  if (!wifiOk || busy) {
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  AppCommand cmd;
  cmd.type = CMD_OTA_CHECK;
  enqueueAppCommand(cmd);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onNetOtaInstall(lv_event_t *e) {
  (void)e;
  if (s_netOtaInstallBtn && lv_obj_has_state(s_netOtaInstallBtn, LV_STATE_DISABLED)) return;
  bool wifiOk = false;
  uint8_t phase = OTA_IDLE;
  bool hasVer = false;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    wifiOk = st.wifiConnected;
    phase = st.otaPhase;
    hasVer = (st.otaLatestVer[0] != '\0');
    mixerStateUnlock();
  }
  if (!wifiOk || phase != OTA_AVAILABLE || !hasVer) {
    s_otaInstallArmed = false;
    mixerMarkUiOnlyDirty(DIRTY_NETWORK);
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  const uint32_t now = millis();
  if (!s_otaInstallArmed || static_cast<int32_t>(now - s_otaInstallArmUntilMs) >= 0) {
    s_otaInstallArmed = true;
    s_otaInstallArmUntilMs = now + 5000u;
    mixerMarkUiOnlyDirty(DIRTY_NETWORK);
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  s_otaInstallArmed = false;
  AppCommand cmd;
  cmd.type = CMD_OTA_INSTALL;
  enqueueAppCommand(cmd);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

static constexpr int LIB_TARGET_COUNT = 57;  // 32 CH + 8 AUX + 16 BUS + 1 MAIN

int stripToLibOrder(StripKind kind, int idx) {
  if (kind == STRIP_CH && idx >= 0 && idx < NUM_CHANNELS) return idx;
  if (kind == STRIP_AUX && idx >= 0 && idx < NUM_AUX) return 32 + idx;
  if (kind == STRIP_BUS && idx >= 0 && idx < NUM_BUSES) return 40 + idx;
  if (kind == STRIP_MAIN) return 56;
  return -1;
}

void libOrderToStrip(int ord, StripKind *outKind, int *outIdx) {
  const int w = ((ord % LIB_TARGET_COUNT) + LIB_TARGET_COUNT) % LIB_TARGET_COUNT;
  if (w < 32) {
    *outKind = STRIP_CH;
    *outIdx = w;
  } else if (w < 40) {
    *outKind = STRIP_AUX;
    *outIdx = w - 32;
  } else if (w < 56) {
    *outKind = STRIP_BUS;
    *outIdx = w - 40;
  } else {
    *outKind = STRIP_MAIN;
    *outIdx = 0;
  }
}

void onLibModeClick(lv_event_t *e) {
  const int m = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_libMode = (m == 1) ? LIB_PRESETS : LIB_SCENES;
  s_libLoadArmed = false;
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onLibBankClick(lv_event_t *e) {
  const int b = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_libBank = static_cast<uint8_t>((b >= 0 && b < 4) ? b : 0);
  s_libSelSlot = static_cast<int16_t>(s_libBank * 8 + (s_libSelSlot & 7));
  s_libLoadArmed = false;
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onLibCardClick(lv_event_t *e) {
  const int localIdx = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_libSelSlot = static_cast<int16_t>(s_libBank * 8 + (localIdx & 7));
  s_libLoadArmed = false;
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onLibTargetStep(lv_event_t *e) {
  const int dir = (reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) >= 0) ? 1 : -1;
  s_libLoadArmed = false;
  StripKind curK = STRIP_CH;
  int16_t curI = 0;
  if (mixerStateLock(20)) {
    curK = mixerStateUnsafe().selectedKind;
    curI = mixerStateUnsafe().selectedIdx;
    mixerStateUnlock();
  }
  const int ord = stripToLibOrder(curK, curI);
  const int nextOrd = (ord < 0) ? (dir > 0 ? 0 : LIB_TARGET_COUNT - 1) : (ord + dir);
  StripKind nextK = STRIP_CH;
  int nextI = 0;
  libOrderToStrip(nextOrd, &nextK, &nextI);
  selectStripOptimistic(nextK, nextI, false);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onLibScopeClick(lv_event_t *e) {
  const uint8_t bit = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  const uint8_t next = static_cast<uint8_t>(s_libScope ^ (1u << bit));
  if (next != 0) {
    s_libScope = next;
  }
  s_libLoadArmed = false;
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onLibLoadClick(lv_event_t *e) {
  (void)e;
  bool canLoad = false;
  bool badDca = false;
  StripKind selKind = STRIP_CH;
  int16_t selIdx = 0;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    selKind = st.selectedKind;
    selIdx = st.selectedIdx;
    if (s_libMode == LIB_SCENES) {
      canLoad = (s_libSelSlot >= 0 && s_libSelSlot < NUM_SCENES && st.scenes[s_libSelSlot].hasData);
    } else {
      badDca = (st.selectedKind == STRIP_DCA);
      canLoad = (!badDca && s_libSelSlot >= 0 && s_libSelSlot < NUM_CH_PRESETS &&
                 st.chPresets[s_libSelSlot].hasData && s_libScope != 0);
    }
    mixerStateUnlock();
  }
  if (!canLoad) {
    s_libLoadArmed = false;
    if (badDca && mixerStateLock(20)) {
      std::snprintf(mixerStateUnsafe().statusMsg, 64, "Select Ch/Aux/Bus/Main to load preset");
      mixerMarkUiOnlyDirty(DIRTY_HEADER | DIRTY_LIBRARY);
      mixerStateUnlock();
    }
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  if (!s_libLoadArmed) {
    s_libLoadArmed = true;
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  s_libLoadArmed = false;
  AppCommand cmd;
  if (s_libMode == LIB_SCENES) {
    cmd.type = CMD_RECALL_SCENE;
    cmd.slot = s_libSelSlot;
    cmd.idx = s_libSelSlot;
  } else {
    cmd.type = CMD_LOAD_CH_PRESET;
    cmd.kind = selKind;
    cmd.idx = selIdx;
    cmd.slot = s_libSelSlot;
    cmd.scope = s_libScope;
  }
  enqueueAppCommand(cmd);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void openLibSaveModal(lv_event_t *e) {
  (void)e;
  s_libLoadArmed = false;
  char title[64] = {0};
  char initName[24] = {0};
  char initNotes[36] = {0};
  bool blockedDca = false;

  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    if (s_libMode == LIB_SCENES) {
      std::snprintf(title, sizeof(title), "Save Mixer to Scene %02d", s_libSelSlot);
      if (st.scenes[s_libSelSlot].name[0]) {
        std::snprintf(initName, sizeof(initName), "%s", st.scenes[s_libSelSlot].name);
      } else {
        std::snprintf(initName, sizeof(initName), "Scene %02d", s_libSelSlot);
      }
      std::snprintf(initNotes, sizeof(initNotes), "%s", st.scenes[s_libSelSlot].notes);
    } else if (st.selectedKind == STRIP_DCA) {
      blockedDca = true;
      std::snprintf(mixerStateUnsafe().statusMsg, 64, "Select Ch/Aux/Bus/Main to save preset");
      mixerMarkUiOnlyDirty(DIRTY_HEADER);
    } else {
      char tag[12];
      mixerFormatStripTag(st.selectedKind, st.selectedIdx, tag, sizeof(tag));
      std::snprintf(title, sizeof(title), "Save %s -> Preset %02d", tag, s_libSelSlot + 1);
      const StripState *sp = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
      if (st.chPresets[s_libSelSlot].name[0]) {
        std::snprintf(initName, sizeof(initName), "%s", st.chPresets[s_libSelSlot].name);
      } else if (sp && sp->name[0]) {
        std::snprintf(initName, sizeof(initName), "%s", sp->name);
      } else {
        std::snprintf(initName, sizeof(initName), "Preset %02d", s_libSelSlot + 1);
      }
    }
    mixerStateUnlock();
  }

  if (blockedDca) {
    requestImmediateRefresh();
    return;
  }

  lv_label_set_text(s_libKbTitleLbl, title);
  lv_textarea_set_text(s_libModalNameTa, initName);
  lv_textarea_set_cursor_pos(s_libModalNameTa, LV_TEXTAREA_CURSOR_LAST);
  lv_textarea_set_text(s_libModalNotesTa, initNotes);
  lv_textarea_set_cursor_pos(s_libModalNotesTa, LV_TEXTAREA_CURSOR_LAST);
  setVisibleIfChanged(s_libModalNotesTa, s_libMode == LIB_SCENES);

  s_libActiveTa = s_libModalNameTa;
  lv_obj_add_state(s_libModalNameTa, LV_STATE_FOCUSED);
  lv_obj_clear_state(s_libModalNotesTa, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_libKb, s_libModalNameTa);
  s_libKbNumMode = false;
  lv_keyboard_set_mode(s_libKb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_label_set_text(s_libModalModeLbl, "123");

  lv_obj_clear_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_libKbModal);
  requestImmediateRefresh();
}

void onLibModalTaFocus(lv_event_t *e) {
  lv_obj_t *ta = lv_event_get_target(e);
  if (!ta) return;
  s_libActiveTa = ta;
  lv_keyboard_set_textarea(s_libKb, ta);
}

void onLibModalCommit(lv_event_t *e) {
  (void)e;
  const char *name = lv_textarea_get_text(s_libModalNameTa);
  const char *notes = lv_textarea_get_text(s_libModalNotesTa);
  AppCommand cmd;
  if (s_libMode == LIB_SCENES) {
    cmd.type = CMD_SAVE_SCENE;
    cmd.slot = s_libSelSlot;
    cmd.idx = s_libSelSlot;
    std::snprintf(cmd.text, sizeof(cmd.text), "%s", name ? name : "");
    std::snprintf(cmd.text2, sizeof(cmd.text2), "%s", notes ? notes : "");
  } else {
    StripKind k = STRIP_CH;
    int16_t idx = 0;
    if (mixerStateLock(20)) {
      k = mixerStateUnsafe().selectedKind;
      idx = mixerStateUnsafe().selectedIdx;
      mixerStateUnlock();
    }
    if (k == STRIP_DCA) {
      lv_obj_add_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
      requestImmediateRefresh();
      return;
    }
    cmd.type = CMD_SAVE_CH_PRESET;
    cmd.kind = k;
    cmd.idx = idx;
    cmd.slot = s_libSelSlot;
    std::snprintf(cmd.text, sizeof(cmd.text), "%s", name ? name : "");
  }
  enqueueAppCommand(cmd);
  lv_obj_add_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onLibModalCancel(lv_event_t *e) {
  (void)e;
  lv_obj_add_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
  requestImmediateRefresh();
}

void onLibModalClear(lv_event_t *e) {
  (void)e;
  lv_obj_t *ta = s_libActiveTa ? s_libActiveTa : s_libModalNameTa;
  lv_textarea_set_text(ta, "");
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
}

void onLibModalKbModeToggle(lv_event_t *e) {
  (void)e;
  s_libKbNumMode = !s_libKbNumMode;
  lv_keyboard_set_mode(
      s_libKb, s_libKbNumMode ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_label_set_text(s_libModalModeLbl, s_libKbNumMode ? "ABC" : "123");
  if (s_libActiveTa) lv_obj_add_state(s_libActiveTa, LV_STATE_FOCUSED);
}

// ============================================================
// Config Tab Callbacks (TAB_CONFIG)
// ============================================================

void getStripCfgValues(const MixerState &st,
                       StripKind kind,
                       int idx,
                       uint8_t *outSrc,
                       float *outTrim,
                       bool *outInvert,
                       bool *outHpon,
                       float *outHpf,
                       bool *outMainSt,
                       float *outPan) {
  if (kind == STRIP_AUX) {
    const AuxConfigState &c = st.auxCfg[idx];
    *outSrc = c.source;
    *outTrim = c.trim;
    *outInvert = c.invert;
    *outHpon = false;
    *outHpf = 0.0f;
    *outMainSt = c.mainSt;
    *outPan = c.pan;
  } else {
    const ChConfigState &c = st.chCfg[idx];
    *outSrc = c.source;
    *outTrim = c.trim;
    *outInvert = c.invert;
    *outHpon = c.hpon;
    *outHpf = c.hpf;
    *outMainSt = c.mainSt;
    *outPan = c.pan;
  }
}

void finishCfgAction(uint32_t dirtyFlags) {
  mixerMarkDirty(dirtyFlags);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void applyCfgSource(uint8_t newSrc) {
  StripKind k = STRIP_CH;
  int idx = 0;
  bool ok = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_CH) {
      st.chCfg[idx].source = newSrc;
      ok = true;
    } else if (k == STRIP_AUX) {
      st.auxCfg[idx].source = newSrc;
      ok = true;
    }
    mixerStateUnlock();
  }
  if (!ok) return;
  s_cfgPhantomArmed = false;
  AppCommand cmd;
  cmd.type = CMD_SET_SOURCE;
  cmd.kind = k;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.u32val = newSrc;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG | DIRTY_OVERVIEW);
}

void onCfgCatClick(lv_event_t *e) {
  const int c = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_cfgCat = (c >= 0 && c <= 2) ? static_cast<CfgCat>(c) : CFG_CH1;
  StripKind k = STRIP_CH;
  int start = 0, cnt = 0;
  getCfgCategoryRange(s_cfgCat, &k, &start, &cnt);
  s_cfgPhantomArmed = false;
  s_cfgLastKind = k;
  s_cfgLastIdx = start;
  selectStripOptimistic(k, start, true);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onCfgPickClick(lv_event_t *e) {
  const int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  StripKind k = STRIP_CH;
  int start = 0, cnt = 0;
  getCfgCategoryRange(s_cfgCat, &k, &start, &cnt);
  if (slot < 0 || slot >= cnt) return;
  s_cfgPhantomArmed = false;
  s_cfgLastKind = k;
  s_cfgLastIdx = start + slot;
  selectStripOptimistic(k, start + slot, true);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onCfgStepClick(lv_event_t *e) {
  const int dir = (reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) >= 0) ? 1 : -1;
  StripKind curK = STRIP_CH;
  int curIdx = 0;
  if (mixerStateLock(20)) {
    getActiveConfigTarget(mixerStateUnsafe(), &curK, &curIdx);
    mixerStateUnlock();
  }
  const int total = NUM_CHANNELS + NUM_AUX;
  const int ord = (curK == STRIP_AUX) ? (NUM_CHANNELS + curIdx) : curIdx;
  const int nextOrd = ((ord + dir) % total + total) % total;
  const StripKind nextK = (nextOrd < NUM_CHANNELS) ? STRIP_CH : STRIP_AUX;
  const int nextI = (nextOrd < NUM_CHANNELS) ? nextOrd : (nextOrd - NUM_CHANNELS);
  s_cfgLastKind = nextK;
  s_cfgLastIdx = nextI;
  s_cfgCat = (nextK == STRIP_AUX) ? CFG_AUX : (nextI < 16 ? CFG_CH1 : CFG_CH2);
  s_cfgPhantomArmed = false;
  selectStripOptimistic(nextK, nextI, true);
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onCfgJumpNameClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    getActiveConfigTarget(mixerStateUnsafe(), &k, &idx);
    mixerStateUnlock();
  }
  jumpToRenameStrip(k, idx);
}

void onCfgSrcTabClick(lv_event_t *e) {
  const int t = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  s_cfgSrcTab = static_cast<uint8_t>(t & 3);
  s_cfgCache.init = false;
  syncActiveTabImmediate();
  requestImmediateRefresh();
}

void onCfgSrcBtnClick(lv_event_t *e) {
  const int slot = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  if (slot < 0 || slot >= 16) return;
  applyCfgSource(static_cast<uint8_t>(s_cfgSrcTab * 16 + slot + 1));
}

void onCfgSrcOffClick(lv_event_t *e) {
  (void)e;
  applyCfgSource(0);
}

void onCfgSrcDefClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    getActiveConfigTarget(mixerStateUnsafe(), &k, &idx);
    mixerStateUnlock();
  }
  applyCfgSource(x32_default_source_for_strip(static_cast<int>(k), idx));
}

void applyCfgGainStepVal(int32_t sliderStep, bool syncUiNow) {
  if (sliderStep < 0) sliderStep = 0;
  if (sliderStep > 144) sliderStep = 144;
  AppCommand cmd;
  char valBuf[20] = {0};

  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    StripKind k = STRIP_CH;
    int idx = 0;
    getActiveConfigTarget(st, &k, &idx);
    uint8_t src = 0;
    float trim = 0.5f, hpf = 0.0f, pan = 0.5f;
    bool inv = false, hpon = false, mainSt = true;
    getStripCfgValues(st, k, idx, &src, &trim, &inv, &hpon, &hpf, &mainSt, &pan);
    const int haIdx = x32_source_to_headamp_idx(src, st.inRouting);
    const bool split = (haIdx >= 0) && x32_headamp_is_gain_split(haIdx, st.haFlags);
    const bool useHa = (haIdx >= 0) && !split && !s_cfgForceTrim;
    if (useHa) {
      const float db = -12.0f + static_cast<float>(sliderStep) * 0.5f;
      const float f = x32_db_to_gain(db);
      st.headampGain[haIdx] = f;
      cmd.type = CMD_SET_GAIN;
      cmd.idx = static_cast<int16_t>(haIdx);
      cmd.fval = f;
      x32_format_gain_db(f, valBuf, sizeof(valBuf));
    } else {
      const float db = -18.0f + static_cast<float>(sliderStep) * 0.25f;
      const float f = x32_db_to_trim(db);
      if (k == STRIP_AUX) st.auxCfg[idx].trim = f;
      else st.chCfg[idx].trim = f;
      cmd.type = CMD_SET_TRIM;
      cmd.kind = k;
      cmd.idx = static_cast<int16_t>(idx);
      cmd.fval = f;
      x32_format_trim_db(f, valBuf, sizeof(valBuf));
    }
    mixerStateUnlock();
  }
  if (cmd.type == CMD_NONE) return;
  s_cfgCache.gainStep = sliderStep;
  setLabelTextIfChanged(s_cfgGainDbLbl, valBuf);
  enqueueAppCommand(cmd);
  if (syncUiNow) {
    finishCfgAction(DIRTY_CONFIG);
  } else {
    mixerMarkDirty(DIRTY_CONFIG);
  }
}

void onCfgGainSliderChanged(lv_event_t *e) {
  lv_obj_t *sl = lv_event_get_target(e);
  applyCfgGainStepVal(lv_slider_get_value(sl), false);
}

void onCfgGainStep(lv_event_t *e) {
  const int dir = (reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) >= 0) ? 1 : -1;
  applyCfgGainStepVal(lv_slider_get_value(s_cfgGainSlider) + dir, true);
}

void onCfgTrimModeClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  bool hasUnSplitHa = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    uint8_t src = 0;
    float trim = 0.5f, hpf = 0.0f, pan = 0.5f;
    bool inv = false, hpon = false, mainSt = true;
    getStripCfgValues(st, k, idx, &src, &trim, &inv, &hpon, &hpf, &mainSt, &pan);
    const int haIdx = x32_source_to_headamp_idx(src, st.inRouting);
    const bool split = (haIdx >= 0) && x32_headamp_is_gain_split(haIdx, st.haFlags);
    hasUnSplitHa = (haIdx >= 0) && !split;
    if (!hasUnSplitHa) {
      if (k == STRIP_AUX) st.auxCfg[idx].trim = 0.5f;
      else st.chCfg[idx].trim = 0.5f;
    }
    mixerStateUnlock();
  }
  if (hasUnSplitHa) {
    s_cfgForceTrim = !s_cfgForceTrim;
    s_cfgPhantomArmed = false;
    s_cfgCache.init = false;
    finishCfgAction(DIRTY_CONFIG);
    return;
  }
  AppCommand cmd;
  cmd.type = CMD_SET_TRIM;
  cmd.kind = k;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.fval = 0.5f;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG);
}

void onCfgPhantomClick(lv_event_t *e) {
  (void)e;
  int haIdx = -1;
  bool split = false;
  bool curPh = false;
  if (mixerStateLock(20)) {
    const MixerState &st = mixerStateUnsafe();
    StripKind k = STRIP_CH;
    int idx = 0;
    getActiveConfigTarget(st, &k, &idx);
    const uint8_t src = (k == STRIP_AUX) ? st.auxCfg[idx].source : st.chCfg[idx].source;
    haIdx = x32_source_to_headamp_idx(src, st.inRouting);
    split = (haIdx >= 0) && x32_headamp_is_gain_split(haIdx, st.haFlags);
    if (haIdx >= 0 && haIdx < 128) curPh = st.headampPhantom[haIdx];
    mixerStateUnlock();
  }
  if (haIdx < 0 || split) {
    s_cfgPhantomArmed = false;
    return;
  }
  const uint32_t now = millis();
  if (!s_cfgPhantomArmed || s_cfgPhantomArmHa != haIdx || (now - s_cfgPhantomArmMs > 3500u)) {
    s_cfgPhantomArmed = true;
    s_cfgPhantomArmMs = now;
    s_cfgPhantomArmHa = static_cast<int16_t>(haIdx);
    syncActiveTabImmediate();
    requestImmediateRefresh();
    return;
  }
  s_cfgPhantomArmed = false;
  if (mixerStateLock(20)) {
    mixerStateUnsafe().headampPhantom[haIdx] = !curPh;
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_PHANTOM;
  cmd.idx = static_cast<int16_t>(haIdx);
  cmd.bval = !curPh;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG | DIRTY_OVERVIEW);
}

void onCfgInvertClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  bool nextInv = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_AUX) {
      nextInv = !st.auxCfg[idx].invert;
      st.auxCfg[idx].invert = nextInv;
    } else {
      nextInv = !st.chCfg[idx].invert;
      st.chCfg[idx].invert = nextInv;
    }
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_INVERT;
  cmd.kind = k;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.bval = nextInv;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG | DIRTY_OVERVIEW);
}

void onCfgHpfOnClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  bool nextOn = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_CH) {
      nextOn = !st.chCfg[idx].hpon;
      st.chCfg[idx].hpon = nextOn;
    }
    mixerStateUnlock();
  }
  if (k != STRIP_CH) return;
  AppCommand cmd;
  cmd.type = CMD_SET_HPON;
  cmd.kind = STRIP_CH;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.bval = nextOn;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG);
}

void applyCfgHpfVal(int32_t sliderVal, bool syncUiNow) {
  if (sliderVal < 0) sliderVal = 0;
  if (sliderVal > 1000) sliderVal = 1000;
  const float f = static_cast<float>(sliderVal) / 1000.0f;
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_CH) st.chCfg[idx].hpf = f;
    mixerStateUnlock();
  }
  if (k != STRIP_CH) return;
  char hzBuf[16];
  x32_format_hpf_hz(f, hzBuf, sizeof(hzBuf));
  s_cfgCache.hpfInt = sliderVal;
  setLabelTextIfChanged(s_cfgHpfFreqLbl, hzBuf);
  AppCommand cmd;
  cmd.type = CMD_SET_HPF;
  cmd.kind = STRIP_CH;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.fval = f;
  enqueueAppCommand(cmd);
  if (syncUiNow) {
    finishCfgAction(DIRTY_CONFIG);
  } else {
    mixerMarkDirty(DIRTY_CONFIG);
  }
}

void onCfgHpfSliderChanged(lv_event_t *e) {
  applyCfgHpfVal(lv_slider_get_value(lv_event_get_target(e)), false);
}

void onCfgHpfStep(lv_event_t *e) {
  const int dir = (reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) >= 0) ? 1 : -1;
  applyCfgHpfVal(lv_slider_get_value(s_cfgHpfSlider) + dir * 25, true);
}

void onCfgHpfPresetClick(lv_event_t *e) {
  const int hz = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const float f = x32_hz_to_hpf(static_cast<float>(hz));
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_CH) {
      st.chCfg[idx].hpf = f;
      st.chCfg[idx].hpon = true;
    }
    mixerStateUnlock();
  }
  if (k != STRIP_CH) return;
  AppCommand cmdOn;
  cmdOn.type = CMD_SET_HPON;
  cmdOn.kind = STRIP_CH;
  cmdOn.idx = static_cast<int16_t>(idx);
  cmdOn.bval = true;
  enqueueAppCommand(cmdOn);
  AppCommand cmdHz;
  cmdHz.type = CMD_SET_HPF;
  cmdHz.kind = STRIP_CH;
  cmdHz.idx = static_cast<int16_t>(idx);
  cmdHz.fval = f;
  enqueueAppCommand(cmdHz);
  finishCfgAction(DIRTY_CONFIG);
}

void applyCfgPanPct(int pct, bool syncUiNow) {
  if (pct < -100) pct = -100;
  if (pct > 100) pct = 100;
  const float f = x32_pct_to_pan(pct);
  StripKind k = STRIP_CH;
  int idx = 0;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_AUX) st.auxCfg[idx].pan = f;
    else st.chCfg[idx].pan = f;
    mixerStateUnlock();
  }
  char panBuf[12];
  x32_format_pan(f, panBuf, sizeof(panBuf));
  s_cfgCache.panPct = pct;
  setLabelTextIfChanged(s_cfgPanValLbl, panBuf);
  AppCommand cmd;
  cmd.type = CMD_SET_PAN;
  cmd.kind = k;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.fval = f;
  enqueueAppCommand(cmd);
  if (syncUiNow) {
    finishCfgAction(DIRTY_CONFIG);
  } else {
    mixerMarkDirty(DIRTY_CONFIG);
  }
}

void onCfgPanSliderChanged(lv_event_t *e) {
  applyCfgPanPct(static_cast<int>(lv_slider_get_value(lv_event_get_target(e))), false);
}

void onCfgPanStep(lv_event_t *e) {
  const int dir = (reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) >= 0) ? 1 : -1;
  applyCfgPanPct(static_cast<int>(lv_slider_get_value(s_cfgPanSlider)) + dir * 2, true);
}

void onCfgPanCenterClick(lv_event_t *e) {
  (void)e;
  applyCfgPanPct(0, true);
}

void onCfgMainStClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  bool nextSt = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_AUX) {
      nextSt = !st.auxCfg[idx].mainSt;
      st.auxCfg[idx].mainSt = nextSt;
    } else {
      nextSt = !st.chCfg[idx].mainSt;
      st.chCfg[idx].mainSt = nextSt;
    }
    mixerStateUnlock();
  }
  AppCommand cmd;
  cmd.type = CMD_SET_MAIN_ST;
  cmd.kind = k;
  cmd.idx = static_cast<int16_t>(idx);
  cmd.bval = nextSt;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG);
}

void onCfgLinkClick(lv_event_t *e) {
  (void)e;
  StripKind k = STRIP_CH;
  int idx = 0;
  bool nextLink = false;
  if (mixerStateLock(20)) {
    MixerState &st = mixerStateUnsafe();
    getActiveConfigTarget(st, &k, &idx);
    if (k == STRIP_CH) {
      const int pair = idx / 2;
      nextLink = !st.chLinks[pair];
      st.chLinks[pair] = nextLink;
    }
    mixerStateUnlock();
  }
  if (k != STRIP_CH) return;
  AppCommand cmd;
  cmd.type = CMD_SET_CH_LINK;
  cmd.idx = static_cast<int16_t>(idx / 2);
  cmd.bval = nextLink;
  enqueueAppCommand(cmd);
  finishCfgAction(DIRTY_CONFIG | DIRTY_MATRIX);
}

// ============================================================
// UI Builders
// ============================================================

void buildTopBar(lv_obj_t *scr) {
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, 800, 46);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x11161D), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);

  s_connPill = lv_btn_create(bar);
  lv_obj_set_pos(s_connPill, 6, 7);
  lv_obj_set_size(s_connPill, 100, 32);
  lv_obj_set_style_radius(s_connPill, 16, 0);
  lv_obj_set_style_border_width(s_connPill, 1, 0);
  lv_obj_set_style_shadow_width(s_connPill, 0, 0);
  lv_obj_set_style_pad_all(s_connPill, 0, 0);
  lv_obj_add_event_cb(s_connPill, onConnPillClick, LV_EVENT_PRESSED, nullptr);
  s_connLbl = lv_label_create(s_connPill);
  lv_obj_set_style_text_font(s_connLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_width(s_connLbl, 88);
  lv_label_set_long_mode(s_connLbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_connLbl, "NO X32");
  lv_obj_center(s_connLbl);

  static constexpr const char *kTabNames[TAB_COUNT] = {
      "Overview", "Config", "Scribble", "DCA/Mutes", "Faders", "Presets", "Wi-Fi/X32"};
  static constexpr int kTabWidths[TAB_COUNT] = {64, 50, 58, 72, 52, 56, 64};
  int x = 110;
  for (int i = 0; i < TAB_COUNT; ++i) {
    const int w = kTabWidths[i];
    s_tabBtns[i] = makeBtn(bar, kTabNames[i], w, 34, (i == 0) ? COL_ACCENT : COL_PANEL2);
    lv_obj_set_pos(s_tabBtns[i], x, 6);
    lv_obj_set_style_text_font(lv_obj_get_child(s_tabBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_tabBtns[i], onTabBtnClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    x += w + 2;
  }

  s_clearSoloBtn = makeBtn(bar, "CLR SOLO", 82, 32, 0x9E6A03);
  lv_obj_set_pos(s_clearSoloBtn, 544, 7);
  lv_obj_set_style_border_color(s_clearSoloBtn, lv_color_hex(0xF2CC60), 0);
  lv_obj_set_style_text_font(lv_obj_get_child(s_clearSoloBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_clearSoloBtn, onClearSoloClick, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_clearSoloBtn, LV_OBJ_FLAG_HIDDEN);

  s_netLbl = lv_label_create(bar);
  lv_obj_set_pos(s_netLbl, 632, 15);
  lv_obj_set_size(s_netLbl, 162, 18);
  lv_label_set_long_mode(s_netLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s_netLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_netLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_netLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(s_netLbl, "AP 192.168.4.1");
}

void buildOverviewTab(lv_obj_t *parent) {
  lv_obj_t *tb = lv_obj_create(parent);
  lv_obj_set_pos(tb, 4, 4);
  lv_obj_set_size(tb, 544, 34);
  lv_obj_clear_flag(tb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tb, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(tb, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(tb, 6, 0);
  lv_obj_set_style_pad_all(tb, 0, 0);

  s_ovStatusLbl = lv_label_create(tb);
  lv_obj_set_pos(s_ovStatusLbl, 8, 9);
  lv_obj_set_size(s_ovStatusLbl, 290, 18);
  lv_label_set_long_mode(s_ovStatusLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(s_ovStatusLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_ovStatusLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(s_ovStatusLbl, "32-Ch Bridge (hold tile to rename)");

  s_ovModeLbl = lv_label_create(tb);
  lv_obj_set_pos(s_ovModeLbl, 304, 9);
  lv_obj_set_size(s_ovModeLbl, 180, 18);
  lv_obj_set_style_text_align(s_ovModeLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_ovModeLbl, &lv_font_montserrat_12, 0);
  lv_label_set_text(s_ovModeLbl, "Tap: Inspect");

  s_ovModeSw = lv_switch_create(tb);
  lv_obj_set_pos(s_ovModeSw, 492, 5);
  lv_obj_set_size(s_ovModeSw, 44, 24);
  lv_obj_add_event_cb(s_ovModeSw, onOvModeChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // 4x8 Channel Tile Grid
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const int row = i / 8;
    const int col = i % 8;
    const int x = 4 + col * 68;
    const int y = 42 + row * 97;

    lv_obj_t *tile = lv_obj_create(parent);
    s_ovTile[i] = tile;
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, 65, 93);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 0, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_add_event_cb(tile, onOvTileShortClick, LV_EVENT_SHORT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(tile, onOvTileLongPress, LV_EVENT_LONG_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    lv_obj_t *hd = lv_obj_create(tile);
    s_ovTileHead[i] = hd;
    lv_obj_set_pos(hd, 0, 0);
    lv_obj_set_size(hd, 63, 42);
    lv_obj_clear_flag(hd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(hd, 0, 0);
    lv_obj_set_style_shadow_width(hd, 0, 0);
    lv_obj_set_style_pad_all(hd, 0, 0);

    s_ovTileNum[i] = lv_label_create(hd);
    lv_obj_set_pos(s_ovTileNum[i], 3, 2);
    lv_obj_set_style_text_font(s_ovTileNum[i], &lv_font_montserrat_12, 0);
    char numStr[8];
    std::snprintf(numStr, sizeof(numStr), "%02d", i + 1);
    lv_label_set_text(s_ovTileNum[i], numStr);

    s_ovTileSrc[i] = lv_label_create(hd);
    lv_obj_set_pos(s_ovTileSrc[i], 21, 2);
    lv_obj_set_size(s_ovTileSrc[i], 39, 14);
    lv_label_set_long_mode(s_ovTileSrc[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_ovTileSrc[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(s_ovTileSrc[i], &lv_font_montserrat_12, 0);

    s_ovTileName[i] = lv_label_create(hd);
    lv_obj_set_pos(s_ovTileName[i], 3, 20);
    lv_obj_set_size(s_ovTileName[i], 57, 18);
    lv_label_set_long_mode(s_ovTileName[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_ovTileName[i], &lv_font_montserrat_12, 0);

    s_ovTileVu[i] = lv_bar_create(tile);
    lv_obj_set_pos(s_ovTileVu[i], 4, 48);
    lv_obj_set_size(s_ovTileVu[i], 55, 8);
    lv_bar_set_range(s_ovTileVu[i], 0, 100);
    lv_obj_clear_flag(s_ovTileVu[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_ovTileVu[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ovTileVu[i], 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ovTileVu[i], lv_color_hex(0x090C10), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ovTileVu[i], lv_color_hex(COL_GREEN), LV_PART_INDICATOR);

    s_ovTileMute[i] = lv_label_create(tile);
    lv_obj_set_pos(s_ovTileMute[i], 5, 65);
    lv_obj_set_size(s_ovTileMute[i], 24, 18);
    lv_obj_set_style_text_align(s_ovTileMute[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(s_ovTileMute[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ovTileMute[i], 0, 0);
    lv_obj_set_style_text_font(s_ovTileMute[i], &lv_font_montserrat_12, 0);
    lv_label_set_text(s_ovTileMute[i], "M");

    s_ovTileSolo[i] = lv_label_create(tile);
    lv_obj_set_pos(s_ovTileSolo[i], 34, 65);
    lv_obj_set_size(s_ovTileSolo[i], 24, 18);
    lv_obj_set_style_text_align(s_ovTileSolo[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(s_ovTileSolo[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ovTileSolo[i], 0, 0);
    lv_obj_set_style_text_font(s_ovTileSolo[i], &lv_font_montserrat_12, 0);
    lv_label_set_text(s_ovTileSolo[i], "S");
  }

  // Right Companion Column (x=554, w=242)
  lv_obj_t *selCard = lv_obj_create(parent);
  lv_obj_set_pos(selCard, 554, 4);
  lv_obj_set_size(selCard, 242, 140);
  lv_obj_clear_flag(selCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(selCard, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(selCard, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(selCard, 7, 0);
  lv_obj_set_style_pad_all(selCard, 0, 0);

  lv_obj_t *flLbl = lv_label_create(selCard);
  lv_obj_set_pos(flLbl, 8, 5);
  lv_obj_set_style_text_font(flLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(flLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(flLbl, "FOLLOW X32 [SELECT]");

  s_selFollowSw = lv_switch_create(selCard);
  lv_obj_set_pos(s_selFollowSw, 192, 3);
  lv_obj_set_size(s_selFollowSw, 40, 20);
  lv_obj_add_event_cb(s_selFollowSw, onFollowChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  s_selBanner = lv_obj_create(selCard);
  lv_obj_set_pos(s_selBanner, 6, 26);
  lv_obj_set_size(s_selBanner, 228, 40);
  lv_obj_clear_flag(s_selBanner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(s_selBanner, 5, 0);
  lv_obj_set_style_pad_all(s_selBanner, 0, 0);

  s_selTagLbl = lv_label_create(s_selBanner);
  lv_obj_set_pos(s_selTagLbl, 6, 2);
  lv_obj_set_size(s_selTagLbl, 134, 15);
  lv_label_set_long_mode(s_selTagLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(s_selTagLbl, &lv_font_montserrat_12, 0);

  s_selDbLbl = lv_label_create(s_selBanner);
  lv_obj_set_pos(s_selDbLbl, 140, 2);
  lv_obj_set_size(s_selDbLbl, 82, 16);
  lv_obj_set_style_text_align(s_selDbLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_selDbLbl, &lv_font_montserrat_12, 0);

  s_selNameLbl = lv_label_create(s_selBanner);
  lv_obj_set_pos(s_selNameLbl, 6, 17);
  lv_obj_set_size(s_selNameLbl, 216, 20);
  lv_obj_set_style_text_font(s_selNameLbl, &lv_font_montserrat_16, 0);

  lv_obj_t *selPrevBtn = makeBtn(selCard, "<", 32, 26, COL_PANEL2);
  lv_obj_set_pos(selPrevBtn, 6, 71);
  lv_obj_add_event_cb(selPrevBtn, onSelLevelStep, LV_EVENT_SHORT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
  lv_obj_add_event_cb(selPrevBtn, onSelLevelStep, LV_EVENT_LONG_PRESSED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
  lv_obj_add_event_cb(selPrevBtn, onSelLevelStep, LV_EVENT_LONG_PRESSED_REPEAT,
                      reinterpret_cast<void *>(static_cast<intptr_t>(-1)));

  s_selSlider = lv_slider_create(selCard);
  lv_obj_set_pos(s_selSlider, 46, 78);
  lv_obj_set_size(s_selSlider, 148, 12);
  lv_slider_set_range(s_selSlider, 0, 1000);
  lv_obj_set_ext_click_area(s_selSlider, 10);
  lv_obj_set_style_bg_color(s_selSlider, lv_color_hex(0x090C10), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_selSlider, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_selSlider, lv_color_hex(COL_TEXT), LV_PART_KNOB);
  lv_obj_set_style_radius(s_selSlider, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_selSlider, 0, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_selSlider, 0, LV_PART_KNOB);
  lv_obj_add_event_cb(s_selSlider, onSelSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *selNextBtn = makeBtn(selCard, ">", 32, 26, COL_PANEL2);
  lv_obj_set_pos(selNextBtn, 202, 71);
  lv_obj_add_event_cb(selNextBtn, onSelLevelStep, LV_EVENT_SHORT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(1)));
  lv_obj_add_event_cb(selNextBtn, onSelLevelStep, LV_EVENT_LONG_PRESSED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(1)));
  lv_obj_add_event_cb(selNextBtn, onSelLevelStep, LV_EVENT_LONG_PRESSED_REPEAT,
                      reinterpret_cast<void *>(static_cast<intptr_t>(1)));

  s_selMuteBtn = makeBtn(selCard, "MUTE", 52, 30, COL_PANEL2);
  lv_obj_set_pos(s_selMuteBtn, 6, 103);
  lv_obj_set_style_text_font(lv_obj_get_child(s_selMuteBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_selMuteBtn, onSelMuteClick, LV_EVENT_CLICKED, nullptr);

  s_selSoloBtn = makeBtn(selCard, "SOLO", 52, 30, COL_PANEL2);
  lv_obj_set_pos(s_selSoloBtn, 62, 103);
  lv_obj_set_style_text_font(lv_obj_get_child(s_selSoloBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_selSoloBtn, onSelSoloClick, LV_EVENT_CLICKED, nullptr);

  s_selCfgBtn = makeBtn(selCard, "Config", 56, 30, COL_PANEL2);
  lv_obj_set_pos(s_selCfgBtn, 118, 103);
  lv_obj_set_style_border_color(s_selCfgBtn, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_text_font(lv_obj_get_child(s_selCfgBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_selCfgBtn, onJumpConfigSelClick, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *renBtn = makeBtn(selCard, "Name >", 58, 30, COL_ACCENT);
  lv_obj_set_pos(renBtn, 178, 103);
  lv_obj_set_style_text_font(lv_obj_get_child(renBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(renBtn, onJumpRenameSelClick, LV_EVENT_CLICKED, nullptr);

  // DCA 1-8 Mini Grid
  lv_obj_t *dcaCard = lv_obj_create(parent);
  lv_obj_set_pos(dcaCard, 554, 148);
  lv_obj_set_size(dcaCard, 242, 116);
  lv_obj_clear_flag(dcaCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(dcaCard, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(dcaCard, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(dcaCard, 7, 0);
  lv_obj_set_style_pad_all(dcaCard, 0, 0);

  lv_obj_t *dcaHdr = lv_label_create(dcaCard);
  lv_obj_set_pos(dcaHdr, 8, 3);
  lv_obj_set_style_text_font(dcaHdr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(dcaHdr, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(dcaHdr, "DCA GROUPS (TAP MUTE / HOLD EDIT)");

  for (int i = 0; i < NUM_DCAS; ++i) {
    int col = i % 2;
    int row = i / 2;
    lv_obj_t *b = makeBtn(dcaCard, nullptr, 110, 21, COL_PANEL2);
    s_ovDcaBtn[i] = b;
    lv_obj_set_pos(b, 8 + col * 114, 20 + row * 23);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_add_event_cb(b, onOvDcaClick, LV_EVENT_SHORT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    lv_obj_add_event_cb(b, onOvDcaLongPress, LV_EVENT_LONG_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    s_ovDcaDot[i] = lv_obj_create(b);
    lv_obj_set_pos(s_ovDcaDot[i], 4, 5);
    lv_obj_set_size(s_ovDcaDot[i], 9, 9);
    lv_obj_set_style_radius(s_ovDcaDot[i], 0, 0);
    lv_obj_set_style_border_width(s_ovDcaDot[i], 0, 0);
    lv_obj_clear_flag(s_ovDcaDot[i], LV_OBJ_FLAG_CLICKABLE);

    s_ovDcaLbl[i] = lv_label_create(b);
    lv_obj_set_pos(s_ovDcaLbl[i], 16, 1);
    lv_obj_set_size(s_ovDcaLbl[i], 88, 16);
    lv_label_set_long_mode(s_ovDcaLbl[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_ovDcaLbl[i], &lv_font_montserrat_12, 0);
  }

  // Mute Groups 1-6
  lv_obj_t *mgCard = lv_obj_create(parent);
  lv_obj_set_pos(mgCard, 554, 268);
  lv_obj_set_size(mgCard, 242, 82);
  lv_obj_clear_flag(mgCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(mgCard, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(mgCard, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(mgCard, 7, 0);
  lv_obj_set_style_pad_all(mgCard, 0, 0);

  lv_obj_t *mgHdr = lv_label_create(mgCard);
  lv_obj_set_pos(mgHdr, 8, 3);
  lv_obj_set_style_text_font(mgHdr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(mgHdr, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(mgHdr, "MUTE GROUPS (1-6)");

  for (int i = 0; i < NUM_MUTE_GRPS; ++i) {
    int col = i % 3;
    int row = i / 3;
    char lbl[8];
    std::snprintf(lbl, sizeof(lbl), "MG %d", i + 1);
    s_ovMgBtn[i] = makeBtn(mgCard, lbl, 72, 27, COL_PANEL2);
    lv_obj_set_pos(s_ovMgBtn[i], 8 + col * 76, 20 + row * 29);
    lv_obj_set_style_radius(s_ovMgBtn[i], 0, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(s_ovMgBtn[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_ovMgBtn[i], onOvMgClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  // Main LR Mini Card
  lv_obj_t *mainCard = lv_obj_create(parent);
  lv_obj_set_pos(mainCard, 554, 354);
  lv_obj_set_size(mainCard, 242, 76);
  lv_obj_clear_flag(mainCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(mainCard, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(mainCard, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(mainCard, 7, 0);
  lv_obj_set_style_pad_all(mainCard, 0, 0);

  lv_obj_t *mTitle = lv_label_create(mainCard);
  lv_obj_set_pos(mTitle, 8, 4);
  lv_obj_set_style_text_font(mTitle, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(mTitle, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(mTitle, "MAIN LR");

  s_ovMainDbLbl = lv_label_create(mainCard);
  lv_obj_set_pos(s_ovMainDbLbl, 140, 4);
  lv_obj_set_size(s_ovMainDbLbl, 92, 16);
  lv_obj_set_style_text_align(s_ovMainDbLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_ovMainDbLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_ovMainDbLbl, lv_color_hex(COL_CYAN), 0);

  s_ovMainVu = lv_bar_create(mainCard);
  lv_obj_set_pos(s_ovMainVu, 8, 22);
  lv_obj_set_size(s_ovMainVu, 224, 7);
  lv_bar_set_range(s_ovMainVu, 0, 100);
  lv_obj_set_style_radius(s_ovMainVu, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_ovMainVu, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_ovMainVu, lv_color_hex(0x090C10), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_ovMainVu, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);

  s_ovMainSlider = lv_slider_create(mainCard);
  lv_obj_set_pos(s_ovMainSlider, 12, 43);
  lv_obj_set_size(s_ovMainSlider, 152, 14);
  lv_slider_set_range(s_ovMainSlider, 0, 1000);
  lv_obj_set_ext_click_area(s_ovMainSlider, 12);
  lv_obj_set_style_bg_color(s_ovMainSlider, lv_color_hex(COL_AMBER), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_ovMainSlider, lv_color_hex(COL_TEXT), LV_PART_KNOB);
  lv_obj_set_style_radius(s_ovMainSlider, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_ovMainSlider, 0, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_ovMainSlider, 0, LV_PART_KNOB);
  lv_obj_add_event_cb(s_ovMainSlider, onOvMainSlider, LV_EVENT_VALUE_CHANGED, nullptr);

  s_ovMainMuteBtn = makeBtn(mainCard, "MUTE", 58, 28, COL_PANEL2);
  lv_obj_set_pos(s_ovMainMuteBtn, 174, 38);
  lv_obj_add_event_cb(s_ovMainMuteBtn, onOvMainMute, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t *makeCfgSlider(lv_obj_t *parent, int x, int y, int w, int minV, int maxV, uint32_t indCol,
                        lv_event_cb_t cb) {
  lv_obj_t *sl = lv_slider_create(parent);
  lv_obj_set_pos(sl, x, y);
  lv_obj_set_size(sl, w, 12);
  lv_slider_set_range(sl, minV, maxV);
  lv_obj_set_ext_click_area(sl, 10);
  lv_obj_set_style_bg_color(sl, lv_color_hex(0x090C10), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, lv_color_hex(indCol), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, lv_color_hex(COL_TEXT), LV_PART_KNOB);
  lv_obj_set_style_radius(sl, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(sl, 0, LV_PART_INDICATOR);
  lv_obj_set_style_radius(sl, 0, LV_PART_KNOB);
  lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return sl;
}

lv_obj_t *makeCfgCard(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(c, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(c, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_radius(c, 6, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  return c;
}

void buildConfigTab(lv_obj_t *parent) {
  // Row 1 (y=4, h=34): Category buttons + < Banner > + Name> + 1:1 Src + Follow
  static constexpr const char *kCatNames[3] = {"Ch 1-16", "Ch 17-32", "Aux 1-8"};
  static constexpr int kCatX[3] = {4, 80, 162};
  static constexpr int kCatW[3] = {72, 78, 68};
  for (int i = 0; i < 3; ++i) {
    s_cfgCatBtns[i] = makeBtn(parent, kCatNames[i], kCatW[i], 34, COL_PANEL2);
    lv_obj_set_pos(s_cfgCatBtns[i], kCatX[i], 4);
    lv_obj_set_style_text_font(lv_obj_get_child(s_cfgCatBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_cfgCatBtns[i], onCfgCatClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  lv_obj_t *prevStripBtn = makeBtn(parent, "<", 34, 34, COL_PANEL2);
  lv_obj_set_pos(prevStripBtn, 236, 4);
  attachRepeatCallbacks(prevStripBtn, onCfgStepClick, -1);

  s_cfgBanner = lv_obj_create(parent);
  lv_obj_set_pos(s_cfgBanner, 274, 4);
  lv_obj_set_size(s_cfgBanner, 210, 34);
  lv_obj_clear_flag(s_cfgBanner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(s_cfgBanner, 5, 0);
  lv_obj_set_style_pad_all(s_cfgBanner, 0, 0);

  s_cfgTagLbl = lv_label_create(s_cfgBanner);
  lv_obj_set_pos(s_cfgTagLbl, 6, 1);
  lv_obj_set_size(s_cfgTagLbl, 198, 14);
  lv_label_set_long_mode(s_cfgTagLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(s_cfgTagLbl, &lv_font_montserrat_12, 0);

  s_cfgNameLbl = lv_label_create(s_cfgBanner);
  lv_obj_set_pos(s_cfgNameLbl, 6, 15);
  lv_obj_set_size(s_cfgNameLbl, 198, 17);
  lv_label_set_long_mode(s_cfgNameLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(s_cfgNameLbl, &lv_font_montserrat_14, 0);

  lv_obj_t *nextStripBtn = makeBtn(parent, ">", 34, 34, COL_PANEL2);
  lv_obj_set_pos(nextStripBtn, 488, 4);
  attachRepeatCallbacks(nextStripBtn, onCfgStepClick, 1);

  lv_obj_t *nameBtn = makeBtn(parent, "Name >", 66, 34, COL_ACCENT);
  lv_obj_set_pos(nameBtn, 528, 4);
  lv_obj_set_style_text_font(lv_obj_get_child(nameBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(nameBtn, onCfgJumpNameClick, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *defTopBtn = makeBtn(parent, "1:1 Src", 66, 34, COL_PANEL2);
  lv_obj_set_pos(defTopBtn, 598, 4);
  lv_obj_set_style_text_font(lv_obj_get_child(defTopBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(defTopBtn, onCfgSrcDefClick, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *flLbl = lv_label_create(parent);
  lv_obj_set_pos(flLbl, 672, 13);
  lv_obj_set_style_text_font(flLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(flLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(flLbl, "Follow");

  s_cfgFollowSw = lv_switch_create(parent);
  lv_obj_set_pos(s_cfgFollowSw, 742, 9);
  lv_obj_set_size(s_cfgFollowSw, 48, 24);
  lv_obj_add_event_cb(s_cfgFollowSw, onFollowChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // Row 2 (y=41, h=28): 16 Quick Strip Pills
  for (int i = 0; i < 16; ++i) {
    s_cfgPickBtn[i] = makeBtn(parent, nullptr, 47, 28, COL_PANEL2);
    lv_obj_set_pos(s_cfgPickBtn[i], 4 + i * 49, 41);
    lv_obj_set_style_radius(s_cfgPickBtn[i], 3, 0);
    lv_obj_set_style_border_width(s_cfgPickBtn[i], 2, 0);
    s_cfgPickLbl[i] = lv_label_create(s_cfgPickBtn[i]);
    lv_obj_set_size(s_cfgPickLbl[i], 43, 16);
    lv_obj_set_style_text_align(s_cfgPickLbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_cfgPickLbl[i], &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_cfgPickLbl[i], LV_LABEL_LONG_CLIP);
    lv_obj_center(s_cfgPickLbl[i]);
    lv_obj_add_event_cb(s_cfgPickBtn[i], onCfgPickClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  // Left Card: Input Source Patch (x=4, y=73, w=412, h=357)
  lv_obj_t *lc = makeCfgCard(parent, 4, 73, 412, 357);

  lv_obj_t *srcHdr = lv_label_create(lc);
  lv_obj_set_pos(srcHdr, 8, 4);
  lv_obj_set_style_text_font(srcHdr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(srcHdr, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(srcHdr, "INPUT SOURCE");

  s_cfgSrcSubLbl = lv_label_create(lc);
  lv_obj_set_pos(s_cfgSrcSubLbl, 118, 4);
  lv_obj_set_size(s_cfgSrcSubLbl, 284, 16);
  lv_label_set_long_mode(s_cfgSrcSubLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s_cfgSrcSubLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_cfgSrcSubLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgSrcSubLbl, lv_color_hex(COL_CYAN), 0);

  static constexpr const char *kSrcTabNames[4] = {"In 1-16", "In 17-32", "Aux/USB/FX", "Bus 1-16"};
  for (int t = 0; t < 4; ++t) {
    s_cfgSrcTabBtns[t] = makeBtn(lc, kSrcTabNames[t], 96, 28, COL_PANEL2);
    lv_obj_set_pos(s_cfgSrcTabBtns[t], 8 + t * 99, 24);
    lv_obj_set_style_text_font(lv_obj_get_child(s_cfgSrcTabBtns[t], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_cfgSrcTabBtns[t], onCfgSrcTabClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(t)));
  }

  for (int s = 0; s < 16; ++s) {
    const int col = s % 4;
    const int row = s / 4;
    lv_obj_t *b = makeBtn(lc, nullptr, 96, 52, COL_PANEL2);
    s_cfgSrcGridBtns[s] = b;
    lv_obj_set_pos(b, 8 + col * 99, 58 + row * 56);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_t *l = lv_label_create(b);
    s_cfgSrcGridLbls[s] = l;
    lv_obj_set_width(l, 90);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, onCfgSrcBtnClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(s)));
  }

  s_cfgSrcOffBtn = makeBtn(lc, "OFF (Unpatch)", 194, 32, COL_PANEL2);
  lv_obj_set_pos(s_cfgSrcOffBtn, 8, 286);
  lv_obj_set_style_text_font(lv_obj_get_child(s_cfgSrcOffBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgSrcOffBtn, onCfgSrcOffClick, LV_EVENT_CLICKED, nullptr);

  s_cfgSrcDefBtn = makeBtn(lc, "1:1 Default", 196, 32, COL_PANEL2);
  lv_obj_set_pos(s_cfgSrcDefBtn, 208, 286);
  lv_obj_set_style_text_font(lv_obj_get_child(s_cfgSrcDefBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgSrcDefBtn, onCfgSrcDefClick, LV_EVENT_CLICKED, nullptr);

  s_cfgWarnLbl = lv_label_create(lc);
  lv_obj_set_pos(s_cfgWarnLbl, 8, 324);
  lv_obj_set_size(s_cfgWarnLbl, 396, 26);
  lv_label_set_long_mode(s_cfgWarnLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(s_cfgWarnLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgWarnLbl, lv_color_hex(COL_MUTED), 0);

  // Right Card: Preamp, HPF & Main/Link (x=420, y=73, w=376, h=357)
  lv_obj_t *rc = makeCfgCard(parent, 420, 73, 376, 357);

  s_cfgPreTitleLbl = lv_label_create(rc);
  lv_obj_set_pos(s_cfgPreTitleLbl, 8, 6);
  lv_obj_set_size(s_cfgPreTitleLbl, 210, 16);
  lv_obj_set_style_text_font(s_cfgPreTitleLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgPreTitleLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(s_cfgPreTitleLbl, "HA GAIN");

  s_cfgGainDbLbl = lv_label_create(rc);
  lv_obj_set_pos(s_cfgGainDbLbl, 224, 6);
  lv_obj_set_size(s_cfgGainDbLbl, 142, 16);
  lv_obj_set_style_text_align(s_cfgGainDbLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_cfgGainDbLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgGainDbLbl, lv_color_hex(COL_CYAN), 0);

  lv_obj_t *gPrevBtn = makeBtn(rc, "<", 36, 32, COL_PANEL2);
  lv_obj_set_pos(gPrevBtn, 8, 26);
  attachRepeatCallbacks(gPrevBtn, onCfgGainStep, -1);

  s_cfgGainSlider = makeCfgSlider(rc, 52, 36, 272, 0, 144, COL_CYAN, onCfgGainSliderChanged);

  lv_obj_t *gNextBtn = makeBtn(rc, ">", 36, 32, COL_PANEL2);
  lv_obj_set_pos(gNextBtn, 332, 26);
  attachRepeatCallbacks(gNextBtn, onCfgGainStep, 1);

  s_cfgPhantomBtn = makeBtn(rc, "+48V OFF", 124, 34, COL_PANEL2);
  lv_obj_set_pos(s_cfgPhantomBtn, 8, 64);
  s_cfgPhantomLbl = lv_obj_get_child(s_cfgPhantomBtn, 0);
  lv_obj_set_style_text_font(s_cfgPhantomLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgPhantomBtn, onCfgPhantomClick, LV_EVENT_CLICKED, nullptr);

  s_cfgPolBtn = makeBtn(rc, "POL: NORM", 112, 34, COL_PANEL2);
  lv_obj_set_pos(s_cfgPolBtn, 138, 64);
  s_cfgPolLbl = lv_obj_get_child(s_cfgPolBtn, 0);
  lv_obj_set_style_text_font(s_cfgPolLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgPolBtn, onCfgInvertClick, LV_EVENT_CLICKED, nullptr);

  s_cfgModeTrimBtn = makeBtn(rc, "Show Trim", 112, 34, COL_PANEL2);
  lv_obj_set_pos(s_cfgModeTrimBtn, 256, 64);
  s_cfgModeTrimLbl = lv_obj_get_child(s_cfgModeTrimBtn, 0);
  lv_obj_set_style_text_font(s_cfgModeTrimLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgModeTrimBtn, onCfgTrimModeClick, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *div1 = lv_obj_create(rc);
  lv_obj_set_pos(div1, 8, 104);
  lv_obj_set_size(div1, 358, 1);
  lv_obj_set_style_bg_color(div1, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(div1, 0, 0);

  s_cfgHpfBox = lv_obj_create(rc);
  lv_obj_set_pos(s_cfgHpfBox, 0, 108);
  lv_obj_set_size(s_cfgHpfBox, 374, 120);
  lv_obj_clear_flag(s_cfgHpfBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_cfgHpfBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_cfgHpfBox, 0, 0);
  lv_obj_set_style_pad_all(s_cfgHpfBox, 0, 0);

  s_cfgHpfOnBtn = makeBtn(s_cfgHpfBox, "LOW CUT: OFF", 132, 30, COL_PANEL2);
  lv_obj_set_pos(s_cfgHpfOnBtn, 8, 4);
  s_cfgHpfOnLbl = lv_obj_get_child(s_cfgHpfOnBtn, 0);
  lv_obj_set_style_text_font(s_cfgHpfOnLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgHpfOnBtn, onCfgHpfOnClick, LV_EVENT_CLICKED, nullptr);

  s_cfgHpfFreqLbl = lv_label_create(s_cfgHpfBox);
  lv_obj_set_pos(s_cfgHpfFreqLbl, 236, 10);
  lv_obj_set_size(s_cfgHpfFreqLbl, 130, 18);
  lv_obj_set_style_text_align(s_cfgHpfFreqLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_cfgHpfFreqLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgHpfFreqLbl, lv_color_hex(COL_CYAN), 0);

  lv_obj_t *hPrevBtn = makeBtn(s_cfgHpfBox, "<", 36, 30, COL_PANEL2);
  lv_obj_set_pos(hPrevBtn, 8, 40);
  attachRepeatCallbacks(hPrevBtn, onCfgHpfStep, -1);

  s_cfgHpfSlider = makeCfgSlider(s_cfgHpfBox, 52, 49, 272, 0, 1000, COL_GREEN, onCfgHpfSliderChanged);

  lv_obj_t *hNextBtn = makeBtn(s_cfgHpfBox, ">", 36, 30, COL_PANEL2);
  lv_obj_set_pos(hNextBtn, 332, 40);
  attachRepeatCallbacks(hNextBtn, onCfgHpfStep, 1);

  static constexpr int kHpfPresets[4] = {60, 80, 100, 120};
  static constexpr const char *kHpfPresetNames[4] = {"60Hz", "80Hz", "100Hz", "120Hz"};
  for (int i = 0; i < 4; ++i) {
    s_cfgHpfPreBtns[i] = makeBtn(s_cfgHpfBox, kHpfPresetNames[i], 86, 28, COL_PANEL2);
    lv_obj_set_pos(s_cfgHpfPreBtns[i], 8 + i * 90, 76);
    lv_obj_set_style_text_font(lv_obj_get_child(s_cfgHpfPreBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_cfgHpfPreBtns[i], onCfgHpfPresetClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(kHpfPresets[i])));
  }

  s_cfgHpfEmptyLbl = lv_label_create(rc);
  lv_obj_set_pos(s_cfgHpfEmptyLbl, 16, 156);
  lv_obj_set_size(s_cfgHpfEmptyLbl, 342, 24);
  lv_obj_set_style_text_align(s_cfgHpfEmptyLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_cfgHpfEmptyLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgHpfEmptyLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(s_cfgHpfEmptyLbl, "Low Cut (HPF) is available on Ch 01-32");
  lv_obj_add_flag(s_cfgHpfEmptyLbl, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *div2 = lv_obj_create(rc);
  lv_obj_set_pos(div2, 8, 232);
  lv_obj_set_size(div2, 358, 1);
  lv_obj_set_style_bg_color(div2, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(div2, 0, 0);

  lv_obj_t *panHdr = lv_label_create(rc);
  lv_obj_set_pos(panHdr, 8, 240);
  lv_obj_set_style_text_font(panHdr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(panHdr, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(panHdr, "PAN / BAL");

  s_cfgPanValLbl = lv_label_create(rc);
  lv_obj_set_pos(s_cfgPanValLbl, 236, 240);
  lv_obj_set_size(s_cfgPanValLbl, 130, 16);
  lv_obj_set_style_text_align(s_cfgPanValLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_cfgPanValLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_cfgPanValLbl, lv_color_hex(COL_CYAN), 0);

  lv_obj_t *pPrevBtn = makeBtn(rc, "<", 36, 30, COL_PANEL2);
  lv_obj_set_pos(pPrevBtn, 8, 262);
  attachRepeatCallbacks(pPrevBtn, onCfgPanStep, -1);

  s_cfgPanSlider = makeCfgSlider(rc, 52, 271, 222, -100, 100, COL_ACCENT, onCfgPanSliderChanged);

  lv_obj_t *pNextBtn = makeBtn(rc, ">", 36, 30, COL_PANEL2);
  lv_obj_set_pos(pNextBtn, 282, 262);
  attachRepeatCallbacks(pNextBtn, onCfgPanStep, 1);

  lv_obj_t *pCtrBtn = makeBtn(rc, "C", 46, 30, COL_PANEL2);
  lv_obj_set_pos(pCtrBtn, 322, 262);
  lv_obj_set_style_text_font(lv_obj_get_child(pCtrBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(pCtrBtn, onCfgPanCenterClick, LV_EVENT_CLICKED, nullptr);

  s_cfgMainStBtn = makeBtn(rc, "MAIN LR: ON", 174, 40, COL_ACCENT);
  lv_obj_set_pos(s_cfgMainStBtn, 8, 302);
  s_cfgMainStLbl = lv_obj_get_child(s_cfgMainStBtn, 0);
  lv_obj_set_style_text_font(s_cfgMainStLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgMainStBtn, onCfgMainStClick, LV_EVENT_CLICKED, nullptr);

  s_cfgLinkBtn = makeBtn(rc, "LINK CH 01-02", 178, 40, COL_PANEL2);
  lv_obj_set_pos(s_cfgLinkBtn, 190, 302);
  s_cfgLinkLbl = lv_obj_get_child(s_cfgLinkBtn, 0);
  lv_obj_set_style_text_font(s_cfgLinkLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_cfgLinkBtn, onCfgLinkClick, LV_EVENT_CLICKED, nullptr);
}

void buildScIconModal(lv_obj_t *parent) {
  s_scIconModal = lv_obj_create(parent);
  lv_obj_set_pos(s_scIconModal, 0, 0);
  lv_obj_set_size(s_scIconModal, 800, 434);
  lv_obj_clear_flag(s_scIconModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_scIconModal, lv_color_hex(0x0B0F15), 0);
  lv_obj_set_style_bg_opa(s_scIconModal, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_scIconModal, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_scIconModal, 2, 0);
  lv_obj_set_style_radius(s_scIconModal, 6, 0);
  lv_obj_set_style_pad_all(s_scIconModal, 0, 0);
  lv_obj_add_flag(s_scIconModal, LV_OBJ_FLAG_HIDDEN);

  static constexpr const char *kIconCatNames[6] = {
      "All 74", "Drums", "Bass/Gtr", "Keys/Inst", "Vox/Mic", "I/O & PA"};
  int cx = 8;
  for (int i = 0; i < 6; ++i) {
    int cw = (i == 3 || i == 4 || i == 5) ? 84 : 74;
    s_scIconCatBtns[i] = makeBtn(s_scIconModal, kIconCatNames[i], cw, 32, COL_PANEL2);
    lv_obj_set_pos(s_scIconCatBtns[i], cx, 6);
    lv_obj_set_style_text_font(lv_obj_get_child(s_scIconCatBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_scIconCatBtns[i], onScIconCatClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    cx += cw + 4;
  }

  s_scIconPrevBtn = makeBtn(s_scIconModal, "<", 38, 32, COL_PANEL2);
  lv_obj_set_pos(s_scIconPrevBtn, 524, 6);
  lv_obj_add_event_cb(s_scIconPrevBtn, onScIconPageClick, LV_EVENT_PRESSED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(-1)));

  s_scIconPageLbl = lv_label_create(s_scIconModal);
  lv_obj_set_pos(s_scIconPageLbl, 566, 13);
  lv_obj_set_size(s_scIconPageLbl, 52, 18);
  lv_obj_set_style_text_align(s_scIconPageLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_scIconPageLbl, &lv_font_montserrat_12, 0);
  lv_label_set_text(s_scIconPageLbl, "1 / 1");

  s_scIconNextBtn = makeBtn(s_scIconModal, ">", 38, 32, COL_PANEL2);
  lv_obj_set_pos(s_scIconNextBtn, 622, 6);
  lv_obj_add_event_cb(s_scIconNextBtn, onScIconPageClick, LV_EVENT_PRESSED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(1)));

  lv_obj_t *closeBtn = makeBtn(s_scIconModal, "Close", 74, 32, COL_PANEL2);
  lv_obj_set_pos(closeBtn, 718, 6);
  lv_obj_set_style_text_font(lv_obj_get_child(closeBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(closeBtn, closeScIconModal, LV_EVENT_CLICKED, nullptr);

  // 7x3 grid of visual icon tiles
  for (int slot = 0; slot < ICON_GRID_SLOTS; ++slot) {
    const int col = slot % 7;
    const int row = slot / 7;
    lv_obj_t *tile = makeBtn(s_scIconModal, nullptr, 108, 122, COL_PANEL2);
    s_scIconTiles[slot] = tile;
    lv_obj_set_pos(tile, 10 + col * 112, 44 + row * 128);
    lv_obj_set_style_radius(tile, 0, 0);
    lv_obj_add_event_cb(tile, onScIconTileClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(slot)));

    lv_obj_t *glyph = lv_obj_create(tile);
    s_scIconTileGlyph[slot] = glyph;
    lv_obj_set_pos(glyph, 16, 6);
    lv_obj_set_size(glyph, 72, 68);
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(glyph, lv_color_hex(0x101620), 0);
    lv_obj_set_style_bg_opa(glyph, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(glyph, 0, 0);
    lv_obj_set_style_radius(glyph, 0, 0);
    lv_obj_set_style_pad_all(glyph, 0, 0);
    lv_obj_add_event_cb(glyph, onVectorIconDrawPost, LV_EVENT_DRAW_POST,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(slot)));

    lv_obj_t *lbl = lv_label_create(tile);
    s_scIconTileLbl[slot] = lbl;
    lv_obj_set_pos(lbl, 2, 78);
    lv_obj_set_size(lbl, 100, 36);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  }
}

void buildScribbleTab(lv_obj_t *parent) {
  static constexpr const char *kCatNames[6] = {
      "Ch 1-16", "Ch 17-32", "DCA 1-8", "Bus 1-16", "Aux 1-8", "Main"};
  static constexpr int kCatWidths[6] = {76, 86, 76, 86, 76, 68};
  int x = 4;
  for (int i = 0; i < 6; ++i) {
    int w = kCatWidths[i];
    s_scCatBtns[i] = makeBtn(parent, kCatNames[i], w, 30, COL_PANEL2);
    lv_obj_set_pos(s_scCatBtns[i], x, 4);
    lv_obj_set_style_text_font(lv_obj_get_child(s_scCatBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_scCatBtns[i], onScCatClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    x += w + 4;
  }

  lv_obj_t *flLbl = lv_label_create(parent);
  lv_obj_set_pos(flLbl, 574, 11);
  lv_obj_set_style_text_font(flLbl, &lv_font_montserrat_12, 0);
  lv_label_set_text(flLbl, "Follow X32 [SELECT]");

  s_scFollowSw = lv_switch_create(parent);
  lv_obj_set_pos(s_scFollowSw, 742, 6);
  lv_obj_set_size(s_scFollowSw, 48, 24);
  lv_obj_add_event_cb(s_scFollowSw, onFollowChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // Scrollable strip tile bar
  s_scStripRow = lv_obj_create(parent);
  lv_obj_set_pos(s_scStripRow, 4, 38);
  lv_obj_set_size(s_scStripRow, 792, 42);
  lv_obj_set_scroll_dir(s_scStripRow, LV_DIR_HOR);
  lv_obj_set_style_bg_color(s_scStripRow, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(s_scStripRow, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(s_scStripRow, 0, 0);
  lv_obj_set_style_pad_all(s_scStripRow, 3, 0);

  for (int i = 0; i < 16; ++i) {
    s_scPickBtn[i] = makeBtn(s_scStripRow, nullptr, 92, 32, COL_PANEL2);
    lv_obj_set_pos(s_scPickBtn[i], i * 96, 1);
    lv_obj_set_style_radius(s_scPickBtn[i], 0, 0);
    s_scPickLbl[i] = lv_label_create(s_scPickBtn[i]);
    lv_obj_set_size(s_scPickLbl[i], 84, 28);
    lv_obj_set_style_text_font(s_scPickLbl[i], &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_scPickLbl[i], LV_LABEL_LONG_CLIP);
    lv_obj_center(s_scPickLbl[i]);
    lv_obj_add_event_cb(s_scPickBtn[i], onScPickStripClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  // Left: LCD Scribble Preview Card (with live X32 64x64 icon preview)
  s_scPrevBox = lv_obj_create(parent);
  lv_obj_set_pos(s_scPrevBox, 4, 86);
  lv_obj_set_size(s_scPrevBox, 236, 114);
  lv_obj_clear_flag(s_scPrevBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(s_scPrevBox, 8, 0);
  lv_obj_set_style_pad_all(s_scPrevBox, 0, 0);

  s_scPrevTag = lv_label_create(s_scPrevBox);
  lv_obj_set_pos(s_scPrevTag, 8, 6);
  lv_obj_set_style_text_font(s_scPrevTag, &lv_font_montserrat_14, 0);

  s_scPrevCode = lv_label_create(s_scPrevBox);
  lv_obj_set_pos(s_scPrevCode, 164, 6);
  lv_obj_set_size(s_scPrevCode, 64, 18);
  lv_obj_set_style_text_align(s_scPrevCode, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_scPrevCode, &lv_font_montserrat_14, 0);

  s_scPrevIconBtn = lv_btn_create(s_scPrevBox);
  lv_obj_set_pos(s_scPrevIconBtn, 8, 28);
  lv_obj_set_size(s_scPrevIconBtn, 52, 52);
  lv_obj_set_style_bg_opa(s_scPrevIconBtn, LV_OPA_20, 0);
  lv_obj_set_style_bg_color(s_scPrevIconBtn, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(s_scPrevIconBtn, 1, 0);
  lv_obj_set_style_border_opa(s_scPrevIconBtn, LV_OPA_30, 0);
  lv_obj_set_style_radius(s_scPrevIconBtn, 6, 0);
  lv_obj_set_style_shadow_width(s_scPrevIconBtn, 0, 0);
  lv_obj_add_event_cb(s_scPrevIconBtn, openScIconModal, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(s_scPrevIconBtn, onVectorIconDrawPost, LV_EVENT_DRAW_POST,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(0xFFFFu)));

  s_scPrevName = lv_label_create(s_scPrevBox);
  lv_obj_set_pos(s_scPrevName, 66, 28);
  lv_obj_set_size(s_scPrevName, 162, 26);
  lv_label_set_long_mode(s_scPrevName, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s_scPrevName, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(s_scPrevName, &lv_font_montserrat_20, 0);

  s_scPrevIconLbl = lv_label_create(s_scPrevBox);
  lv_obj_set_pos(s_scPrevIconLbl, 66, 56);
  lv_obj_set_size(s_scPrevIconLbl, 162, 18);
  lv_label_set_long_mode(s_scPrevIconLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_scPrevIconLbl, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(s_scPrevIconLbl, &lv_font_montserrat_12, 0);

  s_scIconOpenBtn = makeBtn(s_scPrevBox, "Pick Icon...", 220, 26, COL_PANEL2);
  lv_obj_set_pos(s_scIconOpenBtn, 8, 82);
  lv_obj_set_style_text_font(lv_obj_get_child(s_scIconOpenBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_scIconOpenBtn, openScIconModal, LV_EVENT_CLICKED, nullptr);

  // Right Editor Row 1 (y=88, h=46): Name Textarea + Clr / < Prev / Next >
  s_scNameTa = lv_textarea_create(parent);
  lv_obj_set_pos(s_scNameTa, 248, 88);
  lv_obj_set_size(s_scNameTa, 304, 46);
  lv_textarea_set_one_line(s_scNameTa, true);
  lv_textarea_set_max_length(s_scNameTa, 12);
  lv_textarea_set_placeholder_text(s_scNameTa, "Type strip name...");
  lv_obj_set_style_text_font(s_scNameTa, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(s_scNameTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_scNameTa, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_scNameTa, 2, 0);
  lv_obj_add_state(s_scNameTa, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(s_scNameTa, onScTaValueChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_scNameTa, onScTaReady, LV_EVENT_READY, nullptr);

  lv_obj_t *bClr = makeBtn(parent, "Clr", 56, 46, COL_PANEL2);
  lv_obj_set_pos(bClr, 558, 88);
  lv_obj_set_style_text_font(lv_obj_get_child(bClr, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(bClr, onScClearBtn, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *bPrev = makeBtn(parent, "< Prev", 76, 46, COL_PANEL2);
  lv_obj_set_pos(bPrev, 620, 88);
  lv_obj_set_style_text_font(lv_obj_get_child(bPrev, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(bPrev, onScPrevBtn, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *bNext = makeBtn(parent, "Next >", 94, 46, COL_GREEN);
  lv_obj_set_pos(bNext, 702, 88);
  lv_obj_add_event_cb(bNext, onScNextBtn, LV_EVENT_CLICKED, nullptr);

  // Right Editor Row 2 (y=148, h=46): 8 Base X32 Color Swatches + Invert Toggle
  for (int i = 0; i < 8; ++i) {
    const X32ColorInfo c = x32_get_color_info(static_cast<uint8_t>(i));
    lv_obj_t *sw = makeBtn(parent, c.code, 54, 46, c.bg_rgb);
    s_scSwatchBtn[i] = sw;
    lv_obj_set_pos(sw, 248 + i * 58, 148);
    lv_obj_set_style_radius(sw, 0, 0);
    lv_obj_t *lbl = lv_obj_get_child(sw, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(c.fg_rgb), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(sw, onScSwatchClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  s_scInvertBtn = makeBtn(parent, "Invert", 82, 46, COL_PANEL2);
  lv_obj_set_pos(s_scInvertBtn, 714, 148);
  s_scInvertLbl = lv_obj_get_child(s_scInvertBtn, 0);
  lv_obj_set_style_text_font(s_scInvertLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_scInvertBtn, onScInvertClick, LV_EVENT_PRESSED, nullptr);

  // Persistent Full-Width Touch Keyboard (must reset LV_ALIGN_TOP_LEFT!)
  s_scKeyboard = lv_keyboard_create(parent);
  placeTopLeft(s_scKeyboard, 4, 206, 792, 224);
  styleKeyboard(s_scKeyboard, &lv_font_montserrat_16);
  lv_keyboard_set_textarea(s_scKeyboard, s_scNameTa);
  lv_obj_add_event_cb(s_scKeyboard, onScKeyboardCancel, LV_EVENT_CANCEL, nullptr);

  buildScIconModal(parent);
}

void buildMatrixTab(lv_obj_t *parent) {
  static constexpr const char *kModes[3] = {"DCA Groups (1-8)", "Mute Groups (1-6)", "Stereo Links"};
  int x = 4;
  for (int i = 0; i < 3; ++i) {
    s_mxModeBtns[i] = makeBtn(parent, kModes[i], 130, 32, COL_PANEL2);
    lv_obj_set_pos(s_mxModeBtns[i], x, 4);
    lv_obj_set_style_text_font(lv_obj_get_child(s_mxModeBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_mxModeBtns[i], onMxModeClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    x += 134;
  }

  static constexpr const char *kBanks[5] = {"Ch 1-8", "Ch 9-16", "Ch 17-24", "Ch 25-32", "Aux 1-8"};
  x = 420;
  for (int i = 0; i < 5; ++i) {
    s_mxBankBtns[i] = makeBtn(parent, kBanks[i], 72, 32, COL_PANEL2);
    lv_obj_set_pos(s_mxBankBtns[i], x, 4);
    lv_obj_set_style_text_font(lv_obj_get_child(s_mxBankBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_mxBankBtns[i], onMxBankClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    x += 75;
  }

  s_mxMatrixPanel = lv_obj_create(parent);
  lv_obj_set_pos(s_mxMatrixPanel, 4, 42);
  lv_obj_set_size(s_mxMatrixPanel, 792, 386);
  lv_obj_clear_flag(s_mxMatrixPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_mxMatrixPanel, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(s_mxMatrixPanel, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(s_mxMatrixPanel, 0, 0);
  lv_obj_set_style_pad_all(s_mxMatrixPanel, 4, 0);

  for (int c = 0; c < 8; ++c) {
    s_mxColHdrBtn[c] = makeBtn(s_mxMatrixPanel, nullptr, 78, 36, COL_PANEL2);
    lv_obj_set_pos(s_mxColHdrBtn[c], 144 + c * 80, 2);
    lv_obj_set_style_radius(s_mxColHdrBtn[c], 0, 0);
    s_mxColHdrLbl[c] = lv_label_create(s_mxColHdrBtn[c]);
    lv_obj_set_size(s_mxColHdrLbl[c], 72, 30);
    lv_obj_set_style_text_align(s_mxColHdrLbl[c], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_mxColHdrLbl[c], &lv_font_montserrat_12, 0);
    lv_obj_center(s_mxColHdrLbl[c]);
    lv_obj_add_event_cb(s_mxColHdrBtn[c], onMxColHdrClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(c)));
  }

  for (int r = 0; r < 8; ++r) {
    const int ry = 44 + r * 42;
    s_mxRowLbl[r] = lv_label_create(s_mxMatrixPanel);
    lv_obj_set_pos(s_mxRowLbl[r], 4, ry + 8);
    lv_obj_set_size(s_mxRowLbl[r], 136, 22);
    lv_label_set_long_mode(s_mxRowLbl[r], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_mxRowLbl[r], &lv_font_montserrat_14, 0);

    for (int c = 0; c < 8; ++c) {
      s_mxCellBtn[r][c] = makeBtn(s_mxMatrixPanel, nullptr, 78, 38, 0x11161E);
      lv_obj_set_pos(s_mxCellBtn[r][c], 144 + c * 80, ry);
      lv_obj_set_style_radius(s_mxCellBtn[r][c], 0, 0);
      s_mxCellLbl[r][c] = lv_label_create(s_mxCellBtn[r][c]);
      lv_obj_center(s_mxCellLbl[r][c]);
      const uint32_t packed = static_cast<uint32_t>((r << 8) | c);
      lv_obj_add_event_cb(s_mxCellBtn[r][c], onMxCellClick, LV_EVENT_CLICKED,
                          reinterpret_cast<void *>(static_cast<uintptr_t>(packed)));
    }
  }

  s_mxLinksPanel = lv_obj_create(parent);
  lv_obj_set_pos(s_mxLinksPanel, 4, 42);
  lv_obj_set_size(s_mxLinksPanel, 792, 386);
  lv_obj_clear_flag(s_mxLinksPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_mxLinksPanel, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(s_mxLinksPanel, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(s_mxLinksPanel, 0, 0);
  lv_obj_set_style_pad_all(s_mxLinksPanel, 6, 0);
  lv_obj_add_flag(s_mxLinksPanel, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < 24; ++i) {
    int col = i % 4;
    int row = i / 4;
    s_mxLinkBtn[i] = makeBtn(s_mxLinksPanel, nullptr, 188, 56, COL_PANEL2);
    lv_obj_set_pos(s_mxLinkBtn[i], 4 + col * 193, 4 + row * 61);
    lv_obj_set_style_radius(s_mxLinkBtn[i], 0, 0);
    s_mxLinkLbl[i] = lv_label_create(s_mxLinkBtn[i]);
    lv_obj_set_size(s_mxLinkLbl[i], 176, 44);
    lv_obj_set_style_text_font(s_mxLinkLbl[i], &lv_font_montserrat_12, 0);
    lv_obj_center(s_mxLinkLbl[i]);
    lv_obj_add_event_cb(s_mxLinkBtn[i], onMxLinkClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }
}

void buildFadersTab(lv_obj_t *parent) {
  static constexpr const char *kBanks[8] = {
      "1-8", "9-16", "17-24", "25-32", "Aux", "Bus1-8", "Bus9-16", "DCA"};
  int x = 4;
  for (int i = 0; i < 8; ++i) {
    int w = (i == 5 || i == 6) ? 66 : 56;
    s_fbBtns[i] = makeBtn(parent, kBanks[i], w, 32, COL_PANEL2);
    lv_obj_set_pos(s_fbBtns[i], x, 4);
    lv_obj_set_style_text_font(lv_obj_get_child(s_fbBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_fbBtns[i], onFaderBankClick, LV_EVENT_PRESSED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    x += w + 4;
  }

  s_busDropdown = lv_dropdown_create(parent);
  lv_obj_set_pos(s_busDropdown, 504, 4);
  lv_obj_set_size(s_busDropdown, 218, 32);
  lv_obj_set_style_text_font(s_busDropdown, &lv_font_montserrat_12, 0);
  lv_dropdown_set_options(s_busDropdown, "Main LR (Channel Faders)");
  lv_obj_add_event_cb(s_busDropdown, onBusDropdownChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  s_exitSofBtn = makeBtn(parent, "Main LR", 68, 32, COL_PANEL2);
  lv_obj_set_pos(s_exitSofBtn, 726, 4);
  lv_obj_set_style_text_font(lv_obj_get_child(s_exitSofBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_exitSofBtn, onExitSofClick, LV_EVENT_CLICKED, nullptr);

  for (int i = 0; i < 9; ++i) {
    const int sx = 4 + i * 88;
    lv_obj_t *box = lv_obj_create(parent);
    s_fStripBox[i] = box;
    lv_obj_set_pos(box, sx, 42);
    lv_obj_set_size(box, 84, 386);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, lv_color_hex(i == 8 ? 0x1A1812 : COL_PANEL), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(i == 8 ? COL_AMBER : COL_BORDER), 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    s_fCapBtn[i] = makeBtn(box, nullptr, 78, 44, COL_PANEL2);
    lv_obj_set_pos(s_fCapBtn[i], 2, 2);
    lv_obj_set_style_radius(s_fCapBtn[i], 0, 0);
    lv_obj_add_event_cb(s_fCapBtn[i], onFaderCapClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    s_fCapTag[i] = lv_label_create(s_fCapBtn[i]);
    lv_obj_set_pos(s_fCapTag[i], 3, 2);
    lv_obj_set_style_text_font(s_fCapTag[i], &lv_font_montserrat_12, 0);

    s_fCapName[i] = lv_label_create(s_fCapBtn[i]);
    lv_obj_set_pos(s_fCapName[i], 3, 20);
    lv_obj_set_size(s_fCapName[i], 70, 18);
    lv_label_set_long_mode(s_fCapName[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_fCapName[i], &lv_font_montserrat_12, 0);

    s_fDbLbl[i] = lv_label_create(box);
    lv_obj_set_pos(s_fDbLbl[i], 2, 50);
    lv_obj_set_size(s_fDbLbl[i], 78, 18);
    lv_obj_add_flag(s_fDbLbl[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_align(s_fDbLbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_fDbLbl[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_fDbLbl[i], lv_color_hex(COL_CYAN), 0);
    lv_obj_add_event_cb(s_fDbLbl[i], onFaderUnitySnapClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    s_fVuBar[i] = lv_bar_create(box);
    lv_obj_set_pos(s_fVuBar[i], 8, 74);
    lv_obj_set_size(s_fVuBar[i], 10, 250);
    lv_bar_set_range(s_fVuBar[i], 0, 100);
    lv_obj_clear_flag(s_fVuBar[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_fVuBar[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_fVuBar[i], 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_fVuBar[i], lv_color_hex(0x090C10), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fVuBar[i], lv_color_hex(COL_GREEN), LV_PART_INDICATOR);

    s_fSlider[i] = lv_slider_create(box);
    lv_obj_set_pos(s_fSlider[i], 32, 74);
    lv_obj_set_size(s_fSlider[i], 34, 250);
    lv_slider_set_range(s_fSlider[i], 0, 1000);
    lv_obj_set_ext_click_area(s_fSlider[i], 12);
    lv_obj_set_style_bg_color(s_fSlider[i], lv_color_hex(0x090C10), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fSlider[i], lv_color_hex(i == 8 ? COL_AMBER : COL_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_fSlider[i], lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_radius(s_fSlider[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_fSlider[i], 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_fSlider[i], 0, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(s_fSlider[i], 6, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(s_fSlider[i], 10, LV_PART_KNOB);
    lv_obj_add_event_cb(s_fSlider[i], onFaderSliderChanged, LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    s_fSoloBtn[i] = makeBtn(box, "S", 36, 36, COL_PANEL2);
    lv_obj_set_pos(s_fSoloBtn[i], 4, 342);
    lv_obj_set_style_radius(s_fSoloBtn[i], 0, 0);
    lv_obj_add_event_cb(s_fSoloBtn[i], onFaderSoloClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    s_fMuteBtn[i] = makeBtn(box, "M", 36, 36, COL_PANEL2);
    lv_obj_set_pos(s_fMuteBtn[i], 42, 342);
    lv_obj_set_style_radius(s_fMuteBtn[i], 0, 0);
    lv_obj_add_event_cb(s_fMuteBtn[i], onFaderMuteClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }
}

void buildNetworkTab(lv_obj_t *parent) {
  // Left Card: Wi-Fi
  lv_obj_t *wCard = lv_obj_create(parent);
  lv_obj_set_pos(wCard, 4, 4);
  lv_obj_set_size(wCard, 392, 424);
  lv_obj_clear_flag(wCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(wCard, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(wCard, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(wCard, 8, 0);
  lv_obj_set_style_pad_all(wCard, 0, 0);

  lv_obj_t *wHdr = lv_label_create(wCard);
  lv_obj_set_pos(wHdr, 10, 10);
  lv_obj_set_style_text_font(wHdr, &lv_font_montserrat_16, 0);
  lv_label_set_text(wHdr, "Wi-Fi Network");

  s_netScanBtn = makeBtn(wCard, "Scan Wi-Fi", 104, 30, COL_ACCENT);
  lv_obj_set_pos(s_netScanBtn, 276, 6);
  s_netScanLbl = lv_obj_get_child(s_netScanBtn, 0);
  lv_obj_add_event_cb(s_netScanBtn, onNetWifiScan, LV_EVENT_CLICKED, nullptr);

  s_netWifiStatusLbl = lv_label_create(wCard);
  lv_obj_set_pos(s_netWifiStatusLbl, 10, 38);
  lv_obj_set_size(s_netWifiStatusLbl, 370, 20);
  lv_label_set_long_mode(s_netWifiStatusLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(s_netWifiStatusLbl, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_text_font(s_netWifiStatusLbl, &lv_font_montserrat_12, 0);

  s_netWifiList = lv_list_create(wCard);
  lv_obj_set_pos(s_netWifiList, 10, 62);
  lv_obj_set_size(s_netWifiList, 370, 210);
  lv_obj_set_style_bg_color(s_netWifiList, lv_color_hex(0x0B0F15), 0);
  lv_obj_set_style_border_color(s_netWifiList, lv_color_hex(COL_BORDER), 0);

  s_netSsidTa = lv_textarea_create(wCard);
  lv_obj_set_pos(s_netSsidTa, 10, 280);
  lv_obj_set_size(s_netSsidTa, 370, 38);
  lv_obj_clear_flag(s_netSsidTa, LV_OBJ_FLAG_SCROLLABLE);
  lv_textarea_set_one_line(s_netSsidTa, true);
  lv_textarea_set_placeholder_text(s_netSsidTa, "Tap to type Wi-Fi SSID...");
  lv_obj_set_style_bg_color(s_netSsidTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netSsidTa, lv_color_hex(COL_BORDER), 0);
  lv_obj_add_event_cb(s_netSsidTa, onNetTaClick, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(NET_KB_SSID)));

  s_netPassTa = lv_textarea_create(wCard);
  lv_obj_set_pos(s_netPassTa, 10, 324);
  lv_obj_set_size(s_netPassTa, 370, 38);
  lv_obj_clear_flag(s_netPassTa, LV_OBJ_FLAG_SCROLLABLE);
  lv_textarea_set_one_line(s_netPassTa, true);
  lv_textarea_set_password_mode(s_netPassTa, true);
  lv_textarea_set_placeholder_text(s_netPassTa, "Tap to type Wi-Fi Password...");
  lv_obj_set_style_bg_color(s_netPassTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netPassTa, lv_color_hex(COL_BORDER), 0);
  lv_obj_add_event_cb(s_netPassTa, onNetTaClick, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(NET_KB_PASS)));

  lv_obj_t *wConnBtn = makeBtn(wCard, "Save & Connect Wi-Fi", 370, 40, COL_GREEN);
  lv_obj_set_pos(wConnBtn, 10, 372);
  lv_obj_add_event_cb(wConnBtn, onNetWifiConnect, LV_EVENT_CLICKED, nullptr);

  // Right Card: X32 & Web UI
  lv_obj_t *xCard = lv_obj_create(parent);
  lv_obj_set_pos(xCard, 402, 4);
  lv_obj_set_size(xCard, 394, 424);
  lv_obj_clear_flag(xCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(xCard, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_border_color(xCard, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(xCard, 8, 0);
  lv_obj_set_style_pad_all(xCard, 0, 0);

  lv_obj_t *xHdr = lv_label_create(xCard);
  lv_obj_set_pos(xHdr, 10, 8);
  lv_obj_set_style_text_font(xHdr, &lv_font_montserrat_16, 0);
  lv_label_set_text(xHdr, "Behringer X32 & Web UI");

  lv_obj_t *discBtn = makeBtn(xCard, "Discover X32", 112, 28, COL_ACCENT);
  lv_obj_set_pos(discBtn, 272, 5);
  lv_obj_add_event_cb(discBtn, onNetX32Discover, LV_EVENT_CLICKED, nullptr);

  s_netX32StatusLbl = lv_label_create(xCard);
  lv_obj_set_pos(s_netX32StatusLbl, 10, 35);
  lv_obj_set_size(s_netX32StatusLbl, 372, 16);
  lv_label_set_long_mode(s_netX32StatusLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(s_netX32StatusLbl, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_text_font(s_netX32StatusLbl, &lv_font_montserrat_12, 0);

  s_netConsoleList = lv_list_create(xCard);
  lv_obj_set_pos(s_netConsoleList, 10, 53);
  lv_obj_set_size(s_netConsoleList, 372, 102);
  lv_obj_set_style_bg_color(s_netConsoleList, lv_color_hex(0x0B0F15), 0);
  lv_obj_set_style_border_color(s_netConsoleList, lv_color_hex(COL_BORDER), 0);

  s_netX32IpTa = lv_textarea_create(xCard);
  lv_obj_set_pos(s_netX32IpTa, 10, 159);
  lv_obj_set_size(s_netX32IpTa, 198, 34);
  lv_obj_clear_flag(s_netX32IpTa, LV_OBJ_FLAG_SCROLLABLE);
  lv_textarea_set_one_line(s_netX32IpTa, true);
  lv_textarea_set_placeholder_text(s_netX32IpTa, "Tap for X32 IP...");
  lv_obj_set_style_bg_color(s_netX32IpTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netX32IpTa, lv_color_hex(COL_BORDER), 0);
  lv_obj_add_event_cb(s_netX32IpTa, onNetTaClick, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(NET_KB_X32IP)));

  lv_obj_t *xConnBtn = makeBtn(xCard, "Connect", 84, 34, COL_ACCENT);
  lv_obj_set_pos(xConnBtn, 214, 159);
  lv_obj_add_event_cb(xConnBtn, onNetX32Connect, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *xSyncBtn = makeBtn(xCard, "Sync All", 80, 34, COL_PANEL2);
  lv_obj_set_pos(xSyncBtn, 302, 159);
  lv_obj_add_event_cb(xSyncBtn, onNetX32Sync, LV_EVENT_CLICKED, nullptr);

  s_netPasskeyBox = lv_obj_create(xCard);
  lv_obj_set_pos(s_netPasskeyBox, 10, 197);
  lv_obj_set_size(s_netPasskeyBox, 176, 32);
  lv_obj_clear_flag(s_netPasskeyBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_netPasskeyBox, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netPasskeyBox, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_netPasskeyBox, 1, 0);
  lv_obj_set_style_radius(s_netPasskeyBox, 6, 0);
  lv_obj_set_style_pad_all(s_netPasskeyBox, 0, 0);

  s_netPasskeyLbl = lv_label_create(s_netPasskeyBox);
  lv_obj_set_style_text_font(s_netPasskeyLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_netPasskeyLbl, lv_color_hex(0x56D4C8), 0);
  lv_label_set_text(s_netPasskeyLbl, "PIN: ------");
  lv_obj_center(s_netPasskeyLbl);

  lv_obj_t *regenBtn = makeBtn(xCard, "New PIN", 92, 32, COL_PANEL2);
  lv_obj_set_pos(regenBtn, 192, 197);
  lv_obj_set_style_text_font(lv_obj_get_child(regenBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(regenBtn, onNetRegenPasskey, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *revokeBtn = makeBtn(xCard, "Revoke", 92, 32, 0x7A2E0E);
  lv_obj_set_pos(revokeBtn, 290, 197);
  lv_obj_set_style_text_font(lv_obj_get_child(revokeBtn, 0), &lv_font_montserrat_12, 0);
  lv_obj_set_style_border_color(revokeBtn, lv_color_hex(COL_RED), 0);
  lv_obj_add_event_cb(revokeBtn, onNetRevokeSessions, LV_EVENT_CLICKED, nullptr);

  s_netWebUrlLbl = lv_label_create(xCard);
  lv_obj_set_pos(s_netWebUrlLbl, 10, 233);
  lv_obj_set_size(s_netWebUrlLbl, 372, 48);
  lv_obj_set_style_text_font(s_netWebUrlLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_netWebUrlLbl, lv_color_hex(COL_MUTED), 0);

  s_netBlBtn = makeBtn(xCard, "Sleep LCD", 92, 36, COL_PANEL2);
  lv_obj_set_pos(s_netBlBtn, 10, 285);
  s_netBlLbl = lv_obj_get_child(s_netBlBtn, 0);
  lv_obj_set_style_text_font(s_netBlLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_netBlBtn, onNetBacklightToggle, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *idlePrevBtn = makeBtn(xCard, "<", 38, 36, COL_PANEL2);
  lv_obj_set_pos(idlePrevBtn, 108, 285);
  lv_obj_add_event_cb(idlePrevBtn, onNetIdleTimeoutStep, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(-1)));

  s_netIdleBox = lv_obj_create(xCard);
  lv_obj_set_pos(s_netIdleBox, 150, 285);
  lv_obj_set_size(s_netIdleBox, 190, 36);
  lv_obj_clear_flag(s_netIdleBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_netIdleBox, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netIdleBox, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(s_netIdleBox, 1, 0);
  lv_obj_set_style_radius(s_netIdleBox, 6, 0);
  lv_obj_set_style_pad_all(s_netIdleBox, 0, 0);

  s_netIdleLbl = lv_label_create(s_netIdleBox);
  lv_obj_set_style_text_font(s_netIdleLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_netIdleLbl, lv_color_hex(COL_CYAN), 0);
  lv_label_set_text(s_netIdleLbl, "No-X32 Off: 1 min");
  lv_obj_center(s_netIdleLbl);

  lv_obj_t *idleNextBtn = makeBtn(xCard, ">", 38, 36, COL_PANEL2);
  lv_obj_set_pos(idleNextBtn, 344, 285);
  lv_obj_add_event_cb(idleNextBtn, onNetIdleTimeoutStep, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(1)));

  s_netOtaBox = lv_obj_create(xCard);
  lv_obj_set_pos(s_netOtaBox, 10, 326);
  lv_obj_set_size(s_netOtaBox, 372, 88);
  lv_obj_clear_flag(s_netOtaBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_netOtaBox, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netOtaBox, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(s_netOtaBox, 1, 0);
  lv_obj_set_style_radius(s_netOtaBox, 6, 0);
  lv_obj_set_style_pad_all(s_netOtaBox, 0, 0);

  s_netOtaVerLbl = lv_label_create(s_netOtaBox);
  lv_obj_set_pos(s_netOtaVerLbl, 8, 6);
  lv_obj_set_size(s_netOtaVerLbl, 194, 18);
  lv_label_set_long_mode(s_netOtaVerLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(s_netOtaVerLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_netOtaVerLbl, lv_color_hex(COL_TEXT), 0);

  s_netOtaStatusLbl = lv_label_create(s_netOtaBox);
  lv_obj_set_pos(s_netOtaStatusLbl, 8, 26);
  lv_obj_set_size(s_netOtaStatusLbl, 194, 36);
  lv_label_set_long_mode(s_netOtaStatusLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s_netOtaStatusLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_netOtaStatusLbl, lv_color_hex(COL_MUTED), 0);

  s_netOtaCheckBtn = makeBtn(s_netOtaBox, "Check", 76, 40, COL_PANEL2);
  lv_obj_set_pos(s_netOtaCheckBtn, 206, 14);
  s_netOtaCheckLbl = lv_obj_get_child(s_netOtaCheckBtn, 0);
  lv_obj_set_style_text_font(s_netOtaCheckLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_netOtaCheckBtn, onNetOtaCheck, LV_EVENT_CLICKED, nullptr);

  s_netOtaInstallBtn = makeBtn(s_netOtaBox, "Install", 76, 40, COL_PANEL2);
  lv_obj_set_pos(s_netOtaInstallBtn, 288, 14);
  s_netOtaInstallLbl = lv_obj_get_child(s_netOtaInstallBtn, 0);
  lv_obj_set_style_text_font(s_netOtaInstallLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_netOtaInstallBtn, onNetOtaInstall, LV_EVENT_CLICKED, nullptr);

  s_netOtaBar = lv_bar_create(s_netOtaBox);
  lv_obj_set_pos(s_netOtaBar, 8, 68);
  lv_obj_set_size(s_netOtaBar, 356, 10);
  lv_bar_set_range(s_netOtaBar, 0, 100);
  lv_obj_clear_flag(s_netOtaBar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(s_netOtaBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(s_netOtaBar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_netOtaBar, lv_color_hex(0x151B23), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_netOtaBar, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);

  // Full-Tab Keyboard Sheet (keeps target text box + action buttons always visible above keys)
  s_netKbModal = lv_obj_create(parent);
  lv_obj_set_pos(s_netKbModal, 0, 0);
  lv_obj_set_size(s_netKbModal, 800, 434);
  lv_obj_clear_flag(s_netKbModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_netKbModal, lv_color_hex(0x0B0F15), 0);
  lv_obj_set_style_bg_opa(s_netKbModal, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_netKbModal, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_netKbModal, 2, 0);
  lv_obj_set_style_radius(s_netKbModal, 6, 0);
  lv_obj_set_style_pad_all(s_netKbModal, 0, 0);
  lv_obj_add_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);

  s_netKbTitleLbl = lv_label_create(s_netKbModal);
  lv_obj_set_pos(s_netKbTitleLbl, 10, 4);
  lv_obj_set_style_text_font(s_netKbTitleLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_netKbTitleLbl, lv_color_hex(COL_CYAN), 0);
  lv_label_set_text(s_netKbTitleLbl, "Enter Text");

  s_netModalTa = lv_textarea_create(s_netKbModal);
  lv_obj_set_pos(s_netModalTa, 6, 22);
  lv_obj_set_size(s_netModalTa, 360, 42);
  lv_textarea_set_one_line(s_netModalTa, true);
  lv_obj_set_style_text_font(s_netModalTa, &lv_font_montserrat_18, 0);
  lv_obj_set_style_bg_color(s_netModalTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_netModalTa, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_netModalTa, 2, 0);
  lv_obj_add_event_cb(s_netModalTa, onNetModalCommit, LV_EVENT_READY, nullptr);

  s_netModalShowBtn = makeBtn(s_netKbModal, "Show", 68, 42, COL_PANEL2);
  lv_obj_set_pos(s_netModalShowBtn, 372, 22);
  s_netModalShowLbl = lv_obj_get_child(s_netModalShowBtn, 0);
  lv_obj_add_event_cb(s_netModalShowBtn, onNetModalShowToggle, LV_EVENT_CLICKED, nullptr);

  s_netModalIp1Btn = makeBtn(s_netKbModal, "192.168.", 74, 42, COL_PANEL2);
  lv_obj_set_pos(s_netModalIp1Btn, 372, 22);
  lv_obj_set_style_text_font(lv_obj_get_child(s_netModalIp1Btn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_netModalIp1Btn, onNetModalIpPrefix, LV_EVENT_CLICKED,
                      const_cast<char *>("192.168."));

  s_netModalIp2Btn = makeBtn(s_netKbModal, "10.0.", 52, 42, COL_PANEL2);
  lv_obj_set_pos(s_netModalIp2Btn, 450, 22);
  lv_obj_set_style_text_font(lv_obj_get_child(s_netModalIp2Btn, 0), &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_netModalIp2Btn, onNetModalIpPrefix, LV_EVENT_CLICKED,
                      const_cast<char *>("10.0."));

  s_netModalModeBtn = makeBtn(s_netKbModal, "ABC", 48, 42, COL_PANEL2);
  lv_obj_set_pos(s_netModalModeBtn, 506, 22);
  s_netModalModeLbl = lv_obj_get_child(s_netModalModeBtn, 0);
  lv_obj_set_style_text_font(s_netModalModeLbl, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(s_netModalModeBtn, onNetModalKbModeToggle, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *mClrBtn = makeBtn(s_netKbModal, "Clr", 48, 42, COL_PANEL2);
  lv_obj_set_pos(mClrBtn, 558, 22);
  lv_obj_add_event_cb(mClrBtn, onNetModalClear, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *mCanBtn = makeBtn(s_netKbModal, "Cancel", 66, 42, COL_PANEL2);
  lv_obj_set_pos(mCanBtn, 610, 22);
  lv_obj_add_event_cb(mCanBtn, onNetModalCancel, LV_EVENT_CLICKED, nullptr);

  s_netModalCommitBtn = makeBtn(s_netKbModal, "Done", 114, 42, COL_GREEN);
  lv_obj_set_pos(s_netModalCommitBtn, 680, 22);
  s_netModalCommitLbl = lv_obj_get_child(s_netModalCommitBtn, 0);
  lv_obj_add_event_cb(s_netModalCommitBtn, onNetModalCommit, LV_EVENT_CLICKED, nullptr);

  s_netKb = lv_keyboard_create(s_netKbModal);
  placeTopLeft(s_netKb, 4, 68, 792, 358);
  styleKeyboard(s_netKb, &lv_font_montserrat_18);
  lv_keyboard_set_textarea(s_netKb, s_netModalTa);
  lv_obj_add_event_cb(s_netKb, onNetModalCancel, LV_EVENT_CANCEL, nullptr);
}

void buildLibraryTab(lv_obj_t *parent) {
  // Top row (y=4, h=36): Mode toggle, 4 slot banks (8 per page), live status info
  s_libModeBtns[0] = makeBtn(parent, "Scenes", 94, 36, COL_ACCENT);
  placeTopLeft(s_libModeBtns[0], 4, 4, 94, 36);
  lv_obj_set_style_text_font(lv_obj_get_child(s_libModeBtns[0], 0), &lv_font_montserrat_14, 0);
  lv_obj_add_event_cb(s_libModeBtns[0], onLibModeClick, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(0)));

  s_libModeBtns[1] = makeBtn(parent, "Ch Presets", 110, 36, COL_PANEL2);
  placeTopLeft(s_libModeBtns[1], 102, 4, 110, 36);
  lv_obj_set_style_text_font(lv_obj_get_child(s_libModeBtns[1], 0), &lv_font_montserrat_14, 0);
  lv_obj_add_event_cb(s_libModeBtns[1], onLibModeClick, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(1)));

  static const char *const kBankLbls[4] = {"00-07", "08-15", "16-23", "24-31"};
  for (int i = 0; i < 4; ++i) {
    s_libBankBtns[i] = makeBtn(parent, kBankLbls[i], 66, 36, COL_PANEL2);
    placeTopLeft(s_libBankBtns[i], 222 + i * 70, 4, 66, 36);
    lv_obj_set_style_text_font(lv_obj_get_child(s_libBankBtns[i], 0), &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_libBankBtns[i], onLibBankClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  s_libInfoLbl = lv_label_create(parent);
  placeTopLeft(s_libInfoLbl, 504, 12, 290, 20);
  lv_obj_set_style_text_font(s_libInfoLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_libInfoLbl, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_text_align(s_libInfoLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(s_libInfoLbl, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_libInfoLbl, "Scenes 00-31");

  // 2x4 Grid of Slot Cards (square corners, explicit cover opacity for RGB LCD)
  for (int i = 0; i < 8; ++i) {
    const int col = i % 4;
    const int row = i / 4;
    lv_obj_t *card = lv_btn_create(parent);
    s_libCard[i] = card;
    placeTopLeft(card, 5 + col * 199, 46 + row * 127, 194, 122);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_PANEL2), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_add_event_cb(card, onLibCardClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));

    s_libCardNum[i] = lv_label_create(card);
    placeTopLeft(s_libCardNum[i], 8, 6, 178, 16);
    lv_obj_set_style_text_font(s_libCardNum[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_libCardNum[i], lv_color_hex(COL_MUTED), 0);
    lv_label_set_text(s_libCardNum[i], "--");

    s_libCardName[i] = lv_label_create(card);
    placeTopLeft(s_libCardName[i], 8, 28, 178, 52);
    lv_obj_set_style_text_font(s_libCardName[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_libCardName[i], lv_color_hex(COL_TEXT), 0);
    lv_label_set_long_mode(s_libCardName[i], LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_libCardName[i], "(empty)");

    s_libCardSub[i] = lv_label_create(card);
    placeTopLeft(s_libCardSub[i], 8, 86, 178, 30);
    lv_obj_set_style_text_font(s_libCardSub[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_libCardSub[i], lv_color_hex(COL_MUTED), 0);
    lv_label_set_long_mode(s_libCardSub[i], LV_LABEL_LONG_DOT);
    lv_label_set_text(s_libCardSub[i], "");
  }

  // Bottom Detail & Action Panel
  lv_obj_t *bot = lv_obj_create(parent);
  placeTopLeft(bot, 5, 302, 790, 126);
  lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(bot, 0, 0);
  lv_obj_set_style_bg_color(bot, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(bot, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(bot, 1, 0);
  lv_obj_set_style_shadow_width(bot, 0, 0);
  lv_obj_set_style_pad_all(bot, 0, 0);

  s_libSelTitleLbl = lv_label_create(bot);
  placeTopLeft(s_libSelTitleLbl, 10, 8, 406, 22);
  lv_obj_set_style_text_font(s_libSelTitleLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_libSelTitleLbl, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(s_libSelTitleLbl, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_libSelTitleLbl, "Scene 00");

  s_libSelSubLbl = lv_label_create(bot);
  placeTopLeft(s_libSelSubLbl, 10, 32, 406, 18);
  lv_obj_set_style_text_font(s_libSelSubLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_libSelSubLbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_long_mode(s_libSelSubLbl, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_libSelSubLbl, "");

  // Target Strip Stepper Bar (shown in Ch Presets mode)
  s_libTargetBar = lv_obj_create(bot);
  placeTopLeft(s_libTargetBar, 10, 52, 406, 36);
  lv_obj_clear_flag(s_libTargetBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_libTargetBar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_libTargetBar, 0, 0);
  lv_obj_set_style_pad_all(s_libTargetBar, 0, 0);

  lv_obj_t *tgtCap = lv_label_create(s_libTargetBar);
  placeTopLeft(tgtCap, 0, 9, 48, 18);
  lv_obj_set_style_text_font(tgtCap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tgtCap, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(tgtCap, "Target:");

  lv_obj_t *prevTgtBtn = makeBtn(s_libTargetBar, "<", 42, 34, COL_PANEL2);
  placeTopLeft(prevTgtBtn, 52, 0, 42, 34);
  attachRepeatCallbacks(prevTgtBtn, onLibTargetStep, -1);

  lv_obj_t *tgtBox = lv_obj_create(s_libTargetBar);
  placeTopLeft(tgtBox, 98, 0, 256, 34);
  lv_obj_clear_flag(tgtBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tgtBox, lv_color_hex(COL_PANEL2), 0);
  lv_obj_set_style_bg_opa(tgtBox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(tgtBox, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(tgtBox, 1, 0);
  lv_obj_set_style_radius(tgtBox, 4, 0);
  lv_obj_set_style_pad_all(tgtBox, 0, 0);

  s_libTargetLbl = lv_label_create(tgtBox);
  lv_obj_set_width(s_libTargetLbl, 244);
  lv_obj_set_style_text_font(s_libTargetLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_libTargetLbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_align(s_libTargetLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_libTargetLbl, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_libTargetLbl, "CH01");
  lv_obj_center(s_libTargetLbl);

  lv_obj_t *nextTgtBtn = makeBtn(s_libTargetBar, ">", 42, 34, COL_PANEL2);
  placeTopLeft(nextTgtBtn, 358, 0, 42, 34);
  attachRepeatCallbacks(nextTgtBtn, onLibTargetStep, 1);

  // Scope Toggle Bar (shown in Ch Presets mode: HA=1, Cfg=2, Gate=4, Dyn=8, EQ=16, Sends=32)
  s_libScopeBar = lv_obj_create(bot);
  placeTopLeft(s_libScopeBar, 10, 92, 406, 30);
  lv_obj_clear_flag(s_libScopeBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_libScopeBar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_libScopeBar, 0, 0);
  lv_obj_set_style_pad_all(s_libScopeBar, 0, 0);

  static const char *const kScopeNames[6] = {"HA", "Cfg", "Gate", "Dyn", "EQ", "Sends"};
  static const int kScopeX[6] = {0, 58, 116, 174, 232, 290};
  static const int kScopeW[6] = {54, 54, 54, 54, 54, 60};
  for (int i = 0; i < 6; ++i) {
    s_libScopeBtns[i] = makeBtn(s_libScopeBar, kScopeNames[i], kScopeW[i], 28, COL_PANEL2);
    placeTopLeft(s_libScopeBtns[i], kScopeX[i], 0, kScopeW[i], 28);
    lv_obj_set_style_text_font(lv_obj_get_child(s_libScopeBtns[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_libScopeBtns[i], onLibScopeClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  s_libLoadBtn = makeBtn(bot, "Load", 168, 86, COL_PANEL2);
  placeTopLeft(s_libLoadBtn, 434, 20, 168, 86);
  s_libLoadLbl = lv_obj_get_child(s_libLoadBtn, 0);
  lv_obj_set_style_text_font(s_libLoadLbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_align(s_libLoadLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_event_cb(s_libLoadBtn, onLibLoadClick, LV_EVENT_CLICKED, nullptr);

  s_libSaveBtn = makeBtn(bot, "Save...", 168, 86, COL_ACCENT);
  placeTopLeft(s_libSaveBtn, 612, 20, 168, 86);
  s_libSaveLbl = lv_obj_get_child(s_libSaveBtn, 0);
  lv_obj_set_style_text_font(s_libSaveLbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_align(s_libSaveLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_event_cb(s_libSaveBtn, openLibSaveModal, LV_EVENT_CLICKED, nullptr);

  // Fullscreen Save Name / Notes On-Screen Keyboard Sheet
  s_libKbModal = lv_obj_create(parent);
  placeTopLeft(s_libKbModal, 0, 0, 800, 434);
  lv_obj_clear_flag(s_libKbModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_libKbModal, lv_color_hex(0x0B0F15), 0);
  lv_obj_set_style_bg_opa(s_libKbModal, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_libKbModal, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_libKbModal, 2, 0);
  lv_obj_set_style_radius(s_libKbModal, 6, 0);
  lv_obj_set_style_pad_all(s_libKbModal, 0, 0);
  lv_obj_add_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);

  s_libKbTitleLbl = lv_label_create(s_libKbModal);
  placeTopLeft(s_libKbTitleLbl, 10, 4, 780, 16);
  lv_obj_set_style_text_font(s_libKbTitleLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_libKbTitleLbl, lv_color_hex(COL_CYAN), 0);
  lv_label_set_text(s_libKbTitleLbl, "Save Slot");

  s_libModalNameTa = lv_textarea_create(s_libKbModal);
  placeTopLeft(s_libModalNameTa, 6, 22, 250, 42);
  lv_textarea_set_one_line(s_libModalNameTa, true);
  lv_textarea_set_max_length(s_libModalNameTa, 23);
  lv_textarea_set_placeholder_text(s_libModalNameTa, "Name");
  lv_obj_set_style_text_font(s_libModalNameTa, &lv_font_montserrat_16, 0);
  lv_obj_set_style_bg_color(s_libModalNameTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_libModalNameTa, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(s_libModalNameTa, 2, 0);
  lv_obj_add_event_cb(s_libModalNameTa, onLibModalTaFocus, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(s_libModalNameTa, onLibModalTaFocus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_libModalNameTa, onLibModalCommit, LV_EVENT_READY, nullptr);

  s_libModalNotesTa = lv_textarea_create(s_libKbModal);
  placeTopLeft(s_libModalNotesTa, 262, 22, 240, 42);
  lv_textarea_set_one_line(s_libModalNotesTa, true);
  lv_textarea_set_max_length(s_libModalNotesTa, 35);
  lv_textarea_set_placeholder_text(s_libModalNotesTa, "Notes (optional)");
  lv_obj_set_style_text_font(s_libModalNotesTa, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_libModalNotesTa, lv_color_hex(0x090C10), 0);
  lv_obj_set_style_border_color(s_libModalNotesTa, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(s_libModalNotesTa, 2, 0);
  lv_obj_add_event_cb(s_libModalNotesTa, onLibModalTaFocus, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(s_libModalNotesTa, onLibModalTaFocus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_libModalNotesTa, onLibModalCommit, LV_EVENT_READY, nullptr);

  lv_obj_t *clrBtn = makeBtn(s_libKbModal, "Clr", 54, 42, COL_PANEL2);
  placeTopLeft(clrBtn, 508, 22, 54, 42);
  lv_obj_add_event_cb(clrBtn, onLibModalClear, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *modeBtn = makeBtn(s_libKbModal, "123", 54, 42, COL_PANEL2);
  placeTopLeft(modeBtn, 566, 22, 54, 42);
  s_libModalModeLbl = lv_obj_get_child(modeBtn, 0);
  lv_obj_add_event_cb(modeBtn, onLibModalKbModeToggle, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *cancelBtn = makeBtn(s_libKbModal, "Cancel", 74, 42, COL_PANEL2);
  placeTopLeft(cancelBtn, 624, 22, 74, 42);
  lv_obj_add_event_cb(cancelBtn, onLibModalCancel, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *saveCommitBtn = makeBtn(s_libKbModal, "Save", 92, 42, COL_GREEN);
  placeTopLeft(saveCommitBtn, 702, 22, 92, 42);
  lv_obj_add_event_cb(saveCommitBtn, onLibModalCommit, LV_EVENT_CLICKED, nullptr);

  s_libKb = lv_keyboard_create(s_libKbModal);
  placeTopLeft(s_libKb, 4, 68, 792, 358);
  styleKeyboard(s_libKb, &lv_font_montserrat_18);
  lv_keyboard_set_textarea(s_libKb, s_libModalNameTa);
  lv_obj_add_event_cb(s_libKb, onLibModalCancel, LV_EVENT_CANCEL, nullptr);
}

// ============================================================
// State Sync Functions (Cached & Diff-Based)
// ============================================================

void syncHeader(const MixerState &st) {
  const uint8_t mode = st.x32Connected ? 2 : (st.x32Connecting ? 1 : 0);
  const char *xName = st.x32Name[0] ? st.x32Name : "X32";
  if (!s_hdrCache.init || s_hdrCache.mode != mode ||
      std::strcmp(s_hdrCache.x32Name, xName) != 0) {
    s_hdrCache.mode = mode;
    std::snprintf(s_hdrCache.x32Name, sizeof(s_hdrCache.x32Name), "%s", xName);
    if (mode == 2) {
      lv_obj_set_style_bg_color(s_connPill, lv_color_hex(0x122B1A), 0);
      lv_obj_set_style_border_color(s_connPill, lv_color_hex(COL_GREEN), 0);
      lv_obj_set_style_text_color(s_connLbl, lv_color_hex(0x56D364), 0);
      lv_label_set_text_fmt(s_connLbl, "LIVE %s", xName);
    } else if (mode == 1) {
      lv_obj_set_style_bg_color(s_connPill, lv_color_hex(0x2D2208), 0);
      lv_obj_set_style_border_color(s_connPill, lv_color_hex(COL_AMBER), 0);
      lv_obj_set_style_text_color(s_connLbl, lv_color_hex(0xF2CC60), 0);
      lv_label_set_text(s_connLbl, "SYNC...");
    } else {
      lv_obj_set_style_bg_color(s_connPill, lv_color_hex(0x321414), 0);
      lv_obj_set_style_border_color(s_connPill, lv_color_hex(COL_RED), 0);
      lv_obj_set_style_text_color(s_connLbl, lv_color_hex(0xFF7B72), 0);
      lv_label_set_text(s_connLbl, "NO X32");
    }
  }

  const bool anySolo = mixerAnySoloActive(st);
  if (!s_hdrCache.init || s_hdrCache.anySolo != anySolo) {
    s_hdrCache.anySolo = anySolo;
    setVisibleIfChanged(s_clearSoloBtn, anySolo);
  }

  const char *ip = st.wifiConnected ? st.staIp : (st.apActive ? st.apIp : "");
  if (!s_hdrCache.init || s_hdrCache.wifiConn != st.wifiConnected ||
      s_hdrCache.apActive != st.apActive || s_hdrCache.rssi != st.wifiRssi ||
      std::strcmp(s_hdrCache.netIp, ip) != 0) {
    s_hdrCache.wifiConn = st.wifiConnected;
    s_hdrCache.apActive = st.apActive;
    s_hdrCache.rssi = st.wifiRssi;
    std::snprintf(s_hdrCache.netIp, sizeof(s_hdrCache.netIp), "%s", ip);
    if (st.wifiConnected) {
      lv_label_set_text_fmt(s_netLbl, "%s (%d)", st.staIp, st.wifiRssi);
    } else if (st.apActive) {
      lv_label_set_text_fmt(s_netLbl, "AP %s", st.apIp);
    } else {
      lv_label_set_text(s_netLbl, "Offline");
    }
  }
  s_hdrCache.init = true;
}

void syncOverview(const MixerState &st) {
  if (!s_ovPanelCache.init || std::strcmp(s_ovPanelCache.statusMsg, st.statusMsg) != 0) {
    std::snprintf(s_ovPanelCache.statusMsg, sizeof(s_ovPanelCache.statusMsg), "%s", st.statusMsg);
    setLabelTextIfChanged(s_ovStatusLbl, st.statusMsg);
  }
  if (!s_ovPanelCache.init || s_ovPanelCache.quickMute != st.overviewQuickMute) {
    s_ovPanelCache.quickMute = st.overviewQuickMute;
    setLabelTextIfChanged(s_ovModeLbl, st.overviewQuickMute ? "Tap: Quick Mute" : "Tap: Inspect");
    setSwitchStateIfChanged(s_ovModeSw, st.overviewQuickMute);
  }
  if (!s_ovPanelCache.init || s_ovPanelCache.follow != st.followConsoleSelect) {
    s_ovPanelCache.follow = st.followConsoleSelect;
    setSwitchStateIfChanged(s_selFollowSw, st.followConsoleSelect);
  }

  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const StripState &ch = st.ch[i];
    OvTileCache &c = s_ovTileCache[i];
    const bool isSel = (st.selectedKind == STRIP_CH && st.selectedIdx == i);
    const bool grpOnly = mixerIsStripGroupMutedOnly(st, STRIP_CH, i);
    const bool effMuted = ch.muted || grpOnly;
    char defTag[16];
    const char *nm = ch.name;
    if (!nm[0]) {
      mixerFormatStripTag(STRIP_CH, i, defTag, sizeof(defTag));
      nm = defTag;
    }

    const bool colChanged = !c.init || c.color != ch.color;
    if (!c.init || c.isSel != isSel) {
      c.isSel = isSel;
      lv_obj_set_style_border_color(s_ovTile[i], lv_color_hex(isSel ? COL_CYAN : COL_BORDER), 0);
      lv_obj_set_style_border_width(s_ovTile[i], isSel ? 2 : 1, 0);
    }
    if (colChanged || std::strcmp(c.name, nm) != 0) {
      c.color = ch.color;
      std::snprintf(c.name, sizeof(c.name), "%s", nm);
      applyX32BannerStyle(s_ovTileHead[i], s_ovTileNum[i], s_ovTileName[i], ch.color);
      lv_label_set_text(s_ovTileName[i], nm);
    }
    const uint8_t chSrc = st.chCfg[i].source;
    const uint8_t defSrc = x32_default_source_for_strip(STRIP_CH, i);
    if (colChanged || c.src != chSrc) {
      c.src = chSrc;
      const X32ColorInfo ci = x32_get_color_info(ch.color);
      char srcBuf[10];
      x32_format_source_short(chSrc, srcBuf, sizeof(srcBuf));
      lv_label_set_text(s_ovTileSrc[i], srcBuf);
      if (chSrc == 0) {
        lv_obj_set_style_bg_color(s_ovTileSrc[i], lv_color_hex(0x321414), 0);
        lv_obj_set_style_bg_opa(s_ovTileSrc[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_ovTileSrc[i], lv_color_hex(0xFF7B72), 0);
        lv_obj_set_style_text_opa(s_ovTileSrc[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_decor(s_ovTileSrc[i], LV_TEXT_DECOR_NONE, 0);
      } else if (chSrc != defSrc) {
        lv_obj_set_style_bg_opa(s_ovTileSrc[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_ovTileSrc[i], lv_color_hex(ci.fg_rgb), 0);
        lv_obj_set_style_text_opa(s_ovTileSrc[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_decor(s_ovTileSrc[i], LV_TEXT_DECOR_UNDERLINE, 0);
      } else {
        lv_obj_set_style_bg_opa(s_ovTileSrc[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_ovTileSrc[i], lv_color_hex(ci.fg_rgb), 0);
        lv_obj_set_style_text_opa(s_ovTileSrc[i], LV_OPA_70, 0);
        lv_obj_set_style_text_decor(s_ovTileSrc[i], LV_TEXT_DECOR_NONE, 0);
      }
    }
    if (!c.init || c.meterPct != ch.meterPct) {
      c.meterPct = ch.meterPct;
      lv_bar_set_value(s_ovTileVu[i], ch.meterPct, LV_ANIM_OFF);
    }
    if (!c.init || c.effMuted != effMuted || c.grpMutedOnly != grpOnly) {
      c.effMuted = effMuted;
      c.grpMutedOnly = grpOnly;
      const uint32_t bg = ch.muted ? COL_RED : (grpOnly ? COL_GRP_MUTE : 0x222832);
      lv_obj_set_style_bg_color(s_ovTileMute[i], lv_color_hex(bg), 0);
      lv_obj_set_style_text_color(
          s_ovTileMute[i], lv_color_hex(effMuted ? 0xFFFFFF : 0x6E7681), 0);
      lv_label_set_text(s_ovTileMute[i], grpOnly ? "M*" : "M");
    }
    if (!c.init || c.solo != ch.solo) {
      c.solo = ch.solo;
      lv_obj_set_style_bg_color(s_ovTileSolo[i], lv_color_hex(ch.solo ? COL_SOLO : 0x222832), 0);
      lv_obj_set_style_text_color(
          s_ovTileSolo[i], lv_color_hex(ch.solo ? 0x11151C : 0x6E7681), 0);
    }
    c.init = true;
  }

  const StripState *sel = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
  if (sel) {
    char tag[16], db[16];
    mixerFormatStripTag(st.selectedKind, st.selectedIdx, tag, sizeof(tag));
    x32_format_db(sel->fader, db, sizeof(db));
    const char *snm = sel->name[0] ? sel->name : "(unnamed)";
    const int32_t fInt = static_cast<int32_t>(sel->fader * 1000.0f + 0.5f);

    char selTagBuf[36];
    if (st.selectedKind == STRIP_CH || st.selectedKind == STRIP_AUX) {
      const uint8_t sSrc = (st.selectedKind == STRIP_CH)
                               ? st.chCfg[st.selectedIdx].source
                               : st.auxCfg[st.selectedIdx].source;
      const bool sInv = (st.selectedKind == STRIP_CH)
                            ? st.chCfg[st.selectedIdx].invert
                            : st.auxCfg[st.selectedIdx].invert;
      const int sHa = x32_source_to_headamp_idx(sSrc, st.inRouting);
      const bool phOn = (sHa >= 0 && sHa < 128) && st.headampPhantom[sHa];
      char srcShort[12];
      x32_format_source_short(sSrc, srcShort, sizeof(srcShort));
      if (st.selectedKind == STRIP_CH) {
        std::snprintf(selTagBuf, sizeof(selTagBuf), "CH%02d|%s%s%s", st.selectedIdx + 1, srcShort,
                      phOn ? "+48" : "", sInv ? " INV" : "");
      } else {
        std::snprintf(selTagBuf, sizeof(selTagBuf), "AUX%d|%s%s%s", st.selectedIdx + 1, srcShort,
                      phOn ? "+48" : "", sInv ? " INV" : "");
      }
    } else {
      std::snprintf(selTagBuf, sizeof(selTagBuf), "%s", tag);
    }

    if (!s_ovPanelCache.init || s_ovPanelCache.selKind != st.selectedKind ||
        s_ovPanelCache.selIdx != st.selectedIdx || s_ovPanelCache.selColor != sel->color) {
      s_ovPanelCache.selKind = st.selectedKind;
      s_ovPanelCache.selIdx = st.selectedIdx;
      s_ovPanelCache.selColor = sel->color;
      applyX32BannerStyle(s_selBanner, s_selTagLbl, s_selNameLbl, sel->color);
      const X32ColorInfo ci = x32_get_color_info(sel->color);
      lv_obj_set_style_text_color(s_selDbLbl, lv_color_hex(ci.fg_rgb), 0);
    }
    if (!s_ovPanelCache.init || std::strcmp(s_ovPanelCache.selTagText, selTagBuf) != 0) {
      std::snprintf(s_ovPanelCache.selTagText, sizeof(s_ovPanelCache.selTagText), "%s", selTagBuf);
      lv_label_set_text(s_selTagLbl, selTagBuf);
    }
    if (!s_ovPanelCache.init || std::strcmp(s_ovPanelCache.selDb, db) != 0) {
      std::snprintf(s_ovPanelCache.selDb, sizeof(s_ovPanelCache.selDb), "%s", db);
      lv_label_set_text(s_selDbLbl, db);
    }
    if (!s_ovPanelCache.init || std::strcmp(s_ovPanelCache.selName, snm) != 0) {
      std::snprintf(s_ovPanelCache.selName, sizeof(s_ovPanelCache.selName), "%s", snm);
      lv_label_set_text(s_selNameLbl, snm);
    }
    if (!s_ovPanelCache.init || s_ovPanelCache.selMuted != sel->muted) {
      s_ovPanelCache.selMuted = sel->muted;
      applyMuteBtnStyle(s_selMuteBtn, sel->muted, false);
    }
    if (!s_ovPanelCache.init || s_ovPanelCache.selSolo != sel->solo) {
      s_ovPanelCache.selSolo = sel->solo;
      applySoloBtnStyle(s_selSoloBtn, sel->solo);
    }
    if ((!s_ovPanelCache.init || s_ovPanelCache.selFaderInt != fInt) &&
        !lv_slider_is_dragged(s_selSlider)) {
      s_ovPanelCache.selFaderInt = fInt;
      lv_slider_set_value(s_selSlider, fInt, LV_ANIM_OFF);
    }
  }

  for (int i = 0; i < NUM_DCAS; ++i) {
    const StripState &d = st.dca[i];
    const char *dnm = d.name[0] ? d.name : "DCA";
    if (!s_ovPanelCache.init || s_ovPanelCache.dcaCol[i] != d.color ||
        s_ovPanelCache.dcaMuted[i] != d.muted || std::strcmp(s_ovPanelCache.dcaName[i], dnm) != 0) {
      s_ovPanelCache.dcaCol[i] = d.color;
      s_ovPanelCache.dcaMuted[i] = d.muted;
      std::snprintf(s_ovPanelCache.dcaName[i], sizeof(s_ovPanelCache.dcaName[i]), "%s", dnm);
      const X32ColorInfo ci = x32_get_color_info(d.color);
      lv_obj_set_style_bg_color(
          s_ovDcaDot[i], lv_color_hex(ci.inverted ? ci.accent_rgb : ci.bg_rgb), 0);
      lv_obj_set_style_bg_color(s_ovDcaBtn[i], lv_color_hex(d.muted ? 0x321414 : COL_PANEL2), 0);
      lv_obj_set_style_border_color(
          s_ovDcaBtn[i], lv_color_hex(d.muted ? COL_RED : COL_BORDER), 0);
      lv_label_set_text(s_ovDcaLbl[i], dnm);
    }
  }

  for (int i = 0; i < NUM_MUTE_GRPS; ++i) {
    const bool on = st.muteGroupEngaged[i];
    if (!s_ovPanelCache.init || s_ovPanelCache.mg[i] != on) {
      s_ovPanelCache.mg[i] = on;
      lv_obj_set_style_bg_color(s_ovMgBtn[i], lv_color_hex(on ? COL_RED : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_ovMgBtn[i], lv_color_hex(on ? 0xFF7B72 : COL_BORDER), 0);
    }
  }

  char mainDb[16];
  x32_format_db(st.mainLr.fader, mainDb, sizeof(mainDb));
  const int32_t mInt = static_cast<int32_t>(st.mainLr.fader * 1000.0f + 0.5f);
  if (!s_ovPanelCache.init || std::strcmp(s_ovPanelCache.mainDb, mainDb) != 0) {
    std::snprintf(s_ovPanelCache.mainDb, sizeof(s_ovPanelCache.mainDb), "%s", mainDb);
    lv_label_set_text(s_ovMainDbLbl, mainDb);
  }
  if (!s_ovPanelCache.init || s_ovPanelCache.mainVu != st.mainLr.meterPct) {
    s_ovPanelCache.mainVu = st.mainLr.meterPct;
    lv_bar_set_value(s_ovMainVu, st.mainLr.meterPct, LV_ANIM_OFF);
  }
  if ((!s_ovPanelCache.init || s_ovPanelCache.mainFaderInt != mInt) &&
      !lv_slider_is_dragged(s_ovMainSlider)) {
    s_ovPanelCache.mainFaderInt = mInt;
    lv_slider_set_value(s_ovMainSlider, mInt, LV_ANIM_OFF);
  }
  if (!s_ovPanelCache.init || s_ovPanelCache.mainMuted != st.mainLr.muted) {
    s_ovPanelCache.mainMuted = st.mainLr.muted;
    applyMuteBtnStyle(s_ovMainMuteBtn, st.mainLr.muted, false);
  }
  s_ovPanelCache.init = true;
}

void syncConfig(const MixerState &st) {
  if (!s_cfgCache.init || s_cfgCache.follow != st.followConsoleSelect) {
    s_cfgCache.follow = st.followConsoleSelect;
    setSwitchStateIfChanged(s_cfgFollowSw, st.followConsoleSelect);
  }

  StripKind k = STRIP_CH;
  int idx = 0;
  getActiveConfigTarget(st, &k, &idx);

  const StripState *sp = mixerGetStripConst(st, k, idx);
  if (!sp) return;

  uint8_t src = 0;
  float trim = 0.5f, hpf = 0.0f, pan = 0.5f;
  bool invert = false, hpon = false, mainSt = true;
  getStripCfgValues(st, k, idx, &src, &trim, &invert, &hpon, &hpf, &mainSt, &pan);

  const bool stripChanged = !s_cfgCache.init || s_cfgCache.kind != k || s_cfgCache.idx != idx;
  if (stripChanged) {
    s_cfgPhantomArmed = false;
    if (src >= 1 && src <= 16) s_cfgSrcTab = 0;
    else if (src >= 17 && src <= 32) s_cfgSrcTab = 1;
    else if (src >= 33 && src <= 48) s_cfgSrcTab = 2;
    else if (src >= 49 && src <= 64) s_cfgSrcTab = 3;
  }

  if (!s_cfgCache.init || s_cfgCache.cat != s_cfgCat) {
    s_cfgCache.cat = s_cfgCat;
    for (int i = 0; i < 3; ++i) {
      lv_obj_set_style_bg_color(
          s_cfgCatBtns[i], lv_color_hex(i == s_cfgCat ? COL_ACCENT : COL_PANEL2), 0);
    }
  }

  const char *rawNm = sp->name[0] ? sp->name : "(unnamed)";
  if (stripChanged || s_cfgCache.color != sp->color || std::strcmp(s_cfgCache.name, rawNm) != 0) {
    s_cfgCache.color = sp->color;
    std::snprintf(s_cfgCache.name, sizeof(s_cfgCache.name), "%s", rawNm);
    char tagBuf[16];
    mixerFormatStripTag(k, idx, tagBuf, sizeof(tagBuf));
    applyX32BannerStyle(s_cfgBanner, s_cfgTagLbl, s_cfgNameLbl, sp->color);
    lv_label_set_text(s_cfgTagLbl, tagBuf);
    lv_label_set_text(s_cfgNameLbl, rawNm);
  }

  StripKind pkKind = STRIP_CH;
  int pkStart = 0, pkCnt = 0;
  getCfgCategoryRange(s_cfgCat, &pkKind, &pkStart, &pkCnt);
  for (int i = 0; i < 16; ++i) {
    const bool vis = (i < pkCnt);
    if (!s_cfgCache.init || s_cfgCache.pickVis[i] != vis) {
      s_cfgCache.pickVis[i] = vis;
      setVisibleIfChanged(s_cfgPickBtn[i], vis);
    }
    if (!vis) continue;
    const StripState *ps = mixerGetStripConst(st, pkKind, pkStart + i);
    const uint8_t pcol = ps ? ps->color : 0;
    const bool pSel = (pkKind == k && (pkStart + i) == idx);
    const uint8_t pSrc = (pkKind == STRIP_AUX) ? st.auxCfg[pkStart + i].source : st.chCfg[pkStart + i].source;
    char pTxt[8];
    if (pkKind == STRIP_AUX) std::snprintf(pTxt, sizeof(pTxt), "A%d", pkStart + i + 1);
    else std::snprintf(pTxt, sizeof(pTxt), "%02d", pkStart + i + 1);

    if (stripChanged || !s_cfgCache.init || s_cfgCache.pickCol[i] != pcol ||
        s_cfgCache.pickSrc[i] != pSrc || std::strcmp(s_cfgCache.pickName[i], pTxt) != 0) {
      s_cfgCache.pickCol[i] = pcol;
      s_cfgCache.pickSrc[i] = pSrc;
      std::snprintf(s_cfgCache.pickName[i], sizeof(s_cfgCache.pickName[i]), "%s", pTxt);
      lv_label_set_text(s_cfgPickLbl[i], pTxt);
      const X32ColorInfo pci = x32_get_color_info(pcol);
      lv_obj_set_style_bg_color(s_cfgPickBtn[i], lv_color_hex(pSel ? COL_ACCENT : COL_PANEL2), 0);
      lv_obj_set_style_border_color(
          s_cfgPickBtn[i],
          lv_color_hex(pSel ? COL_CYAN : (pcol == 0 ? COL_BORDER : (pci.inverted ? pci.accent_rgb : pci.bg_rgb))),
          0);
    }
  }

  // Left card: source tabs, 4x4 patch matrix, routing & sharing indicators
  uint32_t sHash = 2166136261u;
  sHash = fnv1aUpdate(sHash, &s_cfgSrcTab, sizeof(s_cfgSrcTab));
  sHash = fnv1aUpdate(sHash, &src, sizeof(src));
  sHash = fnv1aUpdate(sHash, st.inRouting, sizeof(st.inRouting));
  sHash = fnv1aUpdate(sHash, &st.haFlags, sizeof(st.haFlags));
  for (int i = 0; i < NUM_CHANNELS; ++i) sHash = fnv1aUpdate(sHash, &st.chCfg[i].source, 1);
  for (int i = 0; i < NUM_AUX; ++i) sHash = fnv1aUpdate(sHash, &st.auxCfg[i].source, 1);

  const int haIdx = x32_source_to_headamp_idx(src, st.inRouting);
  const bool split = (haIdx >= 0) && x32_headamp_is_gain_split(haIdx, st.haFlags);

  if (stripChanged || !s_cfgCache.init || s_cfgCache.srcTab != s_cfgSrcTab ||
      s_cfgCache.source != src || s_cfgCache.srcShareHash != sHash) {
    s_cfgCache.srcTab = s_cfgSrcTab;
    s_cfgCache.srcShareHash = sHash;

    for (int t = 0; t < 4; ++t) {
      const bool on = (t == s_cfgSrcTab);
      lv_obj_set_style_bg_color(s_cfgSrcTabBtns[t], lv_color_hex(on ? COL_ACCENT : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_cfgSrcTabBtns[t], lv_color_hex(on ? COL_CYAN : COL_BORDER), 0);
    }

    char fullSrc[24];
    x32_format_source_full(src, fullSrc, sizeof(fullSrc));
    if (haIdx >= 0) {
      char sock[16];
      x32_format_headamp_socket(haIdx, true, sock, sizeof(sock));
      lv_label_set_text_fmt(s_cfgSrcSubLbl, "%s (%s)", fullSrc, sock);
    } else {
      lv_label_set_text(s_cfgSrcSubLbl, fullSrc);
    }

    for (int slot = 0; slot < 16; ++slot) {
      const uint8_t bSrc = static_cast<uint8_t>(s_cfgSrcTab * 16 + slot + 1);
      const bool isAct = (bSrc == src);
      bool usedByOther = false;
      for (int c = 0; c < NUM_CHANNELS && !usedByOther; ++c) {
        if ((k != STRIP_CH || c != idx) && st.chCfg[c].source == bSrc) usedByOther = true;
      }
      for (int a = 0; a < NUM_AUX && !usedByOther; ++a) {
        if ((k != STRIP_AUX || a != idx) && st.auxCfg[a].source == bSrc) usedByOther = true;
      }

      char bLbl[32];
      char sName[20];
      x32_format_source_full(bSrc, sName, sizeof(sName));
      const int bHa = x32_source_to_headamp_idx(bSrc, st.inRouting);
      if (bHa >= 0) {
        char sock[14];
        x32_format_headamp_socket(bHa, true, sock, sizeof(sock));
        std::snprintf(bLbl, sizeof(bLbl), "%s\n%s", sName, sock);
      } else {
        std::snprintf(bLbl, sizeof(bLbl), "%s", sName);
      }
      setLabelTextIfChanged(s_cfgSrcGridLbls[slot], bLbl);
      lv_obj_set_style_bg_color(s_cfgSrcGridBtns[slot], lv_color_hex(isAct ? COL_ACCENT : COL_PANEL2), 0);
      lv_obj_set_style_border_color(
          s_cfgSrcGridBtns[slot],
          lv_color_hex(isAct ? COL_CYAN : (usedByOther ? COL_AMBER : COL_BORDER)), 0);
    }

    lv_obj_set_style_bg_color(s_cfgSrcOffBtn, lv_color_hex(src == 0 ? COL_RED : COL_PANEL2), 0);
    lv_obj_set_style_border_color(s_cfgSrcOffBtn, lv_color_hex(src == 0 ? 0xFF7B72 : COL_BORDER), 0);

    const uint8_t defSrc = x32_default_source_for_strip(static_cast<int>(k), idx);
    lv_obj_set_style_border_color(s_cfgSrcDefBtn, lv_color_hex(src == defSrc ? COL_GREEN : COL_BORDER), 0);

    // Find first other strip sharing this non-zero source
    StripKind shK = STRIP_CH;
    int shI = -1;
    if (src != 0) {
      for (int c = 0; c < NUM_CHANNELS; ++c) {
        if ((k != STRIP_CH || c != idx) && st.chCfg[c].source == src) {
          shK = STRIP_CH;
          shI = c;
          break;
        }
      }
      if (shI < 0) {
        for (int a = 0; a < NUM_AUX; ++a) {
          if ((k != STRIP_AUX || a != idx) && st.auxCfg[a].source == src) {
            shK = STRIP_AUX;
            shI = a;
            break;
          }
        }
      }
    }
    if (shI >= 0) {
      char shTag[16];
      mixerFormatStripTag(shK, shI, shTag, sizeof(shTag));
      lv_label_set_text_fmt(s_cfgWarnLbl, "Shared with %s (HA gain / +48V shared)", shTag);
      lv_obj_set_style_text_color(s_cfgWarnLbl, lv_color_hex(COL_AMBER), 0);
    } else if (split) {
      lv_label_set_text(s_cfgWarnLbl, "Gain Split active on socket: Digital Trim mode");
      lv_obj_set_style_text_color(s_cfgWarnLbl, lv_color_hex(COL_CYAN), 0);
    } else {
      lv_label_set_text(s_cfgWarnLbl, "Tap any source above to re-patch live");
      lv_obj_set_style_text_color(s_cfgWarnLbl, lv_color_hex(COL_MUTED), 0);
    }
  }
  s_cfgCache.source = src;

  // Right card Section 1: Preamp Gain or Trim + +48V + Polarity
  const bool useHa = (haIdx >= 0) && !split && !s_cfgForceTrim;
  int32_t gStep = 0;
  char gTitle[28];
  char gDb[20];
  if (useHa) {
    const float gF = st.headampGain[haIdx];
    const float db = x32_gain_to_db(gF);
    gStep = static_cast<int32_t>(std::lround((db + 12.0f) * 2.0f));
    if (gStep < 0) gStep = 0;
    if (gStep > 144) gStep = 144;
    char sock[16];
    x32_format_headamp_socket(haIdx, true, sock, sizeof(sock));
    std::snprintf(gTitle, sizeof(gTitle), "HA GAIN (%s)", sock);
    x32_format_gain_db(gF, gDb, sizeof(gDb));
  } else {
    const float db = x32_trim_to_db(trim);
    gStep = static_cast<int32_t>(std::lround((db + 18.0f) * 4.0f));
    if (gStep < 0) gStep = 0;
    if (gStep > 144) gStep = 144;
    std::snprintf(gTitle, sizeof(gTitle), "DIGITAL TRIM");
    x32_format_trim_db(trim, gDb, sizeof(gDb));
  }

  if (stripChanged || !s_cfgCache.init || s_cfgCache.haIdx != haIdx || s_cfgCache.useHa != useHa) {
    s_cfgCache.useHa = useHa;
    setLabelTextIfChanged(s_cfgPreTitleLbl, gTitle);
    lv_obj_set_style_bg_color(s_cfgGainSlider, lv_color_hex(useHa ? COL_CYAN : COL_AMBER),
                              LV_PART_INDICATOR);
    setLabelTextIfChanged(s_cfgModeTrimLbl,
                          (haIdx >= 0 && !split) ? (s_cfgForceTrim ? "Show HA" : "Show Trim") : "Zero 0dB");
  }
  if (stripChanged || !s_cfgCache.init || s_cfgCache.gainStep != gStep || s_cfgCache.useHa != useHa ||
      s_cfgCache.haIdx != haIdx) {
    s_cfgCache.gainStep = gStep;
    setLabelTextIfChanged(s_cfgGainDbLbl, gDb);
    if (!lv_slider_is_dragged(s_cfgGainSlider)) {
      lv_slider_set_value(s_cfgGainSlider, gStep, LV_ANIM_OFF);
    }
  }

  const bool phValid = (haIdx >= 0 && haIdx < 128 && !split);
  const bool phOn = phValid && st.headampPhantom[haIdx];
  const uint32_t nowMs = millis();
  if (s_cfgPhantomArmed && (!phValid || s_cfgPhantomArmHa != haIdx || (nowMs - s_cfgPhantomArmMs > 3500u))) {
    s_cfgPhantomArmed = false;
  }
  if (stripChanged || !s_cfgCache.init || s_cfgCache.haIdx != haIdx || s_cfgCache.phantom != phOn ||
      s_cfgCache.phantomArmed != s_cfgPhantomArmed) {
    s_cfgCache.haIdx = static_cast<int16_t>(haIdx);
    s_cfgCache.phantom = phOn;
    s_cfgCache.phantomArmed = s_cfgPhantomArmed;
    if (!phValid) {
      lv_obj_add_state(s_cfgPhantomBtn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(s_cfgPhantomBtn, lv_color_hex(COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_cfgPhantomBtn, lv_color_hex(COL_BORDER), 0);
      setLabelTextIfChanged(s_cfgPhantomLbl, split ? "48V Split" : "48V N/A");
      lv_obj_set_style_text_color(s_cfgPhantomLbl, lv_color_hex(COL_MUTED), 0);
    } else {
      lv_obj_clear_state(s_cfgPhantomBtn, LV_STATE_DISABLED);
      lv_obj_set_style_text_color(s_cfgPhantomLbl, lv_color_hex(COL_TEXT), 0);
      if (s_cfgPhantomArmed) {
        lv_obj_set_style_bg_color(s_cfgPhantomBtn, lv_color_hex(COL_AMBER), 0);
        lv_obj_set_style_border_color(s_cfgPhantomBtn, lv_color_hex(0xF2CC60), 0);
        setLabelTextIfChanged(s_cfgPhantomLbl, "CONFIRM 48V?");
      } else if (phOn) {
        lv_obj_set_style_bg_color(s_cfgPhantomBtn, lv_color_hex(COL_RED), 0);
        lv_obj_set_style_border_color(s_cfgPhantomBtn, lv_color_hex(0xFF7B72), 0);
        setLabelTextIfChanged(s_cfgPhantomLbl, "+48V ON");
      } else {
        lv_obj_set_style_bg_color(s_cfgPhantomBtn, lv_color_hex(COL_PANEL2), 0);
        lv_obj_set_style_border_color(s_cfgPhantomBtn, lv_color_hex(COL_BORDER), 0);
        setLabelTextIfChanged(s_cfgPhantomLbl, "+48V OFF");
      }
    }
  }

  if (stripChanged || !s_cfgCache.init || s_cfgCache.invert != invert) {
    s_cfgCache.invert = invert;
    lv_obj_set_style_bg_color(s_cfgPolBtn, lv_color_hex(invert ? COL_AMBER : COL_PANEL2), 0);
    lv_obj_set_style_border_color(s_cfgPolBtn, lv_color_hex(invert ? 0xF2CC60 : COL_BORDER), 0);
    setLabelTextIfChanged(s_cfgPolLbl, invert ? "POL: INV" : "POL: NORM");
  }

  // Right card Section 2: Low Cut (HPF)
  setVisibleIfChanged(s_cfgHpfBox, k == STRIP_CH);
  setVisibleIfChanged(s_cfgHpfEmptyLbl, k != STRIP_CH);
  if (k == STRIP_CH) {
    const int32_t hInt = static_cast<int32_t>(std::lround(hpf * 1000.0f));
    if (stripChanged || !s_cfgCache.init || s_cfgCache.hpon != hpon || s_cfgCache.hpfInt != hInt) {
      s_cfgCache.hpon = hpon;
      s_cfgCache.hpfInt = hInt;
      lv_obj_set_style_bg_color(s_cfgHpfOnBtn, lv_color_hex(hpon ? COL_GREEN : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_cfgHpfOnBtn, lv_color_hex(hpon ? 0x3FB950 : COL_BORDER), 0);
      setLabelTextIfChanged(s_cfgHpfOnLbl, hpon ? "LOW CUT: ON" : "LOW CUT: OFF");

      char hzBuf[16];
      x32_format_hpf_hz(hpf, hzBuf, sizeof(hzBuf));
      setLabelTextIfChanged(s_cfgHpfFreqLbl, hzBuf);
      if (!lv_slider_is_dragged(s_cfgHpfSlider)) {
        lv_slider_set_value(s_cfgHpfSlider, hInt, LV_ANIM_OFF);
      }

      const int hzInt = static_cast<int>(std::lround(x32_hpf_to_hz(hpf)));
      static constexpr int kHpfPre[4] = {60, 80, 100, 120};
      for (int p = 0; p < 4; ++p) {
        const bool match = hpon && (hzInt >= kHpfPre[p] - 3 && hzInt <= kHpfPre[p] + 3);
        lv_obj_set_style_bg_color(s_cfgHpfPreBtns[p], lv_color_hex(match ? COL_ACCENT : COL_PANEL2), 0);
        lv_obj_set_style_border_color(s_cfgHpfPreBtns[p], lv_color_hex(match ? COL_CYAN : COL_BORDER), 0);
      }
    }
  }

  // Right card Section 3: Pan, Main LR & Ch Link
  const int32_t panPct = x32_pan_to_pct(pan);
  if (stripChanged || !s_cfgCache.init || s_cfgCache.panPct != panPct) {
    s_cfgCache.panPct = panPct;
    char panBuf[12];
    x32_format_pan(pan, panBuf, sizeof(panBuf));
    setLabelTextIfChanged(s_cfgPanValLbl, panBuf);
    if (!lv_slider_is_dragged(s_cfgPanSlider)) {
      lv_slider_set_value(s_cfgPanSlider, panPct, LV_ANIM_OFF);
    }
  }

  if (stripChanged || !s_cfgCache.init || s_cfgCache.mainSt != mainSt) {
    s_cfgCache.mainSt = mainSt;
    lv_obj_set_style_bg_color(s_cfgMainStBtn, lv_color_hex(mainSt ? COL_ACCENT : COL_PANEL2), 0);
    lv_obj_set_style_border_color(s_cfgMainStBtn, lv_color_hex(mainSt ? COL_CYAN : COL_BORDER), 0);
    setLabelTextIfChanged(s_cfgMainStLbl, mainSt ? "MAIN LR: ON" : "MAIN LR: OFF");
  }

  const bool chLink = (k == STRIP_CH) ? st.chLinks[idx / 2] : false;
  if (stripChanged || !s_cfgCache.init || s_cfgCache.chLink != chLink) {
    s_cfgCache.chLink = chLink;
    if (k == STRIP_CH) {
      lv_obj_clear_state(s_cfgLinkBtn, LV_STATE_DISABLED);
      const int first = (idx / 2) * 2 + 1;
      char lkBuf[24];
      std::snprintf(lkBuf, sizeof(lkBuf), "LINK %02d-%02d: %s", first, first + 1,
                    chLink ? "ON" : "OFF");
      setLabelTextIfChanged(s_cfgLinkLbl, lkBuf);
      lv_obj_set_style_text_color(s_cfgLinkLbl, lv_color_hex(COL_TEXT), 0);
      lv_obj_set_style_bg_color(s_cfgLinkBtn, lv_color_hex(chLink ? 0x102533 : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_cfgLinkBtn, lv_color_hex(chLink ? COL_CYAN : COL_BORDER), 0);
    } else {
      lv_obj_add_state(s_cfgLinkBtn, LV_STATE_DISABLED);
      setLabelTextIfChanged(s_cfgLinkLbl, "AUX LINK: FIXED");
      lv_obj_set_style_text_color(s_cfgLinkLbl, lv_color_hex(COL_MUTED), 0);
      lv_obj_set_style_bg_color(s_cfgLinkBtn, lv_color_hex(COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_cfgLinkBtn, lv_color_hex(COL_BORDER), 0);
    }
  }

  s_cfgCache.kind = k;
  s_cfgCache.idx = static_cast<int16_t>(idx);
  s_cfgCache.init = true;
}

void syncScribble(const MixerState &st) {
  if (!s_scCache.init || s_scCache.follow != st.followConsoleSelect) {
    s_scCache.follow = st.followConsoleSelect;
    setSwitchStateIfChanged(s_scFollowSw, st.followConsoleSelect);
  }

  if (st.selectedKind == STRIP_CH) {
    s_scCategory = (st.selectedIdx < 16) ? SC_CH1 : SC_CH2;
  } else if (st.selectedKind == STRIP_DCA) {
    s_scCategory = SC_DCA;
  } else if (st.selectedKind == STRIP_BUS) {
    s_scCategory = SC_BUS;
  } else if (st.selectedKind == STRIP_AUX) {
    s_scCategory = SC_AUX;
  } else if (st.selectedKind == STRIP_MAIN) {
    s_scCategory = SC_MAIN;
  }

  if (!s_scCache.init || s_scCache.cat != s_scCategory) {
    s_scCache.cat = s_scCategory;
    for (int i = 0; i < 6; ++i) {
      lv_obj_set_style_bg_color(
          s_scCatBtns[i], lv_color_hex(i == s_scCategory ? COL_ACCENT : COL_PANEL2), 0);
    }
  }

  StripKind k;
  int start = 0, cnt = 0;
  getScCategoryRange(s_scCategory, &k, &start, &cnt);

  for (int i = 0; i < 16; ++i) {
    const bool vis = (i < cnt);
    if (!s_scCache.init || s_scCache.btnVis[i] != vis) {
      s_scCache.btnVis[i] = vis;
      setVisibleIfChanged(s_scPickBtn[i], vis);
    }
    if (!vis) continue;

    const StripState *s = mixerGetStripConst(st, k, start + i);
    const bool isSel = (st.selectedKind == k && st.selectedIdx == (start + i));
    const uint8_t col = s ? s->color : 0;
    char tag[12];
    mixerFormatStripTag(k, start + i, tag, sizeof(tag));
    const char *nm = (s && s->name[0]) ? s->name : tag;
    if (!s_scCache.init || s_scCache.btnSel[i] != isSel ||
        s_scCache.btnCol[i] != col || std::strcmp(s_scCache.btnName[i], nm) != 0) {
      s_scCache.btnSel[i] = isSel;
      s_scCache.btnCol[i] = col;
      std::snprintf(s_scCache.btnName[i], sizeof(s_scCache.btnName[i]), "%s", nm);
      const X32ColorInfo ci = x32_get_color_info(col);
      lv_obj_set_style_bg_color(
          s_scPickBtn[i], lv_color_hex(isSel ? 0x1A2E48 : COL_PANEL2), 0);
      lv_obj_set_style_border_color(
          s_scPickBtn[i], lv_color_hex(isSel ? COL_CYAN : (ci.inverted ? ci.accent_rgb : ci.bg_rgb)), 0);
      lv_obj_set_style_border_width(s_scPickBtn[i], 2, 0);
      lv_label_set_text_fmt(s_scPickLbl[i], "%s\n%s", tag, nm);
    }
  }

  const bool stripSwitched = (s_scLastKind != st.selectedKind || s_scLastIdx != st.selectedIdx);
  if (stripSwitched) {
    const int selSlot = st.selectedIdx - start;
    if (selSlot >= 0 && selSlot < cnt && s_scPickBtn[selSlot]) {
      lv_obj_scroll_to_view(s_scPickBtn[selSlot], LV_ANIM_OFF);
    }
  }

  const StripState *sel = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
  if (sel) {
    const bool activelyTyping = (millis() - s_scLastTypeMs) < 1200;
    if (stripSwitched ||
        (!activelyTyping && std::strcmp(lv_textarea_get_text(s_scNameTa), sel->name) != 0)) {
      setScTextProgrammatic(sel->name);
      s_scLastKind = st.selectedKind;
      s_scLastIdx = st.selectedIdx;
      focusScTextArea();
    }

    const char *pnm = sel->name[0] ? sel->name : "(unnamed)";
    if (!s_scCache.init || s_scCache.selKind != st.selectedKind || s_scCache.selIdx != st.selectedIdx ||
        s_scCache.prevColor != sel->color) {
      s_scCache.selKind = st.selectedKind;
      s_scCache.selIdx = st.selectedIdx;
      s_scCache.prevColor = sel->color;

      char tag[16];
      mixerFormatStripTag(st.selectedKind, st.selectedIdx, tag, sizeof(tag));
      const X32ColorInfo ci = x32_get_color_info(sel->color);
      applyX32BannerStyle(s_scPrevBox, s_scPrevTag, s_scPrevName, sel->color);
      lv_obj_set_style_text_color(s_scPrevCode, lv_color_hex(ci.fg_rgb), 0);
      lv_obj_set_style_text_color(s_scPrevIconLbl, lv_color_hex(ci.fg_rgb), 0);
      lv_obj_set_style_border_color(s_scPrevIconBtn, lv_color_hex(ci.fg_rgb), 0);
      lv_obj_invalidate(s_scPrevIconBtn);
      lv_label_set_text(s_scPrevTag, tag);
      lv_label_set_text(s_scPrevCode, ci.code);

      const bool inv = (sel->color & 0x08) != 0;
      const uint8_t baseSel = sel->color & 0x07;
      for (int i = 0; i < 8; ++i) {
        const X32ColorInfo ciBtn = x32_get_color_info(static_cast<uint8_t>((inv ? 8 : 0) | i));
        lv_obj_set_style_bg_color(s_scSwatchBtn[i], lv_color_hex(ciBtn.bg_rgb), 0);
        lv_obj_set_style_border_color(
            s_scSwatchBtn[i], lv_color_hex(inv ? ciBtn.accent_rgb : COL_BORDER), 0);
        lv_obj_set_style_border_width(s_scSwatchBtn[i], inv ? 2 : 1, 0);
        lv_obj_set_style_text_color(
            lv_obj_get_child(s_scSwatchBtn[i], 0), lv_color_hex(ciBtn.fg_rgb), 0);
        lv_obj_set_style_outline_color(s_scSwatchBtn[i], lv_color_hex(COL_CYAN), 0);
        lv_obj_set_style_outline_width(s_scSwatchBtn[i], (i == baseSel) ? 2 : 0, 0);
      }
      lv_obj_set_style_bg_color(s_scInvertBtn, lv_color_hex(inv ? 0xE6EDF3 : COL_PANEL2), 0);
      lv_obj_set_style_border_color(
          s_scInvertBtn, lv_color_hex(inv ? COL_CYAN : COL_BORDER), 0);
      lv_obj_set_style_border_width(s_scInvertBtn, inv ? 2 : 1, 0);
      lv_obj_set_style_text_color(s_scInvertLbl, lv_color_hex(inv ? 0x11151C : COL_TEXT), 0);
      setLabelTextIfChanged(s_scInvertLbl, inv ? "Invert: ON" : "Invert");
    }
    if (!s_scCache.init || std::strcmp(s_scCache.prevName, pnm) != 0) {
      std::snprintf(s_scCache.prevName, sizeof(s_scCache.prevName), "%s", pnm);
      lv_label_set_text(s_scPrevName, pnm);
    }
    if (!s_scCache.init || s_scCache.prevIcon != sel->icon) {
      s_scCache.prevIcon = sel->icon;
      s_scPrevIconVal = sel->icon ? sel->icon : 1;
      lv_label_set_text_fmt(s_scPrevIconLbl, "#%d %s", sel->icon, x32_get_icon_name(sel->icon));
      lv_obj_invalidate(s_scPrevIconBtn);
    }
  }
  s_scCache.init = true;
}

void syncMatrix(const MixerState &st) {
  const bool modeChanged = !s_mxCache.init || (s_mxCache.mode != s_mxMode);
  const bool bankChanged = !s_mxCache.init || (s_mxCache.bank != s_mxBank);
  if (modeChanged) {
    s_mxCache.mode = s_mxMode;
    for (int i = 0; i < 3; ++i) {
      lv_obj_set_style_bg_color(
          s_mxModeBtns[i], lv_color_hex(i == s_mxMode ? COL_ACCENT : COL_PANEL2), 0);
    }
  }
  if (modeChanged || bankChanged) {
    s_mxCache.bank = s_mxBank;
    for (int i = 0; i < 5; ++i) {
      setVisibleIfChanged(s_mxBankBtns[i], s_mxMode != MX_LINK);
      if (s_mxMode != MX_LINK) {
        lv_obj_set_style_bg_color(
            s_mxBankBtns[i], lv_color_hex(i == s_mxBank ? COL_ACCENT : COL_PANEL2), 0);
      }
    }
  }

  if (s_mxMode == MX_LINK) {
    setVisibleIfChanged(s_mxMatrixPanel, false);
    setVisibleIfChanged(s_mxLinksPanel, true);
    for (int i = 0; i < 16; ++i) {
      const bool on = st.chLinks[i];
      const int a = i * 2, b = i * 2 + 1;
      if (modeChanged || s_mxCache.chLinks[i] != on) {
        s_mxCache.chLinks[i] = on;
        lv_obj_set_style_bg_color(s_mxLinkBtn[i], lv_color_hex(on ? 0x102533 : COL_PANEL2), 0);
        lv_obj_set_style_border_color(s_mxLinkBtn[i], lv_color_hex(on ? COL_CYAN : COL_BORDER), 0);
      }
      char buf[48];
      std::snprintf(buf, sizeof(buf), "Ch %d-%d [%s]\n%s / %s",
                    a + 1, b + 1, on ? "LINK" : "MONO", st.ch[a].name, st.ch[b].name);
      setLabelTextIfChanged(s_mxLinkLbl[i], buf);
    }
    for (int i = 0; i < 8; ++i) {
      const bool on = st.busLinks[i];
      const int a = i * 2, b = i * 2 + 1;
      if (modeChanged || s_mxCache.busLinks[i] != on) {
        s_mxCache.busLinks[i] = on;
        lv_obj_set_style_bg_color(s_mxLinkBtn[16 + i], lv_color_hex(on ? 0x102533 : COL_PANEL2), 0);
        lv_obj_set_style_border_color(s_mxLinkBtn[16 + i], lv_color_hex(on ? COL_CYAN : COL_BORDER), 0);
      }
      char buf[48];
      std::snprintf(buf, sizeof(buf), "Bus %d-%d [%s]\n%s / %s",
                    a + 1, b + 1, on ? "LINK" : "MONO", st.bus[a].name, st.bus[b].name);
      setLabelTextIfChanged(s_mxLinkLbl[16 + i], buf);
    }
    s_mxCache.init = true;
    return;
  }

  setVisibleIfChanged(s_mxMatrixPanel, true);
  setVisibleIfChanged(s_mxLinksPanel, false);

  const int cols = (s_mxMode == MX_DCA) ? 8 : 6;
  for (int c = 0; c < 8; ++c) {
    setVisibleIfChanged(s_mxColHdrBtn[c], c < cols);
    if (c >= cols) continue;
    if (s_mxMode == MX_DCA) {
      if (modeChanged || s_mxCache.dcaCol[c] != st.dca[c].color ||
          std::strcmp(s_mxCache.dcaName[c], st.dca[c].name) != 0) {
        s_mxCache.dcaCol[c] = st.dca[c].color;
        std::snprintf(s_mxCache.dcaName[c], sizeof(s_mxCache.dcaName[c]), "%s", st.dca[c].name);
        const X32ColorInfo ci = x32_get_color_info(st.dca[c].color);
        lv_obj_set_style_bg_color(s_mxColHdrBtn[c], lv_color_hex(COL_PANEL2), 0);
        lv_obj_set_style_border_color(
            s_mxColHdrBtn[c], lv_color_hex(ci.inverted ? ci.accent_rgb : ci.bg_rgb), 0);
        lv_obj_set_style_border_width(s_mxColHdrBtn[c], 2, 0);
        lv_label_set_text_fmt(s_mxColHdrLbl[c], "DCA %d\n%s", c + 1, st.dca[c].name);
      }
    } else {
      const bool on = st.muteGroupEngaged[c];
      if (modeChanged || s_mxCache.mg[c] != on) {
        s_mxCache.mg[c] = on;
        lv_obj_set_style_bg_color(s_mxColHdrBtn[c], lv_color_hex(on ? COL_RED : COL_PANEL2), 0);
        lv_obj_set_style_border_color(s_mxColHdrBtn[c], lv_color_hex(on ? 0xFF7B72 : COL_BORDER), 0);
        lv_label_set_text_fmt(s_mxColHdrLbl[c], "MG %d\n%s", c + 1, on ? "MUTE" : "OFF");
      }
    }
  }

  const StripKind kind = (s_mxBank == 4) ? STRIP_AUX : STRIP_CH;
  const int base = (s_mxBank == 4) ? 0 : (s_mxBank * 8);
  for (int r = 0; r < 8; ++r) {
    const StripState *s = mixerGetStripConst(st, kind, base + r);
    const char *snm = (s && s->name[0]) ? s->name : "";
    if (modeChanged || bankChanged || std::strcmp(s_mxCache.rowName[r], snm) != 0) {
      std::snprintf(s_mxCache.rowName[r], sizeof(s_mxCache.rowName[r]), "%s", snm);
      char tag[12];
      mixerFormatStripTag(kind, base + r, tag, sizeof(tag));
      lv_label_set_text_fmt(s_mxRowLbl[r], "%s %s", tag, snm);
    }

    const uint8_t curMask = s ? ((s_mxMode == MX_DCA) ? s->dcaMask : s->muteMask) : 0;
    const uint8_t prevMask = s_mxCache.cellMask[r];
    if (modeChanged || bankChanged || curMask != prevMask) {
      s_mxCache.cellMask[r] = curMask;
      for (int c = 0; c < 8; ++c) {
        setVisibleIfChanged(s_mxCellBtn[r][c], c < cols);
        if (c >= cols) continue;
        const bool on = ((curMask >> c) & 1u) != 0;
        lv_obj_set_style_bg_color(
            s_mxCellBtn[r][c],
            lv_color_hex(on ? (s_mxMode == MX_DCA ? COL_ACCENT : COL_RED) : 0x11161E), 0);
        if (s_mxMode == MX_DCA) {
          lv_label_set_text_fmt(s_mxCellLbl[r][c], on ? "%d" : "-", c + 1);
        } else {
          lv_label_set_text_fmt(s_mxCellLbl[r][c], on ? "M%d" : "-", c + 1);
        }
      }
    }
  }
  s_mxCache.init = true;
}

void syncFaders(const MixerState &st) {
  static FaderBankId s_lastFb = FB_CH1;
  static int8_t s_lastBus = -99;
  const bool bankChanged = (s_lastFb != s_faderBank);
  if (bankChanged || s_lastBus == -99) {
    s_lastFb = s_faderBank;
    for (int i = 0; i < 8; ++i) {
      lv_obj_set_style_bg_color(
          s_fbBtns[i], lv_color_hex(i == s_faderBank ? COL_ACCENT : COL_PANEL2), 0);
    }
  }

  static char s_lastOpts[768] = "";
  char opts[768];
  int off = std::snprintf(opts, sizeof(opts), "Main LR (Channel Faders)");
  for (int b = 0; b < NUM_BUSES; ++b) {
    off += std::snprintf(opts + off, sizeof(opts) - off, "\nSend -> Bus %02d: %s",
                         b + 1, st.bus[b].name);
  }
  if (!lv_dropdown_is_open(s_busDropdown) && std::strcmp(s_lastOpts, opts) != 0) {
    std::snprintf(s_lastOpts, sizeof(s_lastOpts), "%s", opts);
    lv_dropdown_set_options(s_busDropdown, opts);
  }
  if (s_lastBus != st.activeBusSend) {
    s_lastBus = st.activeBusSend;
    lv_dropdown_set_selected(s_busDropdown, static_cast<uint16_t>(st.activeBusSend + 1));
    const bool sof = (st.activeBusSend >= 0);
    lv_obj_set_style_bg_color(s_exitSofBtn, lv_color_hex(sof ? COL_AMBER : COL_PANEL2), 0);
    lv_obj_t *lbl = lv_obj_get_child(s_exitSofBtn, 0);
    if (lbl) {
      lv_label_set_text(lbl, sof ? "Exit SOF" : "Main LR");
      lv_obj_set_style_text_color(lbl, lv_color_hex(sof ? 0x11151C : COL_MUTED), 0);
    }
  }

  StripKind k;
  int start = 0;
  getFaderBankRange(s_faderBank, &k, &start);
  const bool isSendMode = (st.activeBusSend >= 0 && k == STRIP_CH);

  for (int i = 0; i < 9; ++i) {
    StripKind sk = (i == 8) ? (st.activeBusSend >= 0 ? STRIP_BUS : STRIP_MAIN) : k;
    int sidx = (i == 8) ? (st.activeBusSend >= 0 ? st.activeBusSend : 0) : (start + i);
    const StripState *s = mixerGetStripConst(st, sk, sidx);
    if (!s) continue;

    FaderSlotCache &fc = s_fSlotCache[i];
    const bool sendSlot = (i < 8 && isSendMode);
    const float val = sendSlot ? st.chSendLevel[sidx][st.activeBusSend] : s->fader;
    const bool directMuted = sendSlot ? !st.chSendOn[sidx][st.activeBusSend] : s->muted;
    const bool grpOnly = sendSlot ? false : mixerIsStripGroupMutedOnly(st, sk, sidx);
    const bool effMuted = directMuted || grpOnly;
    char defTag[14];
    const char *nm = s->name;
    if (!nm[0]) {
      mixerFormatStripTag(sk, sidx, defTag, sizeof(defTag));
      nm = defTag;
    }
    const int32_t fInt = static_cast<int32_t>(val * 1000.0f);

    char db[16];
    x32_format_db(val, db, sizeof(db));

    if (!fc.init || fc.kind != sk || fc.idx != sidx || fc.color != s->color ||
        std::strcmp(fc.name, nm) != 0) {
      fc.kind = sk;
      fc.idx = sidx;
      fc.color = s->color;
      std::snprintf(fc.name, sizeof(fc.name), "%s", nm);
      char tag[14];
      mixerFormatStripTag(sk, sidx, tag, sizeof(tag));
      applyX32BannerStyle(s_fCapBtn[i], s_fCapTag[i], s_fCapName[i], s->color);
      lv_label_set_text(s_fCapTag[i], tag);
      lv_label_set_text(s_fCapName[i], nm);
    }
    if (!fc.init || std::strcmp(fc.db, db) != 0) {
      std::snprintf(fc.db, sizeof(fc.db), "%s", db);
      lv_label_set_text(s_fDbLbl[i], db);
    }
    if (!fc.init || fc.meterPct != s->meterPct) {
      fc.meterPct = s->meterPct;
      lv_bar_set_value(s_fVuBar[i], s->meterPct, LV_ANIM_OFF);
    }
    if (!fc.init || fc.isSend != sendSlot) {
      fc.isSend = sendSlot;
      const uint32_t indCol = (i == 8 || sendSlot) ? COL_AMBER : COL_CYAN;
      lv_obj_set_style_bg_color(s_fSlider[i], lv_color_hex(indCol), LV_PART_INDICATOR);
    }
    if ((!fc.init || fc.faderInt != fInt) && !lv_slider_is_dragged(s_fSlider[i])) {
      fc.faderInt = fInt;
      lv_slider_set_value(s_fSlider[i], fInt, LV_ANIM_OFF);
    }
    if (!fc.init || fc.solo != s->solo) {
      fc.solo = s->solo;
      applySoloBtnStyle(s_fSoloBtn[i], s->solo);
    }
    if (!fc.init || fc.effMuted != effMuted || fc.grpMutedOnly != grpOnly) {
      fc.effMuted = effMuted;
      fc.grpMutedOnly = grpOnly;
      applyMuteBtnStyle(s_fMuteBtn[i], directMuted, grpOnly);
      lv_label_set_text(lv_obj_get_child(s_fMuteBtn[i], 0), grpOnly ? "M*" : "M");
    }
    fc.init = true;
  }
}

void syncNetwork(const MixerState &st) {
  if (!s_netCache.init || s_netCache.wifiScanning != st.wifiScanning) {
    s_netCache.wifiScanning = st.wifiScanning;
    if (s_netScanBtn && s_netScanLbl) {
      if (st.wifiScanning) {
        lv_obj_add_state(s_netScanBtn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_netScanBtn, lv_color_hex(COL_PANEL2), 0);
        lv_label_set_text(s_netScanLbl, "Scanning...");
        lv_obj_set_style_text_color(s_netScanLbl, lv_color_hex(COL_MUTED), 0);
      } else {
        lv_obj_clear_state(s_netScanBtn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_netScanBtn, lv_color_hex(COL_ACCENT), 0);
        lv_label_set_text(s_netScanLbl, "Scan Wi-Fi");
        lv_obj_set_style_text_color(s_netScanLbl, lv_color_hex(COL_TEXT), 0);
      }
    }
  }

  char wStatus[96];
  uint32_t wCol = COL_CYAN;
  if (st.wifiConnected) {
    std::snprintf(wStatus, sizeof(wStatus), "Connected: %s (%s, %d dBm)",
                  st.wifiSsid, st.staIp, st.wifiRssi);
    wCol = COL_GREEN;
  } else if (st.wifiScanning) {
    std::snprintf(wStatus, sizeof(wStatus), "Scanning 2.4 GHz Wi-Fi...");
    wCol = COL_AMBER;
  } else if (st.wifiConnecting) {
    std::snprintf(wStatus, sizeof(wStatus), "Connecting to %s...", st.wifiSsid);
    wCol = COL_AMBER;
  } else if (st.wifiStatusMsg[0] != '\0') {
    std::snprintf(wStatus, sizeof(wStatus), "%s", st.wifiStatusMsg);
  } else {
    std::snprintf(wStatus, sizeof(wStatus), "Fallback AP: X32-Companion (%s)", st.apIp);
  }
  if (!s_netCache.init || std::strcmp(s_netCache.wifiStatus, wStatus) != 0 ||
      s_netCache.wifiStatusCol != wCol) {
    std::snprintf(s_netCache.wifiStatus, sizeof(s_netCache.wifiStatus), "%s", wStatus);
    s_netCache.wifiStatusCol = wCol;
    lv_label_set_text(s_netWifiStatusLbl, wStatus);
    lv_obj_set_style_text_color(s_netWifiStatusLbl, lv_color_hex(wCol), 0);
  }

  const uint8_t scanDoneFlag = st.wifiScanDone ? 1u : 0u;
  const uint8_t scanningFlag = st.wifiScanning ? 1u : 0u;
  uint32_t wHash = 2166136261u;
  wHash = fnv1aUpdate(wHash, &scanDoneFlag, 1);
  wHash = fnv1aUpdate(wHash, &scanningFlag, 1);
  wHash = fnv1aUpdate(wHash, &st.wifiScanCount, 1);
  for (uint8_t i = 0; i < st.wifiScanCount; ++i) {
    wHash = fnv1aUpdate(wHash, st.wifiScan[i].ssid, std::strlen(st.wifiScan[i].ssid));
    wHash = fnv1aUpdate(wHash, &st.wifiScan[i].rssi, 1);
    wHash = fnv1aUpdate(wHash, &st.wifiScan[i].secure, 1);
  }
  if (!s_netCache.init || s_netCache.wifiHash != wHash) {
    s_netCache.wifiHash = wHash;
    lv_obj_clean(s_netWifiList);
    if (st.wifiScanCount > 0) {
      for (uint8_t i = 0; i < st.wifiScanCount; ++i) {
        char rowTxt[56];
        std::snprintf(rowTxt, sizeof(rowTxt), "%s (%d dBm)%s",
                      st.wifiScan[i].ssid, st.wifiScan[i].rssi,
                      st.wifiScan[i].secure ? " *" : "");
        lv_obj_t *btn = lv_list_add_btn(s_netWifiList, LV_SYMBOL_WIFI, rowTxt);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_PANEL2), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
        lv_obj_add_event_cb(btn, onNetWifiItemClick, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
      }
    } else if (st.wifiScanDone && !st.wifiScanning) {
      lv_obj_t *emptyItem = lv_list_add_text(s_netWifiList, "No 2.4 GHz Wi-Fi found (5 GHz N/A)");
      lv_obj_set_style_text_color(emptyItem, lv_color_hex(COL_MUTED), 0);
    }
  }

  char xStatus[96];
  if (st.x32Connected) {
    std::snprintf(xStatus, sizeof(xStatus), "Connected: %s (%s) Sync %u%%",
                  st.x32Name, st.x32Ip, st.syncProgress);
  } else {
    std::snprintf(xStatus, sizeof(xStatus), "%s", st.statusMsg);
  }
  if (!s_netCache.init || std::strcmp(s_netCache.x32Status, xStatus) != 0) {
    std::snprintf(s_netCache.x32Status, sizeof(s_netCache.x32Status), "%s", xStatus);
    lv_label_set_text(s_netX32StatusLbl, xStatus);
  }

  uint32_t cHash = 2166136261u;
  cHash = fnv1aUpdate(cHash, &st.consoleCount, 1);
  for (uint8_t i = 0; i < st.consoleCount; ++i) {
    cHash = fnv1aUpdate(cHash, st.consoles[i].ip, std::strlen(st.consoles[i].ip));
    cHash = fnv1aUpdate(cHash, st.consoles[i].name, std::strlen(st.consoles[i].name));
  }
  if (!s_netCache.init || s_netCache.consoleHash != cHash) {
    s_netCache.consoleHash = cHash;
    lv_obj_clean(s_netConsoleList);
    for (uint8_t i = 0; i < st.consoleCount; ++i) {
      char cTxt[64];
      std::snprintf(cTxt, sizeof(cTxt), "%s [%s] - %s",
                    st.consoles[i].name, st.consoles[i].model, st.consoles[i].ip);
      lv_obj_t *btn = lv_list_add_btn(s_netConsoleList, LV_SYMBOL_AUDIO, cTxt);
      lv_obj_set_style_bg_color(btn, lv_color_hex(COL_PANEL2), 0);
      lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
      lv_obj_add_event_cb(btn, onNetX32ConsoleClick, LV_EVENT_CLICKED,
                          reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
  }

  if (!s_netCache.init || std::strcmp(s_netCache.savedSsid, st.savedWifiSsid) != 0) {
    std::snprintf(s_netCache.savedSsid, sizeof(s_netCache.savedSsid), "%s", st.savedWifiSsid);
    if (st.savedWifiSsid[0] != '\0') lv_textarea_set_text(s_netSsidTa, st.savedWifiSsid);
  }
  if (!s_netCache.init || std::strcmp(s_netCache.x32Ip, st.x32Ip) != 0) {
    std::snprintf(s_netCache.x32Ip, sizeof(s_netCache.x32Ip), "%s", st.x32Ip);
    if (st.x32Ip[0] != '\0') lv_textarea_set_text(s_netX32IpTa, st.x32Ip);
  }

  if (!s_netCache.init || s_netCache.backlightOn != st.backlightOn) {
    s_netCache.backlightOn = st.backlightOn;
    lv_label_set_text(s_netBlLbl, st.backlightOn ? "Sleep LCD" : "LCD: OFF");
  }

  if (!s_netCache.init || s_netCache.screenTimeoutSec != st.screenTimeoutSec) {
    s_netCache.screenTimeoutSec = st.screenTimeoutSec;
    char tBuf[32];
    formatScreenTimeoutLabel(st.screenTimeoutSec, tBuf, sizeof(tBuf));
    lv_label_set_text(s_netIdleLbl, tBuf);
  }

  if (!s_netCache.init || std::strcmp(s_netCache.webPasskey, st.webPasskey) != 0) {
    std::snprintf(s_netCache.webPasskey, sizeof(s_netCache.webPasskey), "%s", st.webPasskey);
    lv_label_set_text_fmt(s_netPasskeyLbl, "PIN: %s",
                          st.webPasskey[0] ? st.webPasskey : "------");
  }

  char webBuf[128];
  std::snprintf(webBuf, sizeof(webBuf),
                "Web UI: http://%s/\n"
                "mDNS: http://x32-companion.local/\n"
                "Paired WS: %u  (PIN: 15m / 1-use)",
                st.wifiConnected ? st.staIp : st.apIp, st.wsClientCount);
  if (!s_netCache.init || std::strcmp(s_netCache.webUrlText, webBuf) != 0) {
    std::snprintf(s_netCache.webUrlText, sizeof(s_netCache.webUrlText), "%s", webBuf);
    lv_label_set_text(s_netWebUrlLbl, webBuf);
  }

  if (s_otaInstallArmed && st.otaPhase != OTA_AVAILABLE) {
    s_otaInstallArmed = false;
  }
  const bool otaDirty = !s_netCache.init ||
                        s_netCache.wifiConnected != st.wifiConnected ||
                        s_netCache.otaPhase != st.otaPhase ||
                        s_netCache.otaProgressPct != st.otaProgressPct ||
                        s_netCache.otaInstallArmed != s_otaInstallArmed ||
                        std::strcmp(s_netCache.otaLatestVer, st.otaLatestVer) != 0 ||
                        std::strcmp(s_netCache.otaStatusMsg, st.otaStatusMsg) != 0;
  if (otaDirty) {
    s_netCache.wifiConnected = st.wifiConnected;
    s_netCache.otaPhase = st.otaPhase;
    s_netCache.otaProgressPct = st.otaProgressPct;
    s_netCache.otaInstallArmed = s_otaInstallArmed;
    std::snprintf(s_netCache.otaLatestVer, sizeof(s_netCache.otaLatestVer), "%s", st.otaLatestVer);
    std::snprintf(s_netCache.otaStatusMsg, sizeof(s_netCache.otaStatusMsg), "%s", st.otaStatusMsg);

    const bool otaBusy = (st.otaPhase == OTA_CHECKING || st.otaPhase == OTA_DOWNLOADING ||
                          st.otaPhase == OTA_VERIFYING || st.otaPhase == OTA_REBOOTING);
    const bool canCheck = st.wifiConnected && !otaBusy;
    const bool canInstall = st.wifiConnected && st.otaPhase == OTA_AVAILABLE && st.otaLatestVer[0] != '\0';

    char verBuf[64];
    if (st.otaLatestVer[0] != '\0') {
      std::snprintf(verBuf, sizeof(verBuf), "FW v%s -> v%s",
                    X32_COMPANION_FW_VERSION, st.otaLatestVer);
    } else {
      std::snprintf(verBuf, sizeof(verBuf), "FW v%s (GitHub OTA)",
                    X32_COMPANION_FW_VERSION);
    }
    lv_label_set_text(s_netOtaVerLbl, verBuf);

    const char *statusTxt = st.otaStatusMsg;
    if (statusTxt[0] == '\0') {
      statusTxt = st.wifiConnected ? "Tap Check for latest release" : "Connect Wi-Fi to check updates";
    }
    uint32_t statusCol = COL_MUTED;
    switch (st.otaPhase) {
      case OTA_ERROR:       statusCol = COL_RED;   break;
      case OTA_AVAILABLE:
      case OTA_REBOOTING:   statusCol = COL_GREEN; break;
      case OTA_UP_TO_DATE:  statusCol = COL_CYAN;  break;
      case OTA_CHECKING:
      case OTA_DOWNLOADING:
      case OTA_VERIFYING:   statusCol = COL_AMBER; break;
      default:              statusCol = COL_MUTED; break;
    }
    lv_label_set_text(s_netOtaStatusLbl, statusTxt);
    lv_obj_set_style_text_color(s_netOtaStatusLbl, lv_color_hex(statusCol), 0);

    if (canCheck) {
      lv_obj_clear_state(s_netOtaCheckBtn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(s_netOtaCheckBtn, lv_color_hex(COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_netOtaCheckBtn, lv_color_hex(COL_BORDER), 0);
      lv_obj_set_style_text_color(s_netOtaCheckLbl, lv_color_hex(COL_TEXT), 0);
      lv_label_set_text(s_netOtaCheckLbl, "Check");
    } else {
      lv_obj_add_state(s_netOtaCheckBtn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(s_netOtaCheckBtn, lv_color_hex(0x111722), 0);
      lv_obj_set_style_border_color(s_netOtaCheckBtn, lv_color_hex(COL_BORDER), 0);
      lv_obj_set_style_text_color(s_netOtaCheckLbl, lv_color_hex(COL_MUTED), 0);
      lv_label_set_text(s_netOtaCheckLbl, otaBusy ? "..." : "Check");
    }

    if (!canInstall) {
      lv_obj_add_state(s_netOtaInstallBtn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(s_netOtaInstallBtn, lv_color_hex(0x111722), 0);
      lv_obj_set_style_border_color(s_netOtaInstallBtn, lv_color_hex(COL_BORDER), 0);
      lv_obj_set_style_text_color(s_netOtaInstallLbl, lv_color_hex(COL_MUTED), 0);
      const bool installing = (st.otaPhase == OTA_DOWNLOADING ||
                               st.otaPhase == OTA_VERIFYING ||
                               st.otaPhase == OTA_REBOOTING);
      lv_label_set_text(s_netOtaInstallLbl, installing ? "Busy" : "Install");
    } else if (s_otaInstallArmed) {
      lv_obj_clear_state(s_netOtaInstallBtn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(s_netOtaInstallBtn, lv_color_hex(COL_AMBER), 0);
      lv_obj_set_style_border_color(s_netOtaInstallBtn, lv_color_hex(0xF2CC60), 0);
      lv_obj_set_style_text_color(s_netOtaInstallLbl, lv_color_hex(COL_TEXT), 0);
      lv_label_set_text(s_netOtaInstallLbl, "Sure?");
    } else {
      lv_obj_clear_state(s_netOtaInstallBtn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(s_netOtaInstallBtn, lv_color_hex(COL_GREEN), 0);
      lv_obj_set_style_border_color(s_netOtaInstallBtn, lv_color_hex(0x3FB950), 0);
      lv_obj_set_style_text_color(s_netOtaInstallLbl, lv_color_hex(COL_TEXT), 0);
      lv_label_set_text(s_netOtaInstallLbl, "Install");
    }

    const int pct = (st.otaProgressPct > 100) ? 100 : static_cast<int>(st.otaProgressPct);
    lv_bar_set_value(s_netOtaBar, pct, LV_ANIM_OFF);
    uint32_t barCol = COL_CYAN;
    if (st.otaPhase == OTA_ERROR) {
      barCol = COL_RED;
    } else if (st.otaPhase == OTA_REBOOTING || pct == 100) {
      barCol = COL_GREEN;
    }
    lv_obj_set_style_bg_color(s_netOtaBar, lv_color_hex(barCol), LV_PART_INDICATOR);
  }
  s_netCache.init = true;
}

void syncLibrary(const MixerState &st) {
  if (s_libBank > 3) s_libBank = 0;
  if (s_libSelSlot < 0 || s_libSelSlot >= NUM_SCENES) s_libSelSlot = s_libBank * 8;

  const uint8_t curMode = static_cast<uint8_t>(s_libMode);
  const bool modeChanged = !s_libCache.init || s_libCache.mode != curMode;
  const bool bankChanged = !s_libCache.init || s_libCache.bank != s_libBank;
  const bool selChanged = !s_libCache.init || s_libCache.selSlot != s_libSelSlot;
  const bool curScnChanged = !s_libCache.init || s_libCache.curScene != st.currentScene;
  const bool scopeChanged = !s_libCache.init || s_libCache.scope != s_libScope;
  const bool armedChanged = !s_libCache.init || s_libCache.armed != s_libLoadArmed;

  const StripState *selSp = mixerGetStripConst(st, st.selectedKind, st.selectedIdx);
  const uint8_t curStripCol = selSp ? selSp->color : 0;
  const char *curStripName = selSp ? selSp->name : "";
  const bool stripChanged = !s_libCache.init || s_libCache.selKind != st.selectedKind ||
                            s_libCache.selIdx != st.selectedIdx ||
                            s_libCache.selStripCol != curStripCol ||
                            std::strcmp(s_libCache.selStripName, curStripName) != 0;

  if (modeChanged) {
    for (int m = 0; m < 2; ++m) {
      const bool on = (m == curMode);
      lv_obj_set_style_bg_color(s_libModeBtns[m], lv_color_hex(on ? COL_ACCENT : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_libModeBtns[m], lv_color_hex(on ? COL_CYAN : COL_BORDER), 0);
    }
    static const char *const kScnBanks[4] = {"00-07", "08-15", "16-23", "24-31"};
    static const char *const kPreBanks[4] = {"01-08", "09-16", "17-24", "25-32"};
    for (int b = 0; b < 4; ++b) {
      lv_label_set_text(lv_obj_get_child(s_libBankBtns[b], 0),
                        (s_libMode == LIB_SCENES) ? kScnBanks[b] : kPreBanks[b]);
    }
  }

  if (bankChanged) {
    for (int b = 0; b < 4; ++b) {
      const bool on = (b == s_libBank);
      lv_obj_set_style_bg_color(s_libBankBtns[b], lv_color_hex(on ? COL_ACCENT : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_libBankBtns[b], lv_color_hex(on ? COL_CYAN : COL_BORDER), 0);
    }
  }

  char infoBuf[64];
  if (s_libMode == LIB_SCENES) {
    if (st.currentScene >= 0 && st.currentScene < NUM_SCENES) {
      const char *curNm = st.scenes[st.currentScene].name[0] ? st.scenes[st.currentScene].name
                                                             : "Untitled";
      std::snprintf(infoBuf, sizeof(infoBuf), "Active %02d: %s", st.currentScene, curNm);
    } else {
      std::snprintf(infoBuf, sizeof(infoBuf), "Scenes 00-31");
    }
  } else {
    std::snprintf(infoBuf, sizeof(infoBuf), "Ch Presets 01-32 | Scope 0x%02X", s_libScope);
  }
  setLabelTextIfChanged(s_libInfoLbl, infoBuf);

  bool anyCardChanged = false;
  const int base = s_libBank * 8;
  for (int i = 0; i < 8; ++i) {
    const int slot = base + i;
    bool has = false;
    bool isCur = false;
    const char *rawName = "";
    const char *rawSub = "";
    char numBuf[24];
    char nameBuf[24];
    char subBuf[36];

    if (s_libMode == LIB_SCENES) {
      has = (slot < NUM_SCENES) && st.scenes[slot].hasData;
      isCur = (slot == st.currentScene);
      rawName = (slot < NUM_SCENES) ? st.scenes[slot].name : "";
      rawSub = (slot < NUM_SCENES) ? st.scenes[slot].notes : "";
      std::snprintf(numBuf, sizeof(numBuf), isCur ? "SCN %02d  [ACTIVE]" : "SCN %02d", slot);
      std::snprintf(nameBuf, sizeof(nameBuf), "%s", rawName[0] ? rawName : "(empty)");
      std::snprintf(subBuf, sizeof(subBuf), "%s",
                    rawSub[0] ? rawSub : (has ? "Saved scene" : "Empty slot"));
    } else {
      has = (slot < NUM_CH_PRESETS) && st.chPresets[slot].hasData;
      rawName = (slot < NUM_CH_PRESETS) ? st.chPresets[slot].name : "";
      std::snprintf(numBuf, sizeof(numBuf), "PRE %02d", slot + 1);
      std::snprintf(nameBuf, sizeof(nameBuf), "%s", rawName[0] ? rawName : "(empty)");
      std::snprintf(subBuf, sizeof(subBuf), "%s", has ? "Channel library" : "Empty slot");
    }

    const bool isSel = (slot == s_libSelSlot);
    const bool cardNeedsUpdate =
        modeChanged || bankChanged || selChanged || curScnChanged ||
        s_libCache.slotHas[i] != has || std::strcmp(s_libCache.slotName[i], nameBuf) != 0 ||
        std::strcmp(s_libCache.slotSub[i], subBuf) != 0;

    if (cardNeedsUpdate) {
      anyCardChanged = true;
      s_libCache.slotHas[i] = has;
      std::snprintf(s_libCache.slotName[i], sizeof(s_libCache.slotName[i]), "%s", nameBuf);
      std::snprintf(s_libCache.slotSub[i], sizeof(s_libCache.slotSub[i]), "%s", subBuf);

      lv_obj_set_style_bg_color(
          s_libCard[i], lv_color_hex(isSel ? 0x1A2940 : (has ? COL_PANEL : 0x10151C)), 0);
      lv_obj_set_style_border_color(
          s_libCard[i], lv_color_hex(isSel ? COL_ACCENT : (isCur ? COL_GREEN : COL_BORDER)), 0);
      lv_obj_set_style_text_color(
          s_libCardNum[i], lv_color_hex(isCur ? COL_GREEN : (isSel ? COL_CYAN : COL_MUTED)), 0);
      lv_obj_set_style_text_color(s_libCardName[i], lv_color_hex(has ? COL_TEXT : COL_MUTED), 0);

      setLabelTextIfChanged(s_libCardNum[i], numBuf);
      setLabelTextIfChanged(s_libCardName[i], nameBuf);
      setLabelTextIfChanged(s_libCardSub[i], subBuf);
    }
  }

  if (modeChanged || bankChanged || selChanged || curScnChanged || scopeChanged || armedChanged ||
      stripChanged || anyCardChanged) {
    char titleBuf[48];
    char subBuf[72];
    bool canLoad = false;
    const bool badDca = (s_libMode == LIB_PRESETS && st.selectedKind == STRIP_DCA);

    if (s_libMode == LIB_SCENES) {
      const char *nm = (s_libSelSlot < NUM_SCENES && st.scenes[s_libSelSlot].name[0])
                           ? st.scenes[s_libSelSlot].name
                           : "(empty)";
      canLoad = (s_libSelSlot < NUM_SCENES && st.scenes[s_libSelSlot].hasData);
      std::snprintf(titleBuf, sizeof(titleBuf), "Scene %02d: %s", s_libSelSlot, nm);
      if (s_libSelSlot < NUM_SCENES && st.scenes[s_libSelSlot].notes[0]) {
        std::snprintf(subBuf, sizeof(subBuf), "Notes: %s", st.scenes[s_libSelSlot].notes);
      } else {
        std::snprintf(subBuf, sizeof(subBuf), "%s",
                      canLoad ? "Full mixer snapshot recall / overwrite" : "Empty scene slot");
      }
    } else {
      const char *nm = (s_libSelSlot < NUM_CH_PRESETS && st.chPresets[s_libSelSlot].name[0])
                           ? st.chPresets[s_libSelSlot].name
                           : "(empty)";
      canLoad = (!badDca && s_libSelSlot < NUM_CH_PRESETS && st.chPresets[s_libSelSlot].hasData &&
                 s_libScope != 0);
      std::snprintf(titleBuf, sizeof(titleBuf), "Preset %02d: %s", s_libSelSlot + 1, nm);
      if (badDca) {
        std::snprintf(subBuf, sizeof(subBuf), "Select Ch/Aux/Bus/Main to load or save preset");
      } else {
        std::snprintf(subBuf, sizeof(subBuf), "%s",
                      (s_libSelSlot < NUM_CH_PRESETS && st.chPresets[s_libSelSlot].hasData)
                          ? "Applies checked scope to target strip"
                          : "Empty preset slot (Save copies target strip)");
      }
    }

    setLabelTextIfChanged(s_libSelTitleLbl, titleBuf);
    setLabelTextIfChanged(s_libSelSubLbl, subBuf);
    setVisibleIfChanged(s_libTargetBar, s_libMode == LIB_PRESETS);
    setVisibleIfChanged(s_libScopeBar, s_libMode == LIB_PRESETS);

    char tag[12];
    mixerFormatStripTag(st.selectedKind, st.selectedIdx, tag, sizeof(tag));
    char tgtBuf[36];
    if (st.selectedKind == STRIP_DCA) {
      std::snprintf(tgtBuf, sizeof(tgtBuf), "%s (N/A)", tag);
    } else if (curStripName[0]) {
      std::snprintf(tgtBuf, sizeof(tgtBuf), "%s  %s", tag, curStripName);
    } else {
      std::snprintf(tgtBuf, sizeof(tgtBuf), "%s", tag);
    }
    setLabelTextIfChanged(s_libTargetLbl, tgtBuf);
    lv_obj_t *tgtBox = lv_obj_get_parent(s_libTargetLbl);
    if (tgtBox) {
      const X32ColorInfo ci = x32_get_color_info(curStripCol);
      lv_obj_set_style_border_color(
          tgtBox, lv_color_hex((st.selectedKind == STRIP_DCA) ? COL_RED : ci.accent_rgb), 0);
    }

    for (int i = 0; i < 6; ++i) {
      const bool on = (s_libScope & (1u << i)) != 0;
      lv_obj_set_style_bg_color(s_libScopeBtns[i], lv_color_hex(on ? COL_ACCENT : COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_libScopeBtns[i], lv_color_hex(on ? COL_CYAN : COL_BORDER), 0);
    }

    if (!canLoad) {
      lv_obj_set_style_bg_color(s_libLoadBtn, lv_color_hex(COL_PANEL2), 0);
      lv_obj_set_style_border_color(s_libLoadBtn, lv_color_hex(COL_BORDER), 0);
      setLabelTextIfChanged(s_libLoadLbl, badDca ? "Select Ch" : "Empty");
    } else if (s_libLoadArmed) {
      lv_obj_set_style_bg_color(s_libLoadBtn, lv_color_hex(COL_AMBER), 0);
      lv_obj_set_style_border_color(s_libLoadBtn, lv_color_hex(0xF2CC60), 0);
      setLabelTextIfChanged(s_libLoadLbl, "Tap Again\nConfirm");
    } else {
      lv_obj_set_style_bg_color(s_libLoadBtn, lv_color_hex(COL_GREEN), 0);
      lv_obj_set_style_border_color(s_libLoadBtn, lv_color_hex(0x3FB950), 0);
      setLabelTextIfChanged(s_libLoadLbl, "Load");
    }

    const bool canSave = (s_libMode == LIB_SCENES) || !badDca;
    lv_obj_set_style_bg_color(s_libSaveBtn, lv_color_hex(canSave ? COL_ACCENT : COL_PANEL2), 0);
    lv_obj_set_style_border_color(s_libSaveBtn, lv_color_hex(canSave ? COL_CYAN : COL_BORDER), 0);
    setLabelTextIfChanged(s_libSaveLbl,
                          (s_libMode == LIB_SCENES) ? "Save Scene..." : "Save Preset...");
  }

  s_libCache.init = true;
  s_libCache.mode = curMode;
  s_libCache.bank = s_libBank;
  s_libCache.selSlot = s_libSelSlot;
  s_libCache.curScene = st.currentScene;
  s_libCache.scope = s_libScope;
  s_libCache.armed = s_libLoadArmed;
  s_libCache.selKind = st.selectedKind;
  s_libCache.selIdx = st.selectedIdx;
  s_libCache.selStripCol = curStripCol;
  std::snprintf(s_libCache.selStripName, sizeof(s_libCache.selStripName), "%s", curStripName);
}

void syncMetersOnly(const MixerState &st) {
  if (s_activeTab == TAB_OVERVIEW) {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      if (s_ovTileCache[i].meterPct != st.ch[i].meterPct) {
        s_ovTileCache[i].meterPct = st.ch[i].meterPct;
        lv_bar_set_value(s_ovTileVu[i], st.ch[i].meterPct, LV_ANIM_OFF);
      }
    }
    if (s_ovPanelCache.mainVu != st.mainLr.meterPct) {
      s_ovPanelCache.mainVu = st.mainLr.meterPct;
      lv_bar_set_value(s_ovMainVu, st.mainLr.meterPct, LV_ANIM_OFF);
    }
  } else if (s_activeTab == TAB_FADERS) {
    StripKind k;
    int start = 0;
    getFaderBankRange(s_faderBank, &k, &start);
    for (int i = 0; i < 8; ++i) {
      const StripState *s = mixerGetStripConst(st, k, start + i);
      if (s && s_fSlotCache[i].meterPct != s->meterPct) {
        s_fSlotCache[i].meterPct = s->meterPct;
        lv_bar_set_value(s_fVuBar[i], s->meterPct, LV_ANIM_OFF);
      }
    }
    const StripState *ms = (st.activeBusSend >= 0) ? &st.bus[st.activeBusSend] : &st.mainLr;
    if (s_fSlotCache[8].meterPct != ms->meterPct) {
      s_fSlotCache[8].meterPct = ms->meterPct;
      lv_bar_set_value(s_fVuBar[8], ms->meterPct, LV_ANIM_OFF);
    }
  }
}

}  // namespace

void uiLvglInit() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar(scr);

  for (int i = 0; i < TAB_COUNT; ++i) {
    lv_obj_t *v = lv_obj_create(scr);
    s_tabViews[i] = v;
    lv_obj_set_pos(v, 0, 46);
    lv_obj_set_size(v, 800, 434);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(v, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(v, 0, 0);
    lv_obj_set_style_radius(v, 0, 0);
    lv_obj_set_style_pad_all(v, 0, 0);
    if (i != 0) lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
  }

  buildOverviewTab(s_tabViews[TAB_OVERVIEW]);
  buildConfigTab(s_tabViews[TAB_CONFIG]);
  buildScribbleTab(s_tabViews[TAB_SCRIBBLE]);
  buildMatrixTab(s_tabViews[TAB_MATRIX]);
  buildFadersTab(s_tabViews[TAB_FADERS]);
  buildLibraryTab(s_tabViews[TAB_LIBRARY]);
  buildNetworkTab(s_tabViews[TAB_NETWORK]);

  // Temporarily unhide tabs/modals so LVGL computes exact screen-space keyboard bounds
  lv_obj_clear_flag(s_tabViews[TAB_SCRIBBLE], LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_tabViews[TAB_LIBRARY], LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_tabViews[TAB_NETWORK], LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(scr);
  lv_area_t scA{}, netA{}, libA{};
  lv_obj_get_coords(s_scKeyboard, &scA);
  lv_obj_get_coords(s_netKb, &netA);
  lv_obj_get_coords(s_libKb, &libA);
  lv_obj_add_flag(s_netKbModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_tabViews[TAB_NETWORK], LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_libKbModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_tabViews[TAB_LIBRARY], LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_tabViews[TAB_SCRIBBLE], LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(scr);
  Serial.printf(
      "[UI] Keyboard layout verified: sc=(%d,%d..%d,%d) net=(%d,%d..%d,%d) lib=(%d,%d..%d,%d)\n",
      (int)scA.x1, (int)scA.y1, (int)scA.x2, (int)scA.y2, (int)netA.x1, (int)netA.y1,
      (int)netA.x2, (int)netA.y2, (int)libA.x1, (int)libA.y1, (int)libA.x2, (int)libA.y2);

  mixerMarkDirty(DIRTY_ALL);
}

void uiLvglSyncFromState() {
  if (s_cfgPhantomArmed && (millis() - s_cfgPhantomArmMs > 3500u)) {
    s_cfgPhantomArmed = false;
    mixerMarkUiOnlyDirty(DIRTY_CONFIG);
  }
  if (s_otaInstallArmed && static_cast<int32_t>(millis() - s_otaInstallArmUntilMs) >= 0) {
    s_otaInstallArmed = false;
    mixerMarkUiOnlyDirty(DIRTY_NETWORK);
  }
  const uint32_t dirty = mixerTakeUiDirtyFlags();
  if (dirty == DIRTY_NONE) return;

  if (!mixerStateLock(25)) {
    mixerMarkDirty(dirty);
    return;
  }
  const MixerState &st = mixerStateUnsafe();

  if (dirty & DIRTY_HEADER) {
    syncHeader(st);
  }

  if (s_activeTab == TAB_OVERVIEW && (dirty & DIRTY_OVERVIEW)) {
    syncOverview(st);
  } else if (s_activeTab == TAB_CONFIG && (dirty & DIRTY_CONFIG)) {
    syncConfig(st);
  } else if (s_activeTab == TAB_SCRIBBLE && (dirty & DIRTY_SCRIBBLE)) {
    syncScribble(st);
  } else if (s_activeTab == TAB_MATRIX && (dirty & DIRTY_MATRIX)) {
    syncMatrix(st);
  } else if (s_activeTab == TAB_FADERS && (dirty & DIRTY_FADERS)) {
    syncFaders(st);
  } else if (s_activeTab == TAB_LIBRARY && (dirty & DIRTY_LIBRARY)) {
    syncLibrary(st);
  } else if (s_activeTab == TAB_NETWORK && (dirty & DIRTY_NETWORK)) {
    syncNetwork(st);
  }

  if (dirty & DIRTY_METERS) {
    syncMetersOnly(st);
  }

  mixerStateUnlock();
}
