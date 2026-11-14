#include "board_driver.h"

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

static constexpr int I2C_SDA_PIN = 8;
static constexpr int I2C_SCL_PIN = 9;
static constexpr int TP_INT_PIN = 4;

static constexpr uint8_t CH422G_ADDR_MODE = 0x24;
static constexpr uint8_t CH422G_ADDR_IO = 0x38;

// CH422G IO bits:
// Bit 1 (0x02): TP_RST
// Bit 2 (0x04): LCD_BL (1 = ON)
// Bit 3 (0x08): LCD_RST (1 = normal)
// Bit 4 (0x10): SD_CS (1 = deselected)
// Bit 5 (0x20): USB_SEL (MUST ALWAYS BE 0 to keep USB active!)
static constexpr uint8_t CH422G_BIT_TP_RST = (1u << 1);
static constexpr uint8_t CH422G_BIT_LCD_BL = (1u << 2);
static constexpr uint8_t CH422G_BIT_LCD_RST = (1u << 3);
static constexpr uint8_t CH422G_BIT_SD_CS = (1u << 4);
static constexpr uint8_t CH422G_BIT_USB_SEL = (1u << 5);

uint8_t s_ch422g_out = CH422G_BIT_TP_RST | CH422G_BIT_LCD_BL | CH422G_BIT_LCD_RST | CH422G_BIT_SD_CS;
std::atomic<bool> s_backlight_on{true};
std::atomic<uint32_t> s_activity_seq{1};
std::atomic<bool> s_swallow_until_release{false};
std::atomic<bool> s_wait_release_before_wake{false};
bool s_raw_pressed = false;
uint32_t s_last_raw_press_ms = 0;
uint32_t s_last_sleep_poll_ms = 0;
uint8_t s_gt911_addr = 0;
int16_t s_last_x = 0;
int16_t s_last_y = 0;
bool s_last_pressed = false;

esp_lcd_panel_handle_t s_panel = nullptr;
SemaphoreHandle_t s_i2cMutex = nullptr;
lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t s_disp_drv;
lv_indev_drv_t s_indev_drv;

static constexpr uint32_t LCD_PCLK_HZ = 12000000;  // 240 MHz / (2 * 12 MHz) = 10 exact integer divider (28.4 Hz)

bool ch422gWriteLocked(uint8_t outVal) {
  // Safety guard: never set EXIO5 (USB_SEL) high
  outVal &= static_cast<uint8_t>(~CH422G_BIT_USB_SEL);

  Wire.beginTransmission(CH422G_ADDR_MODE);
  Wire.write(0x01);  // Enable IO output mode
  bool ok = (Wire.endTransmission() == 0);
  if (ok) {
    Wire.beginTransmission(CH422G_ADDR_IO);
    Wire.write(outVal);
    ok = (Wire.endTransmission() == 0);
  }
  if (ok) {
    s_ch422g_out = outVal;
  }
  return ok;
}

