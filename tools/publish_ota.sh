#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
HEADER_PATH="${REPO_ROOT}/firmware/x32_companion/ota_updater.h"
BIN_PATH="${REPO_ROOT}/build/x32_companion.ino.bin"
MANIFEST_PATH="${REPO_ROOT}/build/manifest.json"

DRY_RUN=0
RAW_VERSION=""

usage() {
  cat <<'EOF'
Usage: publish_ota.sh [--dry-run] <version|tag>

Builds X32 Companion firmware (unless SKIP_BUILD=1), verifies the binary
header/size/version against firmware/x32_companion/ota_updater.h, generates
build/manifest.json, and publishes both assets to GitHub Releases via gh CLI.

Examples:
  bash tools/publish_ota.sh --dry-run v1.1.0
  SKIP_BUILD=1 bash tools/publish_ota.sh --dry-run 1.1.0
  bash tools/publish_ota.sh v1.2.0
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "ERROR: Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ -n "${RAW_VERSION}" ]]; then
        echo "ERROR: Unexpected extra argument: $1" >&2
        usage >&2
        exit 2
      fi
      RAW_VERSION="$1"
      shift
      ;;
  esac
done

if [[ -z "${RAW_VERSION}" ]]; then
  echo "ERROR: Missing release version argument (e.g. v1.1.0)." >&2
  usage >&2
  exit 2
fi

VERSION="${RAW_VERSION#v}"
if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "ERROR: Version '${RAW_VERSION}' must match [v]X.Y.Z (digits only)." >&2
  exit 2
fi
TAG="v${VERSION}"

if [[ ! -f "${HEADER_PATH}" ]]; then
  echo "ERROR: Missing header ${HEADER_PATH}" >&2
  exit 1
fi

read -r FW_VERSION GITHUB_REPO < <(
  python3 - "${HEADER_PATH}" <<'PY'
import re
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text(encoding="utf-8")
ver_m = re.search(r'X32_COMPANION_FW_VERSION\s*=\s*"([^"]+)"', text)
repo_m = re.search(r'X32_COMPANION_GITHUB_REPO\s*=\s*"([^"]+)"', text)
if not ver_m or not repo_m:
    sys.exit("Failed to parse version/repo from ota_updater.h")
print(ver_m.group(1), repo_m.group(1))
PY
)

if [[ "${VERSION}" != "${FW_VERSION}" ]]; then
  echo "ERROR: Requested version '${VERSION}' does not match X32_COMPANION_FW_VERSION '${FW_VERSION}' in ${HEADER_PATH}." >&2
  echo "Update X32_COMPANION_FW_VERSION before publishing ${TAG}." >&2
  exit 1
fi

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  echo "Building firmware via build.sh..."
  bash "${REPO_ROOT}/build.sh"
fi

if [[ ! -f "${BIN_PATH}" ]]; then
  echo "ERROR: Firmware binary not found at ${BIN_PATH}" >&2
  exit 1
fi

DOWNLOAD_URL="https://github.com/${GITHUB_REPO}/releases/download/${TAG}/x32_companion.ino.bin"

python3 - "${BIN_PATH}" "${MANIFEST_PATH}" "${VERSION}" "${TAG}" "${DOWNLOAD_URL}" <<'PY'
import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

bin_path = Path(sys.argv[1])
manifest_path = Path(sys.argv[2])
version = sys.argv[3]
tag = sys.argv[4]
url = sys.argv[5]

data = bin_path.read_bytes()
size = len(data)
max_ota_bytes = 0x3EA000  # 4,104,192 bytes (rainmaker_8MB OTA slot)
if size < 200000 or size > max_ota_bytes:
    sys.exit(f"ERROR: Binary size {size} bytes outside safe OTA bounds [200000, {max_ota_bytes}]")
if data[0] != 0xE9:
    sys.exit(f"ERROR: Invalid ESP32 image magic byte 0x{data[0]:02x} (expected 0xE9)")

sha256_hex = hashlib.sha256(data).hexdigest().lower()
manifest = {
    "version": version,
    "tag": tag,
    "url": url,
    "size": size,
    "sha256": sha256_hex,
    "board": "waveshare_esp32_s3_touch_lcd_7",
    "published_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
}
manifest_path.parent.mkdir(parents=True, exist_ok=True)
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(f"Verified {bin_path.name}: {size} bytes, sha256={sha256_hex}")
PY

if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "[DRY-RUN] Generated ${MANIFEST_PATH} (skipping gh release create):"
  cat "${MANIFEST_PATH}"
  exit 0
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "ERROR: GitHub CLI (gh) is required to publish a release." >&2
  exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
  echo "ERROR: gh is not authenticated. Run 'gh auth login' first." >&2
  exit 1
fi

echo "Publishing ${TAG} to github.com/${GITHUB_REPO}..."
gh release create "${TAG}" "${BIN_PATH}" "${MANIFEST_PATH}" \
  --repo "${GITHUB_REPO}" \
  --title "X32 Companion ${TAG}" \
  --generate-notes

echo "Published ${TAG} successfully."
