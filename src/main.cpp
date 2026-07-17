#include <Arduino.h>
#include <lvgl.h>

#include "board_init.h"
#include "config.h"
#include "mqtt_client.h"
#include "net.h"
#include "ui.h"

static UsageData usage_data = {};
static unsigned long last_fetch_ms = 0;

void setup() {
  board_init();
  ui_init();

  wifi_connect();
  // NTP with local timezone (drives the on-screen clock). Epoch math for
  // reset countdowns is unaffected — time() always returns UTC epoch.
  configTzTime(TZ_SPEC, "pool.ntp.org", "time.nist.gov");

  mqtt_init();

  // Initial paint: a retained MQTT message will land within a second or two
  // of connecting; the HTTP fetch covers the cold-start case where none has
  // been published yet.
  if (fetch_usage_data(usage_data)) {
    ui_update(usage_data);
  }
  last_fetch_ms = millis();
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

  // HTTP polling only as a fallback while MQTT is down.
  if (!mqtt_connected()) {
    unsigned long now = millis();
    if (now - last_fetch_ms >= FETCH_INTERVAL_MS) {
      last_fetch_ms = now;
      if (fetch_usage_data(usage_data)) {
        ui_update(usage_data);
      }
    }
  }

  delay(5);
}