bool ch422gWriteRaw(uint8_t outVal) {
  if (s_i2cMutex && xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  const bool ok = ch422gWriteLocked(outVal);
  if (s_i2cMutex) xSemaphoreGive(s_i2cMutex);
  return ok;
}

bool gt911WriteReg8(uint8_t addr, uint16_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool gt911ReadBytes(uint8_t addr, uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const size_t got = Wire.requestFrom(static_cast<int>(addr), static_cast<int>(len));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool initTouchHardware() {
  // Hold TP_INT low while releasing TP_RST so GT911 latches address 0x5D
  pinMode(TP_INT_PIN, OUTPUT);
  digitalWrite(TP_INT_PIN, LOW);

  const uint8_t baseSafe = CH422G_BIT_LCD_BL | CH422G_BIT_SD_CS;
  ch422gWriteRaw(baseSafe);  // TP_RST=0, LCD_RST=0
  delay(20);

  ch422gWriteRaw(baseSafe | CH422G_BIT_TP_RST | CH422G_BIT_LCD_RST);  // TP_RST=1, LCD_RST=1
  delay(10);

  pinMode(TP_INT_PIN, INPUT);
  delay(60);

  static constexpr uint8_t kAddrs[2] = {0x5D, 0x14};
  for (uint8_t addr : kAddrs) {
    uint8_t pid[4] = {0};
    if (gt911ReadBytes(addr, 0x8140, pid, sizeof(pid))) {
      s_gt911_addr = addr;
      Serial.printf("[BOARD] GT911 detected at 0x%02X (PID: %c%c%c%c)\n",
                    addr, pid[0], pid[1], pid[2], pid[3]);
      gt911WriteReg8(s_gt911_addr, 0x814E, 0x00);
      return true;
    }
  }
  Serial.println("[BOARD] Warning: GT911 touch controller did not respond on I2C");
  return false;
}

void lvglDispFlushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  if (!s_panel) {
    lv_disp_flush_ready(drv);
    return;
  }

  // Slice flushes into <=16-row bands (<=25.6 KB, inside the 32 KB D-Cache) so
  // cache writeback finishes in microseconds without starving LCD_CAM GDMA FIFO.
  static constexpr int16_t MAX_FLUSH_ROWS = 16;
  const int16_t width = static_cast<int16_t>(area->x2 - area->x1 + 1);
  const lv_color_t *src = color_p;
  for (int16_t y = area->y1; y <= area->y2; y += MAX_FLUSH_ROWS) {
    int16_t yEnd = y + MAX_FLUSH_ROWS - 1;
    if (yEnd > area->y2) {
      yEnd = area->y2;
    }
    const int16_t rows = static_cast<int16_t>(yEnd - y + 1);
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, y, area->x2 + 1, yEnd + 1, src);
    src += static_cast<size_t>(width) * static_cast<size_t>(rows);
  }
  lv_disp_flush_ready(drv);
}

void lvglTouchReadCb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  (void)indev_drv;
  const uint32_t now = millis();
  const bool blOnAtEntry = s_backlight_on.load(std::memory_order_acquire);
  bool swallowing = s_swallow_until_release.load(std::memory_order_relaxed);
  if (swallowing && static_cast<uint32_t>(now - s_last_raw_press_ms) > 250u) {
    swallowing = false;
    s_swallow_until_release.store(false, std::memory_order_relaxed);
  }

  // Hold the previous touch state until GT911 reports a fresh frame (bit 0x80)
  // so polling faster than the controller scan interval never injects fake releases.
  // Never pass touch events to LVGL while the backlight is off or while swallowing the wake gesture.
  data->point.x = s_last_x;
  data->point.y = s_last_y;
  data->state = (!blOnAtEntry || swallowing)
                    ? LV_INDEV_STATE_RELEASED
                    : (s_last_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED);

  if (s_gt911_addr == 0) return;

  if (!blOnAtEntry && static_cast<uint32_t>(now - s_last_sleep_poll_ms) < 16u) {
    return;
  }
  s_last_sleep_poll_ms = now;

  if (s_i2cMutex && xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return;
  }

  uint8_t status = 0;
  if (!gt911ReadBytes(s_gt911_addr, 0x814E, &status, 1)) {
    if (s_i2cMutex) xSemaphoreGive(s_i2cMutex);
    return;
  }

  bool wokeByTouch = false;
  if (status & 0x80u) {
    const uint8_t points = status & 0x0Fu;
    bool wakeLocked = false;
    if (points > 0) {
      uint8_t raw[6] = {0};
      const bool gotCoords = gt911ReadBytes(s_gt911_addr, 0x814F, raw, sizeof(raw));
      s_raw_pressed = true;
      s_last_raw_press_ms = now;
      s_activity_seq.fetch_add(1u, std::memory_order_relaxed);

      if (!s_backlight_on.load(std::memory_order_acquire)) {
        s_last_pressed = false;
        data->state = LV_INDEV_STATE_RELEASED;
        if (!s_wait_release_before_wake.load(std::memory_order_relaxed)) {
          s_swallow_until_release.store(true, std::memory_order_relaxed);
          wakeLocked = true;
        }
      } else if (s_swallow_until_release.load(std::memory_order_relaxed)) {
        s_last_pressed = false;
        data->state = LV_INDEV_STATE_RELEASED;
      } else if (gotCoords) {
        int16_t x = static_cast<int16_t>(uint16_t(raw[1]) | (uint16_t(raw[2]) << 8));
        int16_t y = static_cast<int16_t>(uint16_t(raw[3]) | (uint16_t(raw[4]) << 8));
        if (x < 0) x = 0;
        if (x >= LCD_H_RES) x = LCD_H_RES - 1;
        if (y < 0) y = 0;
        if (y >= LCD_V_RES) y = LCD_V_RES - 1;
        s_last_x = x;
        s_last_y = y;
        s_last_pressed = true;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
      }
    } else {
      if (s_raw_pressed) {
        s_activity_seq.fetch_add(1u, std::memory_order_relaxed);
      }
      s_raw_pressed = false;
      s_last_pressed = false;
      s_wait_release_before_wake.store(false, std::memory_order_relaxed);
      s_swallow_until_release.store(false, std::memory_order_relaxed);
      data->state = LV_INDEV_STATE_RELEASED;
    }
    // Always clear status latch when buffer ready bit (0x80) was set
    gt911WriteReg8(s_gt911_addr, 0x814E, 0x00);

    if (wakeLocked && ch422gWriteLocked(s_ch422g_out | CH422G_BIT_LCD_BL)) {
      s_backlight_on.store(true, std::memory_order_release);
      s_wait_release_before_wake.store(false, std::memory_order_relaxed);
      wokeByTouch = true;
    }
  }
  if (s_i2cMutex) xSemaphoreGive(s_i2cMutex);

  if (wokeByTouch) {
    Serial.println("[SCREEN] Woke display on touch");
  }
}

