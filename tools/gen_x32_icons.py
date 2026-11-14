#!/usr/bin/env python3
"""Generate firmware/x32_companion/x32_icons.h from official X32 64x64 scribble-strip bitmaps.

Source icons originally extracted from Behringer X32 by Patrick-Gilles Maillot and
redistributed via mamarguerat/behringer-icons (GPLv3).
"""

import io
import struct
import tarfile
import urllib.request
from pathlib import Path

TAR_URL = "https://codeload.github.com/mamarguerat/behringer-icons/tar.gz/refs/heads/main"
ROOT = Path(__file__).resolve().parent.parent
OUT_HEADER = ROOT / "firmware" / "x32_companion" / "x32_icons.h"


def parse_bmp_64x64_1bit(data: bytes) -> bytes:
    bf_off = struct.unpack_from("<I", data, 10)[0]
    bi_size, w, h, _, bpp, comp = struct.unpack_from("<IiiHHI", data, 14)
    if w != 64 or abs(h) != 64 or bpp != 8 or comp != 0:
        raise ValueError(f"Unexpected BMP format: {w}x{h} bpp={bpp} comp={comp}")
    top_down = h < 0
    pal_cnt = (bf_off - 14 - bi_size) // 4
    lums = [
        sum(struct.unpack_from("<BBB", data, 14 + bi_size + i * 4)) // 3
        for i in range(max(pal_cnt, 256))
    ]
    stride = ((w * bpp + 31) // 32) * 4
    bg_lum = lums[data[bf_off]]
    out = bytearray(512)
    for y in range(64):
        src_y = y if top_down else (63 - y)
        row = data[bf_off + src_y * stride : bf_off + (src_y + 1) * stride]
        for x in range(64):
            if abs(lums[row[x]] - bg_lum) > 100:
                out[(y * 64 + x) >> 3] |= 1 << (7 - (x & 7))
    return bytes(out)


def main() -> None:
    raw_tar = urllib.request.urlopen(TAR_URL, timeout=20).read()
    tf = tarfile.open(fileobj=io.BytesIO(raw_tar), mode="r:gz")
    bmps = {}
    for m in tf.getmembers():
        if m.name.endswith(".bmp") and "/bmp/" in m.name:
            bmps[int(Path(m.name).stem)] = tf.extractfile(m).read()

    if len(bmps) != 74 or any(i not in bmps for i in range(1, 75)):
        raise RuntimeError(f"Expected icons 1..74, got {sorted(bmps.keys())}")

    rows = []
    for icon_id in range(2, 75):
        bits = parse_bmp_64x64_1bit(bmps[icon_id])
        assert len(bits) == 512
        hex_bytes = ",".join(f"0x{b:02X}" for b in bits)
        rows.append(f"  {{{hex_bytes}}}")

    header = (
        "#pragma once\n"
        "#include <stdint.h>\n\n"
        "// Native Behringer X32 64x64 1-bit scribble-strip bitmaps for icons 2..74\n"
        "// (Icon 1 is 'None' / blank). Sourced from Patrick-Gilles Maillot /\n"
        "// mamarguerat/behringer-icons (GPLv3). MSB-first, 8 bytes/row, 512 bytes each.\n"
        "inline constexpr uint8_t kX32IconBitmaps[73][512] = {\n"
        + ",\n".join(rows)
        + "\n};\n"
    )
    OUT_HEADER.write_text(header, encoding="utf-8")
    print(f"Generated {OUT_HEADER} (73 icons, {73 * 512} bytes)")


if __name__ == "__main__":
    main()
