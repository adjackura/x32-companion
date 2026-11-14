#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "$HOME/.local/bin/arduino-cli")}"
FQBN="esp32:esp32:waveshare_esp32_s3_touch_lcd_7:PSRAM=enabled,FlashSize=8M,PartitionScheme=rainmaker_8MB,USBMode=hwcdc,CDCOnBoot=default"
SKETCH_DIR="$ROOT/firmware/x32_companion"
BUILD_DIR="$ROOT/build"

get_usb_vid_pid() {
  local dev="$1"
  local tty_name
  tty_name="$(basename "$dev")"
  local sys_path="/sys/class/tty/$tty_name/device"
  if [[ ! -e "$sys_path" ]]; then
    return 1
  fi
  local cur
  cur="$(readlink -f "$sys_path")"
  while [[ "$cur" != "/" ]]; do
    if [[ -f "$cur/idVendor" && -f "$cur/idProduct" ]]; then
      echo "$(cat "$cur/idVendor"):$(cat "$cur/idProduct")"
      return 0
    fi
    cur="$(dirname "$cur")"
  done
  return 1
}

is_allowed_esp32_vid() {
  local vid="$1"
  case "${vid,,}" in
    303a|1a86|10c4|0403) return 0 ;;
    *) return 1 ;;
  esac
}

TARGET_PORT="${1:-}"

if [[ -n "$TARGET_PORT" ]]; then
  if [[ ! -e "$TARGET_PORT" ]]; then
    echo "ERROR: Port $TARGET_PORT does not exist." >&2
    exit 1
  fi
  vidpid="$(get_usb_vid_pid "$TARGET_PORT" || true)"
  vid="${vidpid%%:*}"
  if [[ -n "$vid" ]] && ! is_allowed_esp32_vid "$vid"; then
    echo "SAFETY ABORT: $TARGET_PORT has non-ESP32 USB VID:PID ($vidpid). Refusing to flash." >&2
    exit 2
  fi
else
  for dev in /dev/ttyACM* /dev/ttyUSB*; do
    [[ -e "$dev" ]] || continue
    vidpid="$(get_usb_vid_pid "$dev" || true)"
    vid="${vidpid%%:*}"
    if [[ -n "$vid" ]] && is_allowed_esp32_vid "$vid"; then
      TARGET_PORT="$dev"
      echo "==> Detected ESP32 serial device on $TARGET_PORT (VID:PID=$vidpid)"
      break
    fi
  done
fi

if [[ -z "$TARGET_PORT" ]]; then
  echo "No Waveshare ESP32-S3 serial/USB port detected (safely skipped unrelated USB devices)."
  echo ""
  echo "Troubleshooting checklist:"
  echo "  1. Waveshare ESP32-S3-Touch-LCD-7 has two USB-C ports: try the 'USB' port AND the 'UART1' (CH343) port."
  echo "  2. Confirm the USB-C cable carries data (not power-only) and check the board power switch."
  echo "  3. If factory firmware set CH422G EXIO5 (USB_SEL=1), native USB is switched to CAN:"
  echo "     - Either plug into the 'UART1' USB-C port, OR"
  echo "     - Hold BOOT, press & release RESET, release BOOT to enter ROM download mode (VID:PID 303a:1001)."
  echo "  4. Once detected, re-run: $0"
  exit 1
fi

if [[ ! -f "$BUILD_DIR/x32_companion.ino.bin" ]]; then
  echo "==> No compiled binary in $BUILD_DIR; building first..."
  bash "$ROOT/build.sh"
fi

echo "==> Flashing $BUILD_DIR/x32_companion.ino.bin to $TARGET_PORT..."
"$ARDUINO_CLI" upload \
  --fqbn "$FQBN" \
  --port "$TARGET_PORT" \
  --input-dir "$BUILD_DIR" \
  "$SKETCH_DIR"

echo "==> Flash complete!"