bool initRgbPanel() {
  esp_lcd_rgb_panel_config_t panel_config = {};
  // Use 240 MHz PLL_D2 with 12 MHz PCLK for an exact integer divider (240 / (2 * 12) = 10)
  // and 50% duty cycle with zero fractional clock jitter (28.4 Hz, 24 MB/s = 15% of 160 MB/s PSRAM).
  panel_config.clk_src = LCD_CLK_SRC_PLL240M;
  panel_config.timings.pclk_hz = LCD_PCLK_HZ;
  panel_config.timings.h_res = LCD_H_RES;
  panel_config.timings.v_res = LCD_V_RES;
  panel_config.timings.hsync_pulse_width = 4;
  panel_config.timings.hsync_back_porch = 8;
  panel_config.timings.hsync_front_porch = 8;
  panel_config.timings.vsync_pulse_width = 4;
  panel_config.timings.vsync_back_porch = 16;
  panel_config.timings.vsync_front_porch = 16;
  panel_config.timings.flags.pclk_active_neg = 1;

  panel_config.data_width = 16;
  panel_config.bits_per_pixel = 16;
  panel_config.num_fbs = 1;
  // Stream directly from 80 MHz DDR Octal PSRAM (30 MB/s = 18.75% of 160 MB/s bus) with
  // bounce_buffer_size_px = 0 to avoid ESP-IDF's bb_eof_count parity inversion bug
  // (IDFGH-18263) under CONFIG_LCD_RGB_RESTART_IN_VSYNC when Wi-Fi/NVS stalls interrupts.
  panel_config.bounce_buffer_size_px = 0;
  panel_config.dma_burst_size = 64;

  panel_config.hsync_gpio_num = 46;
  panel_config.vsync_gpio_num = 3;
  panel_config.de_gpio_num = 5;
  panel_config.pclk_gpio_num = 7;
  panel_config.disp_gpio_num = -1;

  // Waveshare ESP32-S3-Touch-LCD-7 RGB565 lane mapping (B3..B7, G2..G7, R3..R7)
  const int gpios[16] = {
      14, 38, 18, 17, 10,     // B3..B7
      39, 0,  45, 48, 47, 21, // G2..G7
      1,  2,  42, 41, 40      // R3..R7
  };
  for (int i = 0; i < 16; ++i) {
    panel_config.data_gpio_nums[i] = gpios[i];
  }
  panel_config.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&panel_config, &s_panel) != ESP_OK) {
    Serial.println("[BOARD] esp_lcd_new_rgb_panel failed");
    return false;
  }
  if (esp_lcd_panel_reset(s_panel) != ESP_OK ||
      esp_lcd_panel_init(s_panel) != ESP_OK) {
    Serial.println("[BOARD] esp_lcd_panel init failed");
    return false;
  }
  return true;
}

}  // namespace

bool boardDriverInit() {
  if (!s_i2cMutex) {
    s_i2cMutex = xSemaphoreCreateMutex();
  }
  // Explicit SDA=8, SCL=9! Default Wire.begin() uses SCL=10 which collides with LCD_B7!
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  Wire.setTimeOut(20);

  initTouchHardware();

  if (!initRgbPanel()) {
    return false;
  }

  lv_init();

  static constexpr size_t kRowCandidates[] = {80, 60, 48, 32};
  size_t bufPixels = 0;
  const char *bufLoc = "INTERNAL";
  lv_color_t *buf1 = nullptr;
  for (size_t rows : kRowCandidates) {
    bufPixels = LCD_H_RES * rows;
    buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buf1) break;
  }
  if (!buf1) {
    bufLoc = "PSRAM";
    bufPixels = LCD_H_RES * 120;
    buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }

  lv_disp_draw_buf_init(&s_draw_buf, buf1, nullptr, bufPixels);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = LCD_H_RES;
  s_disp_drv.ver_res = LCD_V_RES;
  s_disp_drv.flush_cb = lvglDispFlushCb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = lvglTouchReadCb;
  lv_indev_drv_register(&s_indev_drv);

  boardSetBacklight(true);
  Serial.printf("[BOARD] Display & LVGL ready (drawBuf=%s %u px, pclk=%u MHz, SRAM free=%u, PSRAM free=%u)\n",
                bufLoc,
                static_cast<unsigned>(bufPixels),
                static_cast<unsigned>(LCD_PCLK_HZ / 1000000u),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));
  return true;
}

void boardSetBacklight(bool on) {
  if (s_i2cMutex && xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }
  uint8_t next = s_ch422g_out;
  if (on) {
    next |= CH422G_BIT_LCD_BL;
  } else {
    next &= static_cast<uint8_t>(~CH422G_BIT_LCD_BL);
  }
  if (ch422gWriteLocked(next)) {
    s_backlight_on.store(on, std::memory_order_release);
    if (on) {
      s_wait_release_before_wake.store(false, std::memory_order_relaxed);
      s_activity_seq.fetch_add(1u, std::memory_order_relaxed);
    } else {
      s_last_pressed = false;
      s_wait_release_before_wake.store(s_raw_pressed, std::memory_order_relaxed);
    }
  }
  if (s_i2cMutex) xSemaphoreGive(s_i2cMutex);
}

bool boardGetBacklight() {
  return s_backlight_on.load(std::memory_order_acquire);
}

uint32_t boardGetActivitySeq() {
  return s_activity_seq.load(std::memory_order_relaxed);
}

bool boardIsTouchDetected() {
  return s_gt911_addr != 0;
}
