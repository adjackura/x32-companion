#pragma once

#include <cstdint>
#include <lvgl.h>

static constexpr int LCD_H_RES = 800;
static constexpr int LCD_V_RES = 480;

// Initializes CH422G I2C expander (keeping EXIO5 USB_SEL=0),
// GT911 capacitive touch, ST7262 800x480 RGB panel with SRAM bounce buffer,
// and LVGL display + touch drivers.
bool boardDriverInit();

// Controls LCD backlight via CH422G EXIO2 while preserving EXIO5=0.
void boardSetBacklight(bool on);
bool boardGetBacklight();
uint32_t boardGetActivitySeq();
bool boardIsTouchDetected();
