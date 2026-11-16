#pragma once

inline constexpr const char *X32_COMPANION_FW_VERSION = "1.1.1";
inline constexpr const char *X32_COMPANION_GITHUB_REPO = "adjackura/x32-companion";
inline constexpr const char *X32_COMPANION_MANIFEST_URL =
    "https://github.com/adjackura/x32-companion/releases/latest/download/manifest.json";

void otaUpdaterMarkBootValid();
void otaUpdaterRequestCheck();
void otaUpdaterRequestInstall();
