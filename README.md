# X32 Companion

Touchscreen and browser companion controller for the Behringer X32 mixer, built for the Waveshare ESP32-S3-Touch-LCD-7 (800x480 RGB display). It communicates directly with the console over UDP port 10023 using OSC.

## Overview

- **Overview**: 32-channel grid with scribble colors, input source labels, live meters, mute/solo, DCA and Mute Group controls, and X32 `[SELECT]` follow.
- **Config**: Input source routing, preamp gain/trim, +48V phantom power, polarity, HPF, stereo link, and pan.
- **Scribble**: Name, color, and icon editor (all 74 X32 icons) for channels, auxes, buses, DCAs, and Main LR.
- **Groups & Faders**: Channel-to-DCA and Mute Group matrices, stereo link toggles, and banked 8-strip faders with Sends-on-Fader.
- **Presets**: Scene recall/save and channel strip preset load/save.
- **Web UI**: Local browser interface on port 80 (`http://x32-companion.local/`), paired using a single-use 6-digit PIN shown on the display.
- **On-device Settings**: The touchscreen `Wi-Fi / X32` tab handles Wi-Fi credentials, X32 discovery or manual IP entry, LCD sleep timeout, and manual firmware updates.

## Build and Flash

Requirements:
- `arduino-cli` with `esp32:esp32` (tested on `3.3.11`), `lvgl@8.4.0`, and `ArduinoJson@7.4.3`
- Python 3 and `g++` (plus Node.js for optional JS syntax checks, and `gh` to publish releases)

Build `build/x32_companion.ino.bin` (also regenerates `firmware/x32_companion/webui_assets.h` from `webui/index.html`):
```bash
bash build.sh
```

Flash over USB (`flash.sh` checks `/sys/class/tty/*/device` and only uploads to standard ESP32 USB-serial VIDs):
```bash
bash flash.sh [optional:/dev/ttyACM0]
```

Note: Moving from an older `huge_app` build to the `rainmaker_8MB` dual-OTA partition scheme requires one USB flash before wireless updates can be used.

## Setup

1. Power the board, open the **Wi-Fi / X32** tab on the touchscreen, and connect to the same network as the mixer.
2. Tap **Discover X32** or enter the console IP manually and tap **Connect**.
3. Optional: open `http://<device-ip>/` or `http://x32-companion.local/` in a browser and enter the 6-digit PIN from the screen.

## OTA Updates

Firmware updates are manual only and are never checked or installed automatically. On the touchscreen **Wi-Fi / X32** tab, tap **Check**, then tap **Install** twice to download, verify SHA-256, and reboot.

To publish a release (if forking, update `X32_COMPANION_GITHUB_REPO` in `firmware/x32_companion/ota_updater.h` first):
1. Bump `X32_COMPANION_FW_VERSION` in `firmware/x32_companion/ota_updater.h`.
2. Verify the binary and generated `build/manifest.json` locally:
   ```bash
   bash tools/publish_ota.sh --dry-run v1.2.0
   ```
3. Create the GitHub Release and upload `x32_companion.ino.bin` + `manifest.json`:
   ```bash
   bash tools/publish_ota.sh v1.2.0
   ```

## Testing

Host C++ unit tests:
```bash
g++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/x32_companion \
  tests/test_osc_codec.cpp firmware/x32_companion/osc_codec.cpp \
  -o /tmp/test_osc_codec && /tmp/test_osc_codec
```

End-to-end Web UI, mock X32 OSC, and OTA manifest tests:
```bash
python3 tests/test_e2e_x32_and_webui.py
```

Standalone virtual X32 for LAN/desktop testing (optional dashboard at `http://localhost:8085/`):
```bash
python3 tools/x32_mock_server.py --http-port 8085 --name X32-VIRTUAL
```

## License

Licensed under the GNU General Public License v3.0 ([LICENSE](LICENSE)).

The 64x64 X32 scribble-strip icon bitmaps in `firmware/x32_companion/x32_icons.h` originate from Behringer X32 resources extracted by Patrick-Gilles Maillot and redistributed via [mamarguerat/behringer-icons](https://github.com/mamarguerat/behringer-icons) under GPLv3.
