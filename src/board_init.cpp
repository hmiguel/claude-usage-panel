#include "board_init.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <TouchDrvGT911.hpp>
#include <Arduino_GFX_Library.h>

// ---------------------------------------------------------------------------
// Real board bring-up, ported from Waveshare's 06_LVGL_Arduino_v9.ino demo
// (the actual vendor demo — pasted into the project by the user). No
// AXP2101 on this board: panel reset goes through a TCA9554-compatible IO
// expander (Arduino_GFX's Arduino_XCA9554SWSPI), toggling pins 5/6.
// ---------------------------------------------------------------------------

#define PANEL_W 480
#define PANEL_H 480

// The ESP-IDF BSP for this board drives the backlight via LEDC PWM on this
// pin; the Arduino demo we ported never touches it (likely relied on a
// pin_config.h DEV_DEVICE_INIT() macro we don't have). Drive it statically
// high — full brightness, no dimming needed for this panel.
#define BSP_LCD_BACKLIGHT 4

static TouchDrvGT911 gt911;
static int16_t touch_x[5], touch_y[5];

static Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
    7, 0, 2, 1, &Wire, 0x20);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    17 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 9 /* PCLK */,
    10 /* B0 */, 11 /* B1 */, 12 /* B2 */, 13 /* B3 */, 14 /* B4 */,
    21 /* G0 */, 8 /* G1 */, 18 /* G2 */, 45 /* G3 */, 38 /* G4 */, 39 /* G5 */,
    40 /* R0 */, 41 /* R1 */, 42 /* R2 */, 2 /* R3 */, 1 /* R4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    PANEL_W, PANEL_H, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, st7701_type1_init_operations,
    sizeof(st7701_type1_init_operations));

static lv_display_t *disp;
static lv_color_t *disp_draw_buf;
static volatile bool fb_dirty = false;

static uint32_t millis_cb() { return millis(); }

static void my_disp_flush(lv_display_t *disp_, const lv_area_t *area, uint8_t *px_map) {
  (void)area;
  (void)px_map;
  // LVGL renders into a separate shadow buffer (vendor-demo pattern);
  // board_loop_tick() copies it to the panel through a driver draw call,
  // which performs the cache writeback that direct framebuffer writes
  // skip (skipping it caused faint stale-cache-line artifacts on screen).
  fb_dirty = true;
  lv_display_flush_ready(disp_);
}

static void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint8_t touched = gt911.getPoint(touch_x, touch_y, gt911.getSupportTouchPoint());
  if (touched > 0) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_x[0];
    data->point.y = touch_y[0];
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Aligns invalidated regions to even pixel boundaries — required by this
// panel's DMA/bounce-buffer path (from the vendor demo).
static void rounder_event_cb(lv_event_t *e) {
  lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
  area->x1 = (area->x1 >> 1) << 1;
  area->y1 = (area->y1 >> 1) << 1;
  area->x2 = ((area->x2 >> 1) << 1) + 1;
  area->y2 = ((area->y2 >> 1) << 1) + 1;
}

void board_init() {
  Serial.begin(115200);

  // Backlight is ACTIVE LOW on this board — the vendor BSP's brightness code
  // inverts duty (100% brightness = duty 0). Driving this HIGH turns the
  // backlight OFF, which is exactly the mistake that kept the screen black.
  pinMode(BSP_LCD_BACKLIGHT, OUTPUT);
  digitalWrite(BSP_LCD_BACKLIGHT, LOW);

  Wire.begin(47, 48);
  expander->pinMode(5, OUTPUT);
  expander->pinMode(6, OUTPUT);
  expander->digitalWrite(6, LOW);
  delay(200);
  expander->digitalWrite(5, LOW);
  delay(200);
  expander->digitalWrite(5, HIGH);
  delay(200);

  if (!gfx->begin()) {
    Serial.println("board_init: gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

  gt911.setPins(-1, -1);
  if (!gt911.begin(Wire, GT911_SLAVE_ADDRESS_L, 47, 48)) {
    Serial.println("board_init: GT911 not found - touch will not work");
  } else {
    gt911.setMaxTouchPoint(1);
  }

  lv_init();
  lv_tick_set_cb(millis_cb);

  // Full-size shadow buffer, exactly as the vendor demo allocates it:
  // try internal SRAM first (will fail for 450KB — that's expected and
  // matches the vendor code), then fall back to PSRAM.
  uint32_t buf_size_px = (uint32_t)PANEL_W * (uint32_t)PANEL_H;
  disp_draw_buf = (lv_color_t *)heap_caps_malloc(buf_size_px * sizeof(lv_color_t),
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(buf_size_px * sizeof(lv_color_t),
                                                    MALLOC_CAP_8BIT);
  }
  if (!disp_draw_buf) {
    Serial.println("board_init: shadow buffer allocation failed");
    return;
  }

  disp = lv_display_create(PANEL_W, PANEL_H);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, disp_draw_buf, NULL, buf_size_px * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  Serial.println("board_init: ST7701/GT911 driver bring-up complete");
}

void board_loop_tick() {
  lv_timer_handler();
  if (fb_dirty) {
    fb_dirty = false;
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, PANEL_W, PANEL_H);
  }
}
