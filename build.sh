#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "$HOME/.local/bin/arduino-cli")}"
FQBN="esp32:esp32:waveshare_esp32_s3_touch_lcd_7:PSRAM=enabled,FlashSize=8M,PartitionScheme=rainmaker_8MB,USBMode=hwcdc,CDCOnBoot=default"
SKETCH_DIR="$ROOT/firmware/x32_companion"
BUILD_DIR="$ROOT/build"
INC_FLAG="-O2 -DLV_CONF_INCLUDE_SIMPLE -I$SKETCH_DIR"

echo "==> Embedding Web UI into firmware/x32_companion/webui_assets.h"
python3 "$ROOT/tools/embed_webui.py"

mkdir -p "$BUILD_DIR"

echo "==> Compiling for Waveshare ESP32-S3-Touch-LCD-7..."
"$ARDUINO_CLI" compile \
  --fqbn "$FQBN" \
  --build-property "compiler.c.extra_flags=$INC_FLAG" \
  --build-property "compiler.cpp.extra_flags=$INC_FLAG" \
  --output-dir "$BUILD_DIR" \
  "$SKETCH_DIR"

echo "==> Build complete: $BUILD_DIR/x32_companion.ino.bin"
ls -lh "$BUILD_DIR/x32_companion.ino.bin"
