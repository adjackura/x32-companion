#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "osc_codec.h"

static bool approx_eq(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) <= eps;
}

static void write_le_u32(uint8_t *dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFFu);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

static void write_be_u32(uint8_t *dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
  dst[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  dst[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  dst[3] = static_cast<uint8_t>(v & 0xFFu);
}

static void write_le_f32(uint8_t *dst, float f) {
  uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(bits));
  write_le_u32(dst, bits);
}

static void test_osc_build_and_parse_roundtrips() {
  uint8_t buf[256];
  OscPacketView view;

  // Query packet (no arguments, 4-byte padded address + empty type tag ',')
  size_t len = osc_build_query(buf, sizeof(buf), "/ch/01/config/name");
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/ch/01/config/name") == 0);
  assert(view.num_args == 0);

  // Integer argument
  len = osc_build_int(buf, sizeof(buf), "/-stat/selidx", 17);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/-stat/selidx") == 0);
  assert(view.num_args == 1);
  assert(view.args[0].type == OSC_ARG_INT);
  assert(view.args[0].i == 17);

  // Negative integer argument
  len = osc_build_int(buf, sizeof(buf), "/test/neg", -42);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(view.num_args == 1);
  assert(view.args[0].type == OSC_ARG_INT);
  assert(view.args[0].i == -42);

  // Float argument
  len = osc_build_float(buf, sizeof(buf), "/dca/3/fader", 0.75f);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/dca/3/fader") == 0);
  assert(view.num_args == 1);
  assert(view.args[0].type == OSC_ARG_FLOAT);
  assert(approx_eq(view.args[0].f, 0.75f, 1e-6f));

  // String argument (with exact 4-byte boundary test cases: len=3+1=4, len=4+1->8)
  len = osc_build_str(buf, sizeof(buf), "/ch/01/config/name", "Kick");
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/ch/01/config/name") == 0);
  assert(view.num_args == 1);
  assert(view.args[0].type == OSC_ARG_STR);
  assert(std::strcmp(view.args[0].s, "Kick") == 0);

  len = osc_build_str(buf, sizeof(buf), "/xinfo", "ABC");
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(view.num_args == 1);
  assert(std::strcmp(view.args[0].s, "ABC") == 0);

  // Meters request without and with time_factor
  len = osc_build_meters_req(buf, sizeof(buf), "/meters/1", -1);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/meters") == 0);
  assert(view.num_args >= 1);
  assert(view.args[0].type == OSC_ARG_STR);
  assert(std::strcmp(view.args[0].s, "/meters/1") == 0);

  len = osc_build_meters_req(buf, sizeof(buf), "/meters/1", 2);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/meters") == 0);
  assert(view.num_args >= 2);
  assert(view.args[0].type == OSC_ARG_STR && std::strcmp(view.args[0].s, "/meters/1") == 0);
  assert(view.args[view.num_args - 1].type == OSC_ARG_INT && view.args[view.num_args - 1].i == 2);

  // Capacity checks
  assert(osc_build_query(buf, 4, "/ch/01/config/name") == 0);
  assert(osc_build_str(buf, 8, "/ch/01/config/name", "LongStringValue") == 0);

  // Scene /load and /save builders
  len = osc_build_load_scene(buf, sizeof(buf), 7);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/load") == 0);
  assert(view.num_args == 2);
  assert(view.args[0].type == OSC_ARG_STR && std::strcmp(view.args[0].s, "scene") == 0);
  assert(view.args[1].type == OSC_ARG_INT && view.args[1].i == 7);

  len = osc_build_save_scene(buf, sizeof(buf), 12, "Worship", "Sunday AM");
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/save") == 0);
  assert(view.num_args == 4);
  assert(view.args[0].type == OSC_ARG_STR && std::strcmp(view.args[0].s, "scene") == 0);
  assert(view.args[1].type == OSC_ARG_INT && view.args[1].i == 12);
  assert(view.args[2].type == OSC_ARG_STR && std::strcmp(view.args[2].s, "Worship") == 0);
  assert(view.args[3].type == OSC_ARG_STR && std::strcmp(view.args[3].s, "Sunday AM") == 0);

  // Channel preset (libchan) /load and /save builders
  len = osc_build_load_libchan(buf, sizeof(buf), 3, 14, 0x3E);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/load") == 0);
  assert(view.num_args == 4);
  assert(view.args[0].type == OSC_ARG_STR && std::strcmp(view.args[0].s, "libchan") == 0);
  assert(view.args[1].type == OSC_ARG_INT && view.args[1].i == 3);
  assert(view.args[2].type == OSC_ARG_INT && view.args[2].i == 14);
  assert(view.args[3].type == OSC_ARG_INT && view.args[3].i == 0x3E);

  len = osc_build_save_libchan(buf, sizeof(buf), 5, "Lead Vox", 14);
  assert(len > 0 && (len % 4) == 0);
  assert(osc_parse_packet(buf, len, &view));
  assert(std::strcmp(view.addr, "/save") == 0);
  assert(view.num_args == 4);
  assert(view.args[0].type == OSC_ARG_STR && std::strcmp(view.args[0].s, "libchan") == 0);
  assert(view.args[1].type == OSC_ARG_INT && view.args[1].i == 5);
  assert(view.args[2].type == OSC_ARG_STR && std::strcmp(view.args[2].s, "Lead Vox") == 0);
  assert(view.args[3].type == OSC_ARG_INT && view.args[3].i == 14);

  // Short-buffer overflow checks on multi-arg builders
  assert(osc_build_load_scene(buf, 12, 0) == 0);
  assert(osc_build_save_scene(buf, 20, 0, "LongNameHere", "Notes") == 0);
  assert(osc_build_load_libchan(buf, 24, 0, 0, 63) == 0);
  assert(osc_build_save_libchan(buf, 20, 0, "LongPresetName", 0) == 0);
}

static void test_osc_parse_malformed_and_bounds() {
  OscPacketView view;
  uint8_t bad1[] = {'a', 'b', 'c', 0};  // Not starting with '/'
  assert(!osc_parse_packet(bad1, sizeof(bad1), &view));
  assert(!osc_parse_packet(nullptr, 16, &view));
  assert(!osc_parse_packet(bad1, 0, &view));

  // Address without null terminator
  uint8_t bad2[8] = {'/', 'x', 'i', 'n', 'f', 'o', 'a', 'b'};
  assert(!osc_parse_packet(bad2, sizeof(bad2), &view));

  // Truncated integer argument
  uint8_t valid[64];
  size_t len = osc_build_int(valid, sizeof(valid), "/ch/01/mix/on", 1);
  assert(len >= 8);
  assert(!osc_parse_packet(valid, len - 2, &view));

  // Unaligned packet length
  assert(!osc_parse_packet(valid, len - 1, &view));

  // Truncated blob length
  uint8_t blob_pkt[32] = {};
  std::memcpy(blob_pkt, "/meters/1\0\0\0,b\0\0", 16);
  write_be_u32(blob_pkt + 16, 100);  // claims 100 bytes, only 4 available
  assert(!osc_parse_packet(blob_pkt, 24, &view));
}

static void test_x32_fader_db_taper() {
  // Exact X32 piecewise knots:
  // 1.0 -> +10 dB, 0.75 -> 0 dB, 0.5 -> -10 dB, 0.25 -> -30 dB, 0.0625 -> -60 dB, 0.0 -> -90 dB (<= -90)
  assert(approx_eq(x32_fader_to_db(1.0f), 10.0f, 0.05f));
  assert(approx_eq(x32_fader_to_db(0.75f), 0.0f, 0.05f));
  assert(approx_eq(x32_fader_to_db(0.5f), -10.0f, 0.05f));
  assert(approx_eq(x32_fader_to_db(0.25f), -30.0f, 0.05f));
  assert(approx_eq(x32_fader_to_db(0.0625f), -60.0f, 0.05f));
  assert(x32_fader_to_db(0.0f) <= -89.9f);

  // Reverse taper round-trip across knots and midpoints
  const float test_vals[] = {0.0f, 0.03f, 0.0625f, 0.15f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f};
  for (float v : test_vals) {
    float db = x32_fader_to_db(v);
    float rt = x32_db_to_fader(db);
    assert(approx_eq(rt, v, 0.005f));
  }

  // dB formatting checks
  char fmt[32];
  x32_format_db(0.0f, fmt, sizeof(fmt));
  assert(std::strstr(fmt, "oo") != nullptr || std::strstr(fmt, "-90") != nullptr || fmt[0] == '-');
  x32_format_db(0.75f, fmt, sizeof(fmt));
  assert(std::strstr(fmt, "0.0") != nullptr);
  x32_format_db(1.0f, fmt, sizeof(fmt));
  assert(std::strstr(fmt, "10.0") != nullptr);

  // Musical dB stepping checks
  float f = 0.0f;
  bool hit_m60 = false, hit_m30 = false, hit_m15 = false, hit_zero = false;
  int up_steps = 0;
  while (f < 1.0f && up_steps < 200) {
    float next = x32_step_fader_db(f, 1);
    assert(next > f);
    f = next;
    ++up_steps;
    if (approx_eq(f, 0.0625f, 1e-4f)) hit_m60 = true;
    if (approx_eq(f, 0.25f, 1e-4f)) hit_m30 = true;
    if (approx_eq(f, 0.4375f, 1e-4f)) hit_m15 = true;
    if (approx_eq(f, 0.75f, 1e-4f)) hit_zero = true;
  }
  assert(up_steps == 86);
  assert(approx_eq(f, 1.0f, 1e-6f));
  assert(hit_m60 && hit_m30 && hit_m15 && hit_zero);
  assert(approx_eq(x32_step_fader_db(1.0f, 1), 1.0f, 1e-6f));

  int down_steps = 0;
  while (f > 0.0f && down_steps < 200) {
    float prev = x32_step_fader_db(f, -1);
    assert(prev < f);
    f = prev;
    ++down_steps;
  }
  assert(down_steps == 86);
  assert(approx_eq(f, 0.0f, 1e-6f));
  assert(approx_eq(x32_step_fader_db(0.0f, -1), 0.0f, 1e-6f));

  // Off-grid snap to 0.0 dB (0.75f)
  assert(approx_eq(x32_step_fader_db(x32_db_to_fader(-0.3f), 1), 0.75f, 1e-4f));
  assert(approx_eq(x32_step_fader_db(x32_db_to_fader(0.2f), -1), 0.75f, 1e-4f));
}

static void test_x32_meters1_blob() {
  // /meters/1 blob format:
  // Note: When parsed from an OSC packet, arg.blob points to the OSC blob payload:
  // first 4 bytes LE uint32 = float_count (e.g., 96), followed by float_count LE float32 values.
  uint8_t blob[4 + 96 * 4] = {};
  write_le_u32(blob, 96u);
  // Channel 0 (ch 1) to 0.0 linear -> 0%
  // Channel 1 (ch 2) to 0.5 linear (-6.02 dB on -60..0 dB scale) -> ~90%
  // Channel 2 (ch 3) to 1.0 linear -> 100% + clip
  // Channel 3 (ch 4) to 0.0005 linear (below 0.001 floor) -> 0%
  // Channel 4 (ch 5) to 0.01 linear (-40.0 dB on -60..0 dB scale) -> ~33%
  // Channel 5 (ch 6) to 0.25 linear (-12.04 dB) -> ~80%
  // Channel 31 (ch 32) to 0.995 linear -> clip
  write_le_f32(blob + 4 + 0 * 4, 0.0f);
  write_le_f32(blob + 4 + 1 * 4, 0.5f);
  write_le_f32(blob + 4 + 2 * 4, 1.0f);
  write_le_f32(blob + 4 + 3 * 4, 0.0005f);
  write_le_f32(blob + 4 + 4 * 4, 0.01f);
  write_le_f32(blob + 4 + 5 * 4, 0.25f);
  write_le_f32(blob + 4 + 31 * 4, 0.995f);

  uint8_t pct[32] = {};
  uint32_t clip_mask = 0;
  assert(x32_parse_meters1_blob(blob, sizeof(blob), pct, &clip_mask));
  assert(pct[0] == 0);
  assert(pct[1] >= 89 && pct[1] <= 91);
  assert(pct[2] == 100);
  assert(pct[3] == 0);
  assert(pct[4] >= 32 && pct[4] <= 34);
  assert(pct[5] >= 79 && pct[5] <= 81);
  assert(pct[31] >= 99 && pct[31] <= 100);
  assert((clip_mask & (1u << 2)) != 0);
  assert((clip_mask & (1u << 31)) != 0);
  assert((clip_mask & (1u << 1)) == 0);

  // Full OSC packet containing /meters/1 blob
  std::vector<uint8_t> pkt(20 + sizeof(blob), 0);
  std::memcpy(pkt.data(), "/meters/1\0\0\0,b\0\0", 16);
  write_be_u32(pkt.data() + 16, static_cast<uint32_t>(sizeof(blob)));
  std::memcpy(pkt.data() + 20, blob, sizeof(blob));

  OscPacketView view;
  assert(osc_parse_packet(pkt.data(), pkt.size(), &view));
  assert(std::strcmp(view.addr, "/meters/1") == 0);
  assert(view.num_args == 1 && view.args[0].type == OSC_ARG_BLOB);
  assert(view.args[0].blob_len == sizeof(blob));
  std::memset(pct, 0, sizeof(pct));
  clip_mask = 0;
  assert(x32_parse_meters1_blob(view.args[0].blob, view.args[0].blob_len, pct, &clip_mask));
  assert(pct[2] == 100);
  assert((clip_mask & (1u << 2)) != 0);

  // Short/invalid meter blobs rejected safely
  assert(!x32_parse_meters1_blob(blob, 16, pct, &clip_mask));
  assert(!x32_parse_meters1_blob(nullptr, sizeof(blob), pct, &clip_mask));
}

static void test_x32_colors_and_icons() {
  for (uint8_t id = 0; id < 16; ++id) {
    X32ColorInfo info = x32_get_color_info(id);
    assert(info.id == id);
    assert(info.code != nullptr && info.code[0] != '\0');
    if (id < 8) {
      assert(!info.inverted);
    } else {
      assert(info.inverted);
    }
  }
  // Normal vs Inverted contrast expectation for color 1 (Red) vs 9 (Red Inverted)
  X32ColorInfo red_norm = x32_get_color_info(1);
  X32ColorInfo red_inv = x32_get_color_info(9);
  assert(red_norm.bg_rgb != red_inv.bg_rgb);
  assert(red_norm.fg_rgb != red_inv.fg_rgb);

  // Out-of-range wraps/clamps cleanly without crashing
  X32ColorInfo fallback = x32_get_color_info(99);
  assert(fallback.code != nullptr);

  // Icon lookup covers all 1..74 official X32 icons cleanly
  for (uint8_t id = 1; id <= 74; ++id) {
    const char *nm = x32_get_icon_name(id);
    assert(nm != nullptr && std::strcmp(nm, "Icon") != 0 && std::strlen(nm) > 0);
  }
  assert(std::strcmp(x32_get_icon_name(1), "None") == 0);
  assert(std::strcmp(x32_get_icon_name(17), "Electric Bass") == 0);
  assert(std::strcmp(x32_get_icon_name(20), "Les Paul Gtr") == 0);
  assert(std::strcmp(x32_get_icon_name(28), "Organ") == 0);
  assert(std::strcmp(x32_get_icon_name(34), "Keytar") == 0);
  assert(std::strcmp(x32_get_icon_name(50), "Handheld Mic") == 0);
  assert(std::strcmp(x32_get_icon_name(74), "Smiley") == 0);
  assert(std::strcmp(x32_get_icon_name(0), "Icon") == 0);
  assert(std::strcmp(x32_get_icon_name(255), "Icon") == 0);
}

static void test_source_routing_and_preamp_math() {
  char buf[32];

  x32_format_source_short(0, buf, sizeof(buf));
  assert(std::strcmp(buf, "OFF") == 0);
  x32_format_source_full(0, buf, sizeof(buf));
  assert(std::strcmp(buf, "Off (Unpatched)") == 0);

  x32_format_source_short(1, buf, sizeof(buf));
  assert(std::strcmp(buf, "In01") == 0);
  x32_format_source_full(32, buf, sizeof(buf));
  assert(std::strcmp(buf, "In 32") == 0);

  x32_format_source_short(33, buf, sizeof(buf));
  assert(std::strcmp(buf, "Aux1") == 0);
  x32_format_source_full(38, buf, sizeof(buf));
  assert(std::strcmp(buf, "Aux 6") == 0);

  x32_format_source_short(39, buf, sizeof(buf));
  assert(std::strcmp(buf, "UsbL") == 0);
  x32_format_source_full(40, buf, sizeof(buf));
  assert(std::strcmp(buf, "USB R") == 0);

  x32_format_source_short(41, buf, sizeof(buf));
  assert(std::strcmp(buf, "Fx1L") == 0);
  x32_format_source_full(48, buf, sizeof(buf));
  assert(std::strcmp(buf, "Fx 4R") == 0);

  x32_format_source_short(49, buf, sizeof(buf));
  assert(std::strcmp(buf, "Bs01") == 0);
  x32_format_source_full(64, buf, sizeof(buf));
  assert(std::strcmp(buf, "Bus 16") == 0);

  x32_format_source_short(99, buf, sizeof(buf));
  assert(std::strcmp(buf, "OFF") == 0);

  assert(x32_default_source_for_strip(0, 0) == 1);
  assert(x32_default_source_for_strip(0, 31) == 32);
  assert(x32_default_source_for_strip(1, 0) == 33);
  assert(x32_default_source_for_strip(1, 7) == 40);
  assert(x32_default_source_for_strip(2, 0) == 0);

  // Default 1:1 Local XLR + Aux TRS
  const uint8_t rt_local[5] = {0, 1, 2, 3, 0};
  assert(x32_source_to_headamp_idx(1, rt_local) == 0);
  assert(x32_source_to_headamp_idx(20, rt_local) == 19);
  assert(x32_source_to_headamp_idx(32, rt_local) == 31);
  assert(x32_source_to_headamp_idx(33, rt_local) == -1);
  assert(x32_source_to_headamp_idx(39, rt_local) == -1);
  assert(x32_source_to_headamp_idx(0, rt_local) == -1);

  // Mixed AES50-A (block 1 = A9-16 -> rt=5 -> ha 40..47), AES50-B (block 2 = B17-24 -> rt=12 -> ha 96..103),
  // CARD (block 3 = CARD1-8 -> rt=16 -> -1), AUX remap (AN1-4 -> rt=2 -> Aux1..4 map to HA 0..3, Aux5..6 -> -1)
  const uint8_t rt_mixed[5] = {0, 5, 12, 16, 2};
  assert(x32_source_to_headamp_idx(9, rt_mixed) == 40);
  assert(x32_source_to_headamp_idx(17, rt_mixed) == 96);
  assert(x32_source_to_headamp_idx(25, rt_mixed) == -1);
  assert(x32_source_to_headamp_idx(33, rt_mixed) == 0);
  assert(x32_source_to_headamp_idx(36, rt_mixed) == 3);
  assert(x32_source_to_headamp_idx(37, rt_mixed) == -1);

  x32_format_headamp_socket(19, false, buf, sizeof(buf));
  assert(std::strcmp(buf, "Local XLR 20") == 0);
  x32_format_headamp_socket(40, true, buf, sizeof(buf));
  assert(std::strcmp(buf, "A:09") == 0);
  x32_format_headamp_socket(96, false, buf, sizeof(buf));
  assert(std::strcmp(buf, "AES50-B 17") == 0);

  assert(!x32_headamp_is_gain_split(5, 0x00));
  assert(x32_headamp_is_gain_split(5, 0x02));
  assert(!x32_headamp_is_gain_split(40, 0x02));
  assert(x32_headamp_is_gain_split(40, 0x04));
  assert(x32_headamp_is_gain_split(96, 0x08));
  assert(x32_headamp_is_gain_split(-1, 0x00));

  // Gain -12..+60 dB
  assert(std::fabs(x32_gain_to_db(0.0f) - (-12.0f)) < 0.01f);
  assert(std::fabs(x32_gain_to_db(1.0f) - 60.0f) < 0.01f);
  assert(std::fabs(x32_gain_to_db(x32_db_to_gain(18.5f)) - 18.5f) < 0.01f);
  x32_format_gain_db(x32_db_to_gain(0.0f), buf, sizeof(buf));
  assert(std::strcmp(buf, "0.0 dB") == 0);
  x32_format_gain_db(x32_db_to_gain(24.5f), buf, sizeof(buf));
  assert(std::strcmp(buf, "+24.5 dB") == 0);

  // Trim -18..+18 dB
  assert(std::fabs(x32_trim_to_db(0.5f)) < 0.01f);
  assert(std::fabs(x32_trim_to_db(x32_db_to_trim(-6.25f)) - (-6.25f)) < 0.01f);
  x32_format_trim_db(x32_db_to_trim(3.25f), buf, sizeof(buf));
  assert(std::strcmp(buf, "+3.25 dB") == 0);

  // HPF 20..400 Hz log scale
  assert(std::fabs(x32_hpf_to_hz(0.0f) - 20.0f) < 0.1f);
  assert(std::fabs(x32_hpf_to_hz(1.0f) - 400.0f) < 0.5f);
  assert(std::fabs(x32_hpf_to_hz(x32_hz_to_hpf(80.0f)) - 80.0f) < 0.5f);
  x32_format_hpf_hz(x32_hz_to_hpf(100.0f), buf, sizeof(buf));
  assert(std::strcmp(buf, "100 Hz") == 0);

  // Pan L100..C..R100
  assert(x32_pan_to_pct(0.5f) == 0);
  assert(x32_pan_to_pct(0.0f) == -100);
  assert(x32_pan_to_pct(1.0f) == 100);
  x32_format_pan(0.5f, buf, sizeof(buf));
  assert(std::strcmp(buf, "C") == 0);
  x32_format_pan(x32_pct_to_pan(-35), buf, sizeof(buf));
  assert(std::strcmp(buf, "L35") == 0);
  x32_format_pan(x32_pct_to_pan(60), buf, sizeof(buf));
  assert(std::strcmp(buf, "R60") == 0);
}

void test_ota_semver_and_manifest() {
  assert(x32_ota_compare_semver("1.2.0", "1.1.0") == 1);
  assert(x32_ota_compare_semver("v1.1.0", "1.1.0") == 0);
  assert(x32_ota_compare_semver("1.10.0", "1.2.0") == 1);
  assert(x32_ota_compare_semver("1.0.9", "1.1.0") == -1);
  assert(x32_ota_compare_semver("v2", "1.99.99") == 1);
  assert(x32_ota_compare_semver("", "1.1.0") == -2);
  assert(x32_ota_compare_semver("abc", "1.1.0") == -2);
  assert(x32_ota_compare_semver("1.2.", "1.1.0") == -2);

  static constexpr const char *kRepo = "adjackura/x32-companion";
  static constexpr const char *kUrl =
      "https://github.com/adjackura/x32-companion/releases/download/v1.2.0/x32_companion.ino.bin";
  static constexpr const char *kSha =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  static constexpr uint32_t kMaxBytes = 0x3EA000u;

  assert(x32_ota_validate_manifest("1.2.0", kUrl, 1400000u, kSha, kRepo, kMaxBytes));
  assert(x32_ota_validate_manifest("v1.2.0", kUrl, 1400000u, kSha, kRepo, kMaxBytes));
  // HTTP downgrade URL rejected
  assert(!x32_ota_validate_manifest(
      "1.2.0",
      "http://github.com/adjackura/x32-companion/releases/download/v1.2.0/x32_companion.ino.bin",
      1400000u, kSha, kRepo, kMaxBytes));
  // Wrong repository rejected
  assert(!x32_ota_validate_manifest(
      "1.2.0",
      "https://github.com/evil/x32-companion/releases/download/v1.2.0/x32_companion.ino.bin",
      1400000u, kSha, kRepo, kMaxBytes));
  // Wrong SHA-256 length or non-hex character rejected
  assert(!x32_ota_validate_manifest("1.2.0", kUrl, 1400000u, "012345", kRepo, kMaxBytes));
  assert(!x32_ota_validate_manifest(
      "1.2.0", kUrl, 1400000u,
      "g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", kRepo, kMaxBytes));
  // Too small or exceeds rainmaker_8MB OTA slot
  assert(!x32_ota_validate_manifest("1.2.0", kUrl, 1024u, kSha, kRepo, kMaxBytes));
  assert(!x32_ota_validate_manifest("1.2.0", kUrl, kMaxBytes + 1u, kSha, kRepo, kMaxBytes));
}

void test_xinfo_reply_validation() {
  uint8_t qbuf[64];
  const size_t qlen = osc_build_query(qbuf, sizeof(qbuf), "/xinfo");
  assert(qlen > 0);
  OscPacketView qview;
  assert(osc_parse_packet(qbuf, qlen, &qview));
  assert(!x32_is_valid_xinfo_reply(qview));

  auto makeXinfo = [](const char *ip, const char *name, const char *model, const char *fw,
                      uint8_t argc = 4) {
    OscPacketView p;
    p.addr = "/xinfo";
    p.num_args = argc;
    const char *vals[4] = {ip, name, model, fw};
    for (int i = 0; i < 4; ++i) {
      p.args[i].type = OSC_ARG_STR;
      p.args[i].s = vals[i];
    }
    return p;
  };

  assert(x32_is_valid_xinfo_reply(makeXinfo("192.168.1.84", "X32-02-61-CA", "X32", "4.09")));
  assert(x32_is_valid_xinfo_reply(makeXinfo("10.0.0.50", "MON-RACK", "X32RACK", "4.13")));
  assert(x32_is_valid_xinfo_reply(makeXinfo("192.168.1.90", "FOH-M32", "M32R", "4.09")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("192.168.1.55", "iPad", "XR18", "1.0")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("192.168.1.56", "Console", "WING", "2.0")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("192.168.1.56", "Blank", "", "4.09")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("0.0.0.0", "X32", "X32", "4.09")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("255.255.255.255", "X32", "X32", "4.09")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("not-an-ip", "X32", "X32", "4.09")));
  assert(!x32_is_valid_xinfo_reply(makeXinfo("192.168.1.84", "X32", "X32", "4.09", 2)));
}

void test_http_date_epoch_parser() {
  assert(x32_parse_http_date_epoch("Thu, 01 Jan 1970 00:00:00 GMT") == 0LL);
  assert(x32_parse_http_date_epoch("Sun, 15 Nov 2026 23:55:11 GMT") == 1794786911LL);
  assert(x32_parse_http_date_epoch("Tue, 29 Feb 2028 12:00:00 GMT") == 1835438400LL);
  assert(x32_parse_http_date_epoch(nullptr) == -1LL);
  assert(x32_parse_http_date_epoch("") == -1LL);
  assert(x32_parse_http_date_epoch("Sun, 31 Feb 2026 00:00:00 GMT") == -1LL);
  assert(x32_parse_http_date_epoch("not a date") == -1LL);
  assert(x32_parse_http_date_epoch("Sun, 15 Nov 2026 25:00:00 GMT") == -1LL);
}

int main() {
  test_osc_build_and_parse_roundtrips();
  test_osc_parse_malformed_and_bounds();
  test_x32_fader_db_taper();
  test_x32_meters1_blob();
  test_x32_colors_and_icons();
  test_source_routing_and_preamp_math();
  test_ota_semver_and_manifest();
  test_xinfo_reply_validation();
  test_http_date_epoch_parser();
  std::printf("ALL osc_codec C++ unit tests PASSED!\n");
  return 0;
}
