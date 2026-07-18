#include <Arduino.h>
#include <lvgl.h>

#include "board_init.h"
#include "config.h"
#include "mqtt_client.h"
#include "net.h"
#include "ui.h"

static UsageData usage_data = {};

void setup() {
  board_init();
  ui_init();

  wifi_connect();
  // NTP with local timezone (drives the on-screen clock). Epoch math for
  // reset countdowns is unaffected — time() always returns UTC epoch.
  configTzTime(TZ_SPEC, "pool.ntp.org", "time.nist.gov");

  // A retained MQTT message lands within a second or two of connecting,
  // repainting the initial screen.
  mqtt_init();
}

void loop() {
  board_loop_tick();
  mqtt_loop();

  if (mqtt_take_usage(usage_data)) {
    ui_update(usage_data);
  }
  bool thinking;
  if (mqtt_take_thinking(thinking)) {
    ui_set_thinking(thinking);
  }

  delay(5);
}
