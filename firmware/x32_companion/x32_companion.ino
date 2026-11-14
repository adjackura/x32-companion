#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_driver.h"
#include "mixer_state.h"
#include "ota_updater.h"
#include "ui_lvgl.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "x32_client.h"

namespace {

void networkTask(void *param) {
  (void)param;
  wifiManagerInit();
  x32ClientInit();
  webServerInit();

  uint32_t lastYieldMs = 0;
  for (;;) {
    AppCommand cmd;
    const bool gotCmd = dequeueAppCommand(&cmd, 1);
    if (gotCmd) {
      x32ClientProcessCommand(cmd);
      for (int i = 0; i < 15 && dequeueAppCommand(&cmd, 0); ++i) {
        x32ClientProcessCommand(cmd);
      }
    }

    wifiManagerTick();
    x32ClientTick();
    webServerTick();

    const uint32_t now = millis();
    if (gotCmd && (now - lastYieldMs >= 10)) {
      lastYieldMs = now;
      vTaskDelay(1);
    } else if (!gotCmd) {
      lastYieldMs = now;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // Never block HWCDC if USB serial monitor is closed
#endif
  delay(200);
  Serial.println("\n=== Behringer X32 Console Companion Booting ===");

  mixerStateInit();

  if (!boardDriverInit()) {
    Serial.println("[BOOT] Display initialization failed!");
  }

  uiLvglInit();
  uiLvglSyncFromState();
  {
    const uint32_t t0Us = micros();
    lv_refr_now(nullptr);
    const uint32_t renderUs = micros() - t0Us;
    Serial.printf("[UI] Initial Overview full-frame render: %.2f ms\n", renderUs / 1000.0f);
  }

  xTaskCreatePinnedToCore(networkTask, "net_task", 8192, nullptr, 2, nullptr, 0);
  otaUpdaterMarkBootValid();
}

void loop() {
  uiLvglSyncFromState();
  uint32_t waitMs = lv_timer_handler();
  if (waitMs < 1) waitMs = 1;
  const uint32_t maxWaitMs = boardGetBacklight() ? 3u : 12u;
  if (waitMs > maxWaitMs) waitMs = maxWaitMs;
  delay(waitMs);
}
