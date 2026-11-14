#pragma once

#include <cstddef>
#include <cstdint>

static constexpr size_t OSC_MAX_ARGS = 8;

enum OscArgType : uint8_t {
  OSC_ARG_NONE = 0,
  OSC_ARG_INT = 'i',
  OSC_ARG_FLOAT = 'f',
  OSC_ARG_STR = 's',
  OSC_ARG_BLOB = 'b',
  OSC_ARG_TRUE = 'T',
  OSC_ARG_FALSE = 'F'
};

struct OscArg {
  OscArgType type = OSC_ARG_NONE;
  int32_t i = 0;
  float f = 0.0f;
  const char *s = nullptr;
  const uint8_t *blob = nullptr;
  uint32_t blob_len = 0;
};

struct OscPacketView {
  const char *addr = nullptr;
  const char *types = nullptr;
  uint8_t num_args = 0;
  OscArg args[OSC_MAX_ARGS];
};

struct X32ColorInfo {
  uint8_t id;
  const char *code;
  uint32_t bg_rgb;
  uint32_t fg_rgb;
  uint32_t accent_rgb;
  bool inverted;
};

// Pure C++ OSC packet builders (returns total packet bytes, or 0 on overflow/error)
size_t osc_build_query(uint8_t *out, size_t cap, const char *addr);
size_t osc_build_int(uint8_t *out, size_t cap, const char *addr, int32_t val);
size_t osc_build_float(uint8_t *out, size_t cap, const char *addr, float val);
size_t osc_build_str(uint8_t *out, size_t cap, const char *addr, const char *str);
size_t osc_build_meters_req(uint8_t *out, size_t cap, const char *meter_path, int32_t time_factor = -1);
size_t osc_build_load_scene(uint8_t *out, size_t cap, int32_t scene_idx);
size_t osc_build_save_scene(uint8_t *out, size_t cap, int32_t scene_idx, const char *name, const char *notes);
size_t osc_build_load_libchan(uint8_t *out, size_t cap, int32_t slot_idx, int32_t ch_idx, int32_t scope_mask);
size_t osc_build_save_libchan(uint8_t *out, size_t cap, int32_t slot_idx, const char *name, int32_t ch_idx);

// Pure C++ OSC packet parser (zero heap allocations, bounds-checked)
bool osc_parse_packet(const uint8_t *buf, size_t len, OscPacketView *out);

// Parse X32 /meters/1 binary blob payload (blob_data starts AFTER the OSC 4-byte BE blob length)
// Inside blob_data: 4-byte LE float count, followed by LE float32 values [0..1].
bool x32_parse_meters1_blob(const uint8_t *blob_data,
                            size_t blob_len,
                            uint8_t out_ch_pct[32],
                            uint32_t *out_clip_mask_32);

// Exact Behringer X32 piecewise linear fader taper
float x32_fader_to_db(float f);
float x32_db_to_fader(float db);
float x32_step_fader_db(float cur_f, int dir);
void x32_format_db(float f, char *buf, size_t buf_len);

// X32 scribble colors (0..15) and icons (1..74)
X32ColorInfo x32_get_color_info(uint8_t color_id);
const char *x32_get_icon_name(uint8_t icon_id);

// X32 channel/aux config/source (0..64) & headamp routing helpers
void x32_format_source_short(uint8_t src, char *buf, size_t buf_len);
void x32_format_source_full(uint8_t src, char *buf, size_t buf_len);
uint8_t x32_default_source_for_strip(int strip_kind_int, int idx);
int x32_source_to_headamp_idx(uint8_t src, const uint8_t in_routing[5]);
bool x32_headamp_is_gain_split(int ha_idx, uint8_t ha_flags);
void x32_format_headamp_socket(int ha_idx, bool short_fmt, char *buf, size_t buf_len);

// X32 headamp gain (-12..+60 dB), trim (-18..+18 dB), HPF (20..400 Hz log), Pan (L100..R100)
float x32_gain_to_db(float f);
float x32_db_to_gain(float db);
void x32_format_gain_db(float f, char *buf, size_t buf_len);

float x32_trim_to_db(float f);
float x32_db_to_trim(float db);
void x32_format_trim_db(float f, char *buf, size_t buf_len);

float x32_hpf_to_hz(float f);
float x32_hz_to_hpf(float hz);
void x32_format_hpf_hz(float f, char *buf, size_t buf_len);

int x32_pan_to_pct(float f);
float x32_pct_to_pan(int pct);
void x32_format_pan(float f, char *buf, size_t buf_len);

// OTA semver comparison (1: candidate > current, 0: equal, -1: candidate < current, -2: invalid)
int x32_ota_compare_semver(const char *candidate, const char *current);
bool x32_ota_validate_manifest(const char *version,
                               const char *url,
                               uint32_t size,
                               const char *sha256Hex,
                               const char *expectedRepo,
                               uint32_t maxBytes);

// Validates that a parsed OSC packet is a 4-string X32/M32 /xinfo response (not a 0-arg query).
bool x32_is_valid_xinfo_reply(const OscPacketView &pkt);

// Parses an RFC 7231 IMF-fixdate string (e.g. "Sun, 15 Nov 2026 23:55:11 GMT")
// into UTC Unix epoch seconds, or returns -1 on invalid input.
int64_t x32_parse_http_date_epoch(const char *httpDate);
