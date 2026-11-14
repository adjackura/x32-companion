#!/usr/bin/env python3
"""Compress webui/index.html into firmware/x32_companion/webui_assets.h."""

import gzip
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    src = root / "webui" / "index.html"
    dst = root / "firmware" / "x32_companion" / "webui_assets.h"

    raw = src.read_bytes()
    gz = gzip.compress(raw, compresslevel=9, mtime=0)

    lines = [
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <pgmspace.h>",
        "",
        f"// Auto-generated from webui/index.html (raw={len(raw)} bytes, gzip={len(gz)} bytes)",
        f"static constexpr size_t WEBUI_INDEX_GZ_LEN = {len(gz)};",
        "static const uint8_t WEBUI_INDEX_GZ[] PROGMEM = {",
    ]

    for i in range(0, len(gz), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in gz[i : i + 16])
        lines.append(f"  {chunk},")

    lines.append("};")
    lines.append("")

    dst.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {dst} ({len(raw)} -> {len(gz)} bytes gzipped)")


if __name__ == "__main__":
    main()
