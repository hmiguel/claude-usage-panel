#pragma once

#include <stdint.h>

// Powers up the panel via the XCA9554 IO expander reset sequence, brings up
// the ST7701 RGB panel (Arduino_GFX_Library) and GT911 touch (SensorLib),
// and registers LVGL's display/input drivers. Ported from the real vendor
// demo (06_LVGL_Arduino_v9.ino).
void board_init();

// Call once per loop() iteration: drives LVGL's timer handler, blits the
// shadow buffer to the panel, and manages idle backlight dimming.
void board_loop_tick();

// Backlight brightness 0-100% (PWM, polarity handled internally).
void board_set_backlight(uint8_t percent);
