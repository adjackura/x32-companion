#include "osc_codec.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

inline size_t align4(size_t n) {
  return (n + 3u) & ~size_t(3u);
}

inline uint32_t read_be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) |
         (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

inline uint32_t read_le32(const uint8_t *p) {
  return uint32_t(p[0]) |
         (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

inline void write_be32(uint8_t *p, uint32_t v) {
  p[0] = uint8_t((v >> 24) & 0xFFu);
  p[1] = uint8_t((v >> 16) & 0xFFu);
  p[2] = uint8_t((v >> 8) & 0xFFu);
  p[3] = uint8_t(v & 0xFFu);
}

inline float u32_to_f32(uint32_t u) {
  float f = 0.0f;
  std::memcpy(&f, &u, sizeof(f));
  return f;
}

inline uint32_t f32_to_u32(float f) {
  uint32_t u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

bool append_padded_str(uint8_t *out, size_t cap, size_t *offset, const char *str) {
  if (!str) return false;
  const size_t slen = std::strlen(str);
  const size_t padded = align4(slen + 1u);
  if (*offset + padded > cap) return false;
  std::memcpy(out + *offset, str, slen);
  std::memset(out + *offset + slen, 0, padded - slen);
  *offset += padded;
  return true;
}

bool append_be32(uint8_t *out, size_t cap, size_t *offset, int32_t val) {
  if (*offset + 4u > cap) return false;
  write_be32(out + *offset, static_cast<uint32_t>(val));
  *offset += 4u;
  return true;
}

bool start_osc_msg(uint8_t *out, size_t cap, size_t *offset, const char *addr, const char *tags) {
  if (!out || !addr || addr[0] != '/' || !tags) return false;
  *offset = 0;
  return append_padded_str(out, cap, offset, addr) && append_padded_str(out, cap, offset, tags);
}

}  // namespace

size_t osc_build_query(uint8_t *out, size_t cap, const char *addr) {
  if (!out || !addr || addr[0] != '/') return 0;
  size_t off = 0;
  if (!append_padded_str(out, cap, &off, addr)) return 0;
  return off;
}

size_t osc_build_int(uint8_t *out, size_t cap, const char *addr, int32_t val) {
  if (!out || !addr || addr[0] != '/') return 0;
  size_t off = 0;
  if (!append_padded_str(out, cap, &off, addr)) return 0;
  if (!append_padded_str(out, cap, &off, ",i")) return 0;
  if (off + 4u > cap) return 0;
  write_be32(out + off, static_cast<uint32_t>(val));
  off += 4u;
  return off;
}

size_t osc_build_float(uint8_t *out, size_t cap, const char *addr, float val) {
  if (!out || !addr || addr[0] != '/') return 0;
  size_t off = 0;
  if (!append_padded_str(out, cap, &off, addr)) return 0;
  if (!append_padded_str(out, cap, &off, ",f")) return 0;
  if (off + 4u > cap) return 0;
  write_be32(out + off, f32_to_u32(val));
  off += 4u;
  return off;
}

size_t osc_build_str(uint8_t *out, size_t cap, const char *addr, const char *str) {
  if (!out || !addr || addr[0] != '/' || !str) return 0;
  size_t off = 0;
  if (!append_padded_str(out, cap, &off, addr)) return 0;
  if (!append_padded_str(out, cap, &off, ",s")) return 0;
  if (!append_padded_str(out, cap, &off, str)) return 0;
  return off;
}

size_t osc_build_meters_req(uint8_t *out, size_t cap, const char *meter_path, int32_t time_factor) {
  if (!out || !meter_path || meter_path[0] != '/') return 0;
  size_t off = 0;
  if (!append_padded_str(out, cap, &off, "/meters")) return 0;
  if (time_factor >= 0) {
    if (!append_padded_str(out, cap, &off, ",si")) return 0;
    if (!append_padded_str(out, cap, &off, meter_path)) return 0;
    if (off + 4u > cap) return 0;
    write_be32(out + off, static_cast<uint32_t>(time_factor));
    off += 4u;
  } else {
    if (!append_padded_str(out, cap, &off, ",s")) return 0;
    if (!append_padded_str(out, cap, &off, meter_path)) return 0;
  }
  return off;
}

size_t osc_build_load_scene(uint8_t *out, size_t cap, int32_t scene_idx) {
  size_t off = 0;
  if (!start_osc_msg(out, cap, &off, "/load", ",si") ||
      !append_padded_str(out, cap, &off, "scene") ||
      !append_be32(out, cap, &off, scene_idx)) {
    return 0;
  }
  return off;
}

size_t osc_build_save_scene(uint8_t *out,
                            size_t cap,
                            int32_t scene_idx,
                            const char *name,
                            const char *notes) {
  size_t off = 0;
  if (!start_osc_msg(out, cap, &off, "/save", ",siss") ||
      !append_padded_str(out, cap, &off, "scene") ||
      !append_be32(out, cap, &off, scene_idx) ||
      !append_padded_str(out, cap, &off, name ? name : "") ||
      !append_padded_str(out, cap, &off, notes ? notes : "")) {
    return 0;
  }
  return off;
}

size_t osc_build_load_libchan(uint8_t *out,
                              size_t cap,
                              int32_t slot_idx,
                              int32_t ch_idx,
                              int32_t scope_mask) {
  size_t off = 0;
  if (!start_osc_msg(out, cap, &off, "/load", ",siii") ||
      !append_padded_str(out, cap, &off, "libchan") ||
      !append_be32(out, cap, &off, slot_idx) ||
      !append_be32(out, cap, &off, ch_idx) ||
      !append_be32(out, cap, &off, scope_mask)) {
    return 0;
  }
  return off;
}

size_t osc_build_save_libchan(uint8_t *out,
                              size_t cap,
                              int32_t slot_idx,
                              const char *name,
                              int32_t ch_idx) {
  size_t off = 0;
  if (!start_osc_msg(out, cap, &off, "/save", ",sisi") ||
      !append_padded_str(out, cap, &off, "libchan") ||
      !append_be32(out, cap, &off, slot_idx) ||
      !append_padded_str(out, cap, &off, name ? name : "") ||
      !append_be32(out, cap, &off, ch_idx)) {
    return 0;
  }
  return off;
}

bool osc_parse_packet(const uint8_t *buf, size_t len, OscPacketView *out) {
  if (!buf || !out || len < 4u) return false;
  *out = OscPacketView{};

  if (buf[0] != '/') return false;
  const void *nul = std::memchr(buf, 0, len);
  if (!nul) return false;
  const size_t addr_len = static_cast<const uint8_t *>(nul) - buf;
  size_t off = align4(addr_len + 1u);
  if (off > len) return false;

  out->addr = reinterpret_cast<const char *>(buf);

  if (off == len) {
    out->types = "";
    out->num_args = 0;
    return true;
  }

  if (buf[off] != ',') return false;
  const void *tag_nul = std::memchr(buf + off, 0, len - off);
  if (!tag_nul) return false;
  const size_t tag_len = static_cast<const uint8_t *>(tag_nul) - (buf + off);
  const char *tags = reinterpret_cast<const char *>(buf + off + 1u);
  off += align4(tag_len + 1u);
  if (off > len) return false;

  out->types = tags;

  for (size_t i = 0; tags[i] != '\0'; ++i) {
    if (out->num_args >= OSC_MAX_ARGS) break;
    OscArg &arg = out->args[out->num_args];
    const char t = tags[i];

    switch (t) {
      case 'i': {
        if (off + 4u > len) return false;
        arg.type = OSC_ARG_INT;
        arg.i = static_cast<int32_t>(read_be32(buf + off));
        off += 4u;
        out->num_args++;
        break;
      }
      case 'f': {
        if (off + 4u > len) return false;
        arg.type = OSC_ARG_FLOAT;
        arg.f = u32_to_f32(read_be32(buf + off));
        off += 4u;
        out->num_args++;
        break;
      }
      case 's': {
        const void *snul = std::memchr(buf + off, 0, len - off);
        if (!snul) return false;
        const size_t slen = static_cast<const uint8_t *>(snul) - (buf + off);
        const size_t padded = align4(slen + 1u);
        if (off + padded > len) return false;
        arg.type = OSC_ARG_STR;
        arg.s = reinterpret_cast<const char *>(buf + off);
        off += padded;
        out->num_args++;
        break;
      }
      case 'b': {
        if (off + 4u > len) return false;
        const uint32_t blen = read_be32(buf + off);
        off += 4u;
        const size_t padded = align4(blen);
        if (padded < blen || off + padded > len) return false;
        arg.type = OSC_ARG_BLOB;
        arg.blob = buf + off;
        arg.blob_len = blen;
        off += padded;
        out->num_args++;
        break;
      }
      case 'T': {
        arg.type = OSC_ARG_TRUE;
        arg.i = 1;
        out->num_args++;
        break;
      }
      case 'F': {
        arg.type = OSC_ARG_FALSE;
        arg.i = 0;
        out->num_args++;
        break;
      }
      case 'N': {
        // Nil consumes 0 payload bytes
        break;
      }
      default:
        return false;
    }
  }

  return true;
}

bool x32_parse_meters1_blob(const uint8_t *blob_data,
                            size_t blob_len,
                            uint8_t out_ch_pct[32],
                            uint32_t *out_clip_mask_32) {
  if (!blob_data || !out_ch_pct || blob_len < 8u) return false;

  const uint32_t float_count = read_le32(blob_data);
  if (float_count == 0u || float_count > 512u) return false;
  if (4u + size_t(float_count) * 4u > blob_len) return false;

  uint32_t clip_mask = 0;
  const size_t ch_count = float_count < 32u ? size_t(float_count) : 32u;

  for (size_t i = 0; i < 32u; ++i) {
    if (i >= ch_count) {
      out_ch_pct[i] = 0;
      continue;
    }
    const float v = u32_to_f32(read_le32(blob_data + 4u + i * 4u));
    if (!std::isfinite(v) || v <= 0.001f) {
      out_ch_pct[i] = 0;
      continue;
    }
    if (v >= 0.98f) {
      clip_mask |= (1u << i);
    }
    const float db = 20.0f * std::log10(v);
    float pct = (db + 60.0f) * (100.0f / 60.0f);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    out_ch_pct[i] = static_cast<uint8_t>(pct + 0.5f);
  }

  if (out_clip_mask_32) {
    *out_clip_mask_32 = clip_mask;
  }
  return true;
}

float x32_fader_to_db(float f) {
  if (!std::isfinite(f) || f <= 0.0001f) return -90.0f;
  if (f > 1.0f) f = 1.0f;
  if (f >= 0.5f) return f * 40.0f - 30.0f;
  if (f >= 0.25f) return f * 80.0f - 50.0f;
  if (f >= 0.0625f) return f * 160.0f - 70.0f;
  return f * 480.0f - 90.0f;
}

float x32_db_to_fader(float db) {
  if (!std::isfinite(db) || db <= -89.99f) return 0.0f;
  if (db >= 10.0f) return 1.0f;
  if (db >= -10.0f) return (db + 30.0f) / 40.0f;
  if (db >= -30.0f) return (db + 50.0f) / 80.0f;
  if (db >= -60.0f) return (db + 70.0f) / 160.0f;
  return (db + 90.0f) / 480.0f;
}

float x32_step_fader_db(float cur_f, int dir) {
  if (!std::isfinite(cur_f) || cur_f < 0.0f) cur_f = 0.0f;
  if (cur_f > 1.0f) cur_f = 1.0f;
  if (dir == 0) return cur_f;

  // Handle -oo dB floor cleanly
  if (cur_f <= 0.0001f) {
    return (dir > 0) ? x32_db_to_fader(-85.0f) : 0.0f;
  }

  const float cur_db = x32_fader_to_db(cur_f);
  if (dir < 0 && cur_db <= -84.95f) {
    return 0.0f;
  }
  if (dir > 0 && cur_db >= 9.95f) {
    return 1.0f;
  }

  // Use 0.05 dB tolerance (half of x32_format_db's 0.1 dB resolution) so every tap
  // visibly advances the displayed dB value and snaps off-grid slider drags onto clean marks.
  static constexpr float kEpsDb = 0.05f;
  const float probe_db = cur_db + (dir > 0 ? kEpsDb : -kEpsDb);
  float step = 0.5f;
  if (probe_db >= -15.0f) {
    step = 0.5f;
  } else if (probe_db >= -30.0f) {
    step = 1.0f;
  } else if (probe_db >= -60.0f) {
    step = 2.0f;
  } else {
    step = 5.0f;
  }

  float next_db = 0.0f;
  if (dir > 0) {
    next_db = (std::floor((cur_db + kEpsDb) / step) + 1.0f) * step;
  } else {
    next_db = (std::ceil((cur_db - kEpsDb) / step) - 1.0f) * step;
  }

  if (next_db <= -89.9f) return 0.0f;
  if (next_db >= 10.0f) return 1.0f;
  return x32_db_to_fader(next_db);
}

void x32_format_db(float f, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  if (!std::isfinite(f) || f <= 0.0001f) {
    std::snprintf(buf, buf_len, "-oo dB");
    return;
  }
  const float db = x32_fader_to_db(f);
  if (db <= -89.9f) {
    std::snprintf(buf, buf_len, "-oo dB");
  } else if (db >= -0.05f && db <= 0.05f) {
    std::snprintf(buf, buf_len, "0.0 dB");
  } else if (db > 0.05f) {
    std::snprintf(buf, buf_len, "+%.1f dB", static_cast<double>(db));
  } else {
    std::snprintf(buf, buf_len, "%.1f dB", static_cast<double>(db));
  }
}

X32ColorInfo x32_get_color_info(uint8_t color_id) {
  static constexpr X32ColorInfo kBaseColors[8] = {
      {0, "OFF", 0x2A2F3A, 0xA8B3C4, 0x4E5869, false},
      {1, "RD", 0xD9383A, 0xFFFFFF, 0xFF6B6E, false},
      {2, "GN", 0x2EA043, 0xFFFFFF, 0x56D364, false},
      {3, "YE", 0xD4A72C, 0x11151C, 0xF2CC60, false},
      {4, "BL", 0x2F81F7, 0xFFFFFF, 0x79C0FF, false},
      {5, "MG", 0xBF4B8A, 0xFFFFFF, 0xF778BA, false},
      {6, "CY", 0x26A69A, 0x101820, 0x56D4C8, false},
      {7, "WH", 0xE6EDF3, 0x11151C, 0xFFFFFF, false},
  };

  static constexpr uint32_t kInvTextColors[8] = {
      0x9AA4B2, 0xFF6B6E, 0x56D364, 0xF2CC60,
      0x79C0FF, 0xF778BA, 0x56D4C8, 0xE6EDF3,
  };

  static constexpr const char *kInvCodes[8] = {
      "OFFi", "RDi", "GNi", "YEi", "BLi", "MGi", "CYi", "WHi",
  };

  const uint8_t cid = color_id & 0x0Fu;
  if (cid < 8u) {
    return kBaseColors[cid];
  }
  const uint8_t base = cid - 8u;
  return X32ColorInfo{
      cid,
      kInvCodes[base],
      0x141820,
      kInvTextColors[base],
      kBaseColors[base].bg_rgb,
      true,
  };
}

const char *x32_get_icon_name(uint8_t icon_id) {
  static const char *const kNames[75] = {
      "Icon",          // 0 (invalid)
      "None",          // 1
      "Kick Back",     // 2
      "Kick Front",    // 3
      "Snare Top",     // 4
      "Snare Bottom",  // 5
      "High Tom",      // 6
      "Mid Tom",       // 7
      "Floor Tom",     // 8
      "Hi-Hat",        // 9
      "Ride",          // 10
      "Drum Kit",      // 11
      "Cowbell",       // 12
      "Bongos",        // 13
      "Congas",        // 14
      "Tambourine",    // 15
      "Vibraphone",    // 16
      "Electric Bass", // 17
      "Acoustic Bass", // 18
      "Contrabass",    // 19
      "Les Paul Gtr",  // 20
      "Ibanez Gtr",    // 21
      "Washburn Gtr",  // 22
      "Acoustic Gtr",  // 23
      "Bass Amp",      // 24
      "Guitar Amp",    // 25
      "Amp Cabinet",   // 26
      "Piano",         // 27
      "Organ",         // 28
      "Harpsichord",   // 29
      "Keyboard",      // 30
      "Synth 1",       // 31
      "Synth 2",       // 32
      "Synth 3",       // 33
      "Keytar",        // 34
      "Trumpet",       // 35
      "Trombone",      // 36
      "Saxophone",     // 37
      "Clarinet",      // 38
      "Violin",        // 39
      "Cello",         // 40
      "Male Vocal",    // 41
      "Female Vocal",  // 42
      "Choir",         // 43
      "Hand Sign",     // 44
      "Talk A",        // 45
      "Talk B",        // 46
      "Large Mic",     // 47
      "Condenser L",   // 48
      "Condenser R",   // 49
      "Handheld Mic",  // 50
      "Wireless Mic",  // 51
      "Podium Mic",    // 52
      "Headset Mic",   // 53
      "XLR Jack",      // 54
      "TRS Plug",      // 55
      "TRS Left",      // 56
      "TRS Right",     // 57
      "RCA Left",      // 58
      "RCA Right",     // 59
      "Reel to Reel",  // 60
      "FX",            // 61
      "Computer",      // 62
      "Monitor Wedge", // 63
      "Left Speaker",  // 64
      "Right Speaker", // 65
      "Speaker Array", // 66
      "Pole Speaker",  // 67
      "Amp Rack",      // 68
      "Controls",      // 69
      "Fader",         // 70
      "MixBus",        // 71
      "Matrix",        // 72
      "Routing",       // 73
      "Smiley"         // 74
  };
  if (icon_id >= 1 && icon_id <= 74) {
    return kNames[icon_id];
  }
  return kNames[0];
}

namespace {

inline float clamp01(float f) {
  if (!std::isfinite(f)) return 0.0f;
  if (f <= 0.0f) return 0.0f;
  if (f >= 1.0f) return 1.0f;
  return f;
}

void format_source_label(uint8_t src, bool short_fmt, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  if (src == 0 || src > 64) {
    std::snprintf(buf, buf_len, "%s", short_fmt ? "OFF" : "Off (Unpatched)");
    return;
  }
  if (src <= 32) {
    std::snprintf(buf, buf_len, short_fmt ? "In%02u" : "In %02u", unsigned(src));
    return;
  }
  if (src <= 38) {
    std::snprintf(buf, buf_len, short_fmt ? "Aux%u" : "Aux %u", unsigned(src - 32));
    return;
  }
  if (src == 39) {
    std::snprintf(buf, buf_len, "%s", short_fmt ? "UsbL" : "USB L");
    return;
  }
  if (src == 40) {
    std::snprintf(buf, buf_len, "%s", short_fmt ? "UsbR" : "USB R");
    return;
  }
  if (src <= 48) {
    const unsigned fx = unsigned((src - 41) / 2 + 1);
    const char lr = ((src - 41) % 2 == 0) ? 'L' : 'R';
    std::snprintf(buf, buf_len, short_fmt ? "Fx%u%c" : "Fx %u%c", fx, lr);
    return;
  }
  std::snprintf(buf, buf_len, short_fmt ? "Bs%02u" : "Bus %02u", unsigned(src - 48));
}

}  // namespace

void x32_format_source_short(uint8_t src, char *buf, size_t buf_len) {
  format_source_label(src, true, buf, buf_len);
}

void x32_format_source_full(uint8_t src, char *buf, size_t buf_len) {
  format_source_label(src, false, buf, buf_len);
}

uint8_t x32_default_source_for_strip(int strip_kind_int, int idx) {
  if (strip_kind_int == 0 && idx >= 0 && idx < 32) {
    return static_cast<uint8_t>(idx + 1);
  }
  if (strip_kind_int == 1 && idx >= 0 && idx < 8) {
    return static_cast<uint8_t>(33 + idx);
  }
  return 0;
}

int x32_source_to_headamp_idx(uint8_t src, const uint8_t in_routing[5]) {
  if (src >= 1 && src <= 32) {
    const int blk = (src - 1) / 8;
    const int off = (src - 1) % 8;
    const int rt = in_routing ? static_cast<int>(in_routing[blk]) : blk;
    if (rt <= 3) return rt * 8 + off;
    if (rt >= 4 && rt <= 9) return 32 + (rt - 4) * 8 + off;
    if (rt >= 10 && rt <= 15) return 80 + (rt - 10) * 8 + off;
    return -1;
  }
  if (src >= 33 && src <= 38) {
    if (!in_routing) return -1;
    const int auxOff = static_cast<int>(src - 33);
    const int auxRt = static_cast<int>(in_routing[4]);
    if (auxRt >= 1 && auxRt <= 3) {
      const int cnt = auxRt * 2;
      return (auxOff < cnt) ? auxOff : -1;
    }
    if (auxRt >= 4 && auxRt <= 6) {
      const int cnt = (auxRt - 3) * 2;
      return (auxOff < cnt) ? (32 + auxOff) : -1;
    }
    if (auxRt >= 7 && auxRt <= 9) {
      const int cnt = (auxRt - 6) * 2;
      return (auxOff < cnt) ? (80 + auxOff) : -1;
    }
    return -1;
  }
  return -1;
}

bool x32_headamp_is_gain_split(int ha_idx, uint8_t ha_flags) {
  if (ha_idx < 0 || ha_idx >= 128) return true;
  if (ha_idx < 32) return (ha_flags & 0x02u) != 0;
  if (ha_idx < 80) return (ha_flags & 0x04u) != 0;
  return (ha_flags & 0x08u) != 0;
}

void x32_format_headamp_socket(int ha_idx, bool short_fmt, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  if (ha_idx < 0 || ha_idx >= 128) {
    buf[0] = '\0';
    return;
  }
  if (ha_idx < 32) {
    std::snprintf(buf, buf_len, short_fmt ? "XLR %02d" : "Local XLR %02d", ha_idx + 1);
  } else if (ha_idx < 80) {
    std::snprintf(buf, buf_len, short_fmt ? "A:%02d" : "AES50-A %02d", ha_idx - 31);
  } else {
    std::snprintf(buf, buf_len, short_fmt ? "B:%02d" : "AES50-B %02d", ha_idx - 79);
  }
}

float x32_gain_to_db(float f) {
  const float raw = -12.0f + clamp01(f) * 72.0f;
  return std::round(raw * 2.0f) * 0.5f;
}

float x32_db_to_gain(float db) {
  if (!std::isfinite(db)) return clamp01(12.0f / 72.0f);
  return clamp01((db + 12.0f) / 72.0f);
}

void x32_format_gain_db(float f, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  const float db = x32_gain_to_db(f);
  if (db >= -0.05f && db <= 0.05f) {
    std::snprintf(buf, buf_len, "0.0 dB");
  } else if (db > 0.05f) {
    std::snprintf(buf, buf_len, "+%.1f dB", static_cast<double>(db));
  } else {
    std::snprintf(buf, buf_len, "%.1f dB", static_cast<double>(db));
  }
}

float x32_trim_to_db(float f) {
  const float raw = -18.0f + clamp01(f) * 36.0f;
  return std::round(raw * 4.0f) * 0.25f;
}

float x32_db_to_trim(float db) {
  if (!std::isfinite(db)) return 0.5f;
  return clamp01((db + 18.0f) / 36.0f);
}

void x32_format_trim_db(float f, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  const float db = x32_trim_to_db(f);
  const int q = static_cast<int>(std::lround(db * 4.0f));
  if (q == 0) {
    std::snprintf(buf, buf_len, "0.0 dB");
    return;
  }
  const int absQ = q < 0 ? -q : q;
  if ((absQ % 2) == 1) {
    std::snprintf(buf, buf_len, "%s%.2f dB", q > 0 ? "+" : "", static_cast<double>(db));
  } else {
    std::snprintf(buf, buf_len, "%s%.1f dB", q > 0 ? "+" : "", static_cast<double>(db));
  }
}

float x32_hpf_to_hz(float f) {
  return 20.0f * std::pow(20.0f, clamp01(f));
}

float x32_hz_to_hpf(float hz) {
  if (!std::isfinite(hz)) hz = 80.0f;
  if (hz < 20.0f) hz = 20.0f;
  if (hz > 400.0f) hz = 400.0f;
  return clamp01(std::log(hz / 20.0f) / std::log(20.0f));
}

void x32_format_hpf_hz(float f, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  const int hz = static_cast<int>(std::lround(x32_hpf_to_hz(f)));
  std::snprintf(buf, buf_len, "%d Hz", hz);
}

int x32_pan_to_pct(float f) {
  int pct = static_cast<int>(std::lround((clamp01(f) - 0.5f) * 200.0f));
  if (pct < -100) pct = -100;
  if (pct > 100) pct = 100;
  return pct;
}

float x32_pct_to_pan(int pct) {
  if (pct < -100) pct = -100;
  if (pct > 100) pct = 100;
  return clamp01(static_cast<float>(pct + 100) / 200.0f);
}

void x32_format_pan(float f, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  const int pct = x32_pan_to_pct(f);
  if (pct == 0) {
    std::snprintf(buf, buf_len, "C");
  } else if (pct < 0) {
    std::snprintf(buf, buf_len, "L%d", -pct);
  } else {
    std::snprintf(buf, buf_len, "R%d", pct);
  }
}

namespace {

bool parseOtaSemver(const char *s, int out[3]) {
  if (!s || !s[0]) return false;
  if (s[0] == 'v' || s[0] == 'V') ++s;
  if (s[0] < '0' || s[0] > '9') return false;
  out[0] = out[1] = out[2] = 0;
  int idx = 0;
  while (*s && idx < 3) {
    if (*s < '0' || *s > '9') return false;
    int val = 0;
    while (*s >= '0' && *s <= '9') {
      if (val < 100000) val = val * 10 + (*s - '0');
      ++s;
    }
    out[idx++] = val;
    if (*s == '\0') break;
    if (*s == '.' && idx < 3) {
      ++s;
      if (*s < '0' || *s > '9') return false;
    } else if (*s == '-' || *s == '+') {
      break;
    } else {
      return false;
    }
  }
  return true;
}

bool isHexChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isValidUnicastIpv4Str(const char *s) {
  if (!s || !s[0]) return false;
  uint8_t oct[4] = {0, 0, 0, 0};
  const char *p = s;
  for (int i = 0; i < 4; ++i) {
    if (*p < '0' || *p > '9') return false;
    int val = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      val = val * 10 + (*p - '0');
      digits++;
      if (digits > 3 || val > 255) return false;
      ++p;
    }
    oct[i] = static_cast<uint8_t>(val);
    if (i < 3) {
      if (*p != '.') return false;
      ++p;
    }
  }
  if (*p != '\0') return false;
  if (oct[0] == 0 && oct[1] == 0 && oct[2] == 0 && oct[3] == 0) return false;
  if (oct[0] == 255 && oct[1] == 255 && oct[2] == 255 && oct[3] == 255) return false;
  return true;
}

}  // namespace

int x32_ota_compare_semver(const char *candidate, const char *current) {
  int a[3] = {0, 0, 0};
  int b[3] = {0, 0, 0};
  if (!parseOtaSemver(candidate, a) || !parseOtaSemver(current, b)) return -2;
  for (int i = 0; i < 3; ++i) {
    if (a[i] > b[i]) return 1;
    if (a[i] < b[i]) return -1;
  }
  return 0;
}

bool x32_ota_validate_manifest(const char *version,
                               const char *url,
                               uint32_t size,
                               const char *sha256Hex,
                               const char *expectedRepo,
                               uint32_t maxBytes) {
  if (!version || !url || !sha256Hex || !expectedRepo || !expectedRepo[0]) return false;
  const size_t vLen = std::strlen(version);
  if (vLen == 0 || vLen > 23) return false;
  if (x32_ota_compare_semver(version, "0.0.0") < 0) return false;

  const size_t uLen = std::strlen(url);
  if (uLen == 0 || uLen >= 240) return false;
  for (size_t i = 0; i < uLen; ++i) {
    if (static_cast<unsigned char>(url[i]) <= 0x20u) return false;
  }
  static constexpr const char kPrefix[] = "https://github.com/";
  static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
  static constexpr const char kRelSeg[] = "/releases/";
  static constexpr size_t kRelSegLen = sizeof(kRelSeg) - 1;
  const size_t repoLen = std::strlen(expectedRepo);
  if (uLen <= kPrefixLen + repoLen + kRelSegLen) return false;
  if (std::strncmp(url, kPrefix, kPrefixLen) != 0) return false;
  if (std::strncmp(url + kPrefixLen, expectedRepo, repoLen) != 0) return false;
  if (std::strncmp(url + kPrefixLen + repoLen, kRelSeg, kRelSegLen) != 0) return false;

  if (size < 200000u || size > maxBytes) return false;
  if (std::strlen(sha256Hex) != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    if (!isHexChar(sha256Hex[i])) return false;
  }
  return true;
}

bool x32_is_valid_xinfo_reply(const OscPacketView &pkt) {
  if (!pkt.addr || std::strcmp(pkt.addr, "/xinfo") != 0) return false;
  if (pkt.num_args < 4) return false;
  for (int i = 0; i < 4; ++i) {
    if (pkt.args[i].type != OSC_ARG_STR || pkt.args[i].s == nullptr) return false;
  }
  if (!isValidUnicastIpv4Str(pkt.args[0].s)) return false;
  if (pkt.args[1].s[0] == '\0') return false;
  const char *model = pkt.args[2].s;
  const bool isX32OrM32 =
      (model[0] == 'X' || model[0] == 'x' || model[0] == 'M' || model[0] == 'm') &&
      model[1] == '3' && model[2] == '2';
  return isX32OrM32;
}

namespace {

bool parseDecimalRange(const char *&p, int minDigits, int maxDigits, int *out) {
  int val = 0;
  int count = 0;
  while (count < maxDigits && *p >= '0' && *p <= '9') {
    val = val * 10 + (*p - '0');
    ++p;
    ++count;
  }
  if (count < minDigits) return false;
  if (*p >= '0' && *p <= '9') return false;
  *out = val;
  return true;
}

int parseMonthAbbrev(const char *&p) {
  static constexpr const char *kMonths[12] = {
      "jan", "feb", "mar", "apr", "may", "jun",
      "jul", "aug", "sep", "oct", "nov", "dec",
  };
  char lower[4] = {0, 0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    const char c = p[i];
    if (c == '\0') return -1;
    lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
  }
  for (int m = 0; m < 12; ++m) {
    if (std::strcmp(lower, kMonths[m]) == 0) {
      p += 3;
      return m + 1;
    }
  }
  return -1;
}

bool skipSpacesRequired(const char *&p) {
  if (*p != ' ') return false;
  while (*p == ' ') ++p;
  return true;
}

}  // namespace

int64_t x32_parse_http_date_epoch(const char *httpDate) {
  if (!httpDate || !httpDate[0]) return -1;
  const char *p = httpDate;
  while (*p == ' ') ++p;

  const char *comma = nullptr;
  for (int i = 0; p[i] != '\0' && i < 12; ++i) {
    if (p[i] == ',') {
      comma = p + i;
      break;
    }
  }
  if (comma) {
    p = comma + 1;
    while (*p == ' ') ++p;
  }

  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (!parseDecimalRange(p, 1, 2, &day)) return -1;
  if (!skipSpacesRequired(p)) return -1;

  const int month = parseMonthAbbrev(p);
  if (month < 1 || month > 12) return -1;
  if (!skipSpacesRequired(p)) return -1;

  if (!parseDecimalRange(p, 4, 4, &year) || year < 1970 || year > 2199) return -1;
  if (!skipSpacesRequired(p)) return -1;

  if (!parseDecimalRange(p, 2, 2, &hour) || hour < 0 || hour > 23) return -1;
  if (*p++ != ':') return -1;
  if (!parseDecimalRange(p, 2, 2, &minute) || minute < 0 || minute > 59) return -1;
  if (*p++ != ':') return -1;
  if (!parseDecimalRange(p, 2, 2, &second) || second < 0 || second > 60) return -1;

  if (*p != '\0') {
    if (!skipSpacesRequired(p)) return -1;
    if (*p != '\0') {
      if (std::strncmp(p, "GMT", 3) != 0 && std::strncmp(p, "UTC", 3) != 0) return -1;
      p += 3;
      while (*p == ' ') ++p;
      if (*p != '\0') return -1;
    }
  }

  static constexpr int kDaysInMonth[12] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  const bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  const int maxDay = kDaysInMonth[month - 1] + (month == 2 && isLeap ? 1 : 0);
  if (day < 1 || day > maxDay) return -1;

  const int y = year - (month <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2u) / 5u +
                       static_cast<unsigned>(day) - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097LL + static_cast<int64_t>(doe) - 719468LL;
  return days * 86400LL + static_cast<int64_t>(hour) * 3600LL +
         static_cast<int64_t>(minute) * 60LL + static_cast<int64_t>(second);
}
