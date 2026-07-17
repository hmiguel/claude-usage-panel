#pragma once

#include "net.h"

// Builds the 3 swipeable tiles (Today / Week / Models), nav dots, and the
// thinking LED. Call once after board_init() has registered the LVGL
// display driver.
void ui_init();

// Pushes freshly-fetched data into the arc, chart, bars, and LED.
void ui_update(const UsageData &data);

// Updates just the thinking LED — for fast MQTT-pushed on/off events that
// arrive independently of full usage payloads.
void ui_set_thinking(bool thinking);
