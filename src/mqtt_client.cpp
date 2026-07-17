#include "mqtt_client.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

static WiFiClientSecure tls;
static PubSubClient mqtt(tls);

static UsageData pending_usage = {};
static volatile bool usage_pending = false;
static volatile bool thinking_pending = false;
static volatile bool thinking_value = false;

static unsigned long last_reconnect_ms = 0;
static unsigned long reconnect_backoff_ms = 1000;

static void on_message(char *topic, byte *payload, unsigned int len) {
  if (strcmp(topic, MQTT_TOPIC_USAGE) == 0) {
    static char buf[1536];
    if (len >= sizeof(buf)) {
      Serial.printf("mqtt: usage payload too large (%u bytes), dropped\n", len);
      return;
    }
    memcpy(buf, payload, len);
    buf[len] = '\0';
    UsageData parsed;
    if (parse_usage_json(buf, parsed)) {
      pending_usage = parsed;
      usage_pending = true;
      Serial.println("mqtt: usage update received");
    }
  } else if (strcmp(topic, MQTT_TOPIC_THINKING) == 0) {
    thinking_value = (len > 0 && (payload[0] == '1' || payload[0] == 't' || payload[0] == 'T'));
    thinking_pending = true;
    Serial.printf("mqtt: thinking=%d\n", (int)thinking_value);
  }
}

static bool try_connect() {
  // Random client-id suffix so a stale broker session never blocks us.
  char client_id[40];
  snprintf(client_id, sizeof(client_id), "usage-panel-%04x", (unsigned)esp_random() & 0xFFFF);

  Serial.printf("mqtt: connecting to %s:%d...\n", MQTT_HOST, MQTT_PORT);
  if (!mqtt.connect(client_id, MQTT_USER, MQTT_PASS)) {
    Serial.printf("mqtt: connect failed, state=%d\n", mqtt.state());
    return false;
  }

  mqtt.subscribe(MQTT_TOPIC_USAGE, 1);
  mqtt.subscribe(MQTT_TOPIC_THINKING, 1);
  Serial.println("mqtt: connected + subscribed");
  return true;
}

void mqtt_init() {
  tls.setInsecure(); // HiveMQ Cloud uses a Let's Encrypt cert; pin later if desired
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(on_message);
  // Default PubSubClient packet cap is 256 bytes — too small for the usage
  // JSON (~500 bytes). Must cover payload + topic + header.
  mqtt.setBufferSize(1536);

  if (try_connect()) {
    reconnect_backoff_ms = 1000;
  }
  last_reconnect_ms = millis();
}

void mqtt_loop() {
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  unsigned long now = millis();
  if (now - last_reconnect_ms < reconnect_backoff_ms) return;
  last_reconnect_ms = now;

  if (WiFi.status() != WL_CONNECTED) return;

  if (try_connect()) {
    reconnect_backoff_ms = 1000;
  } else {
    // Exponential backoff, capped at 60s
    reconnect_backoff_ms = min(reconnect_backoff_ms * 2, 60000UL);
  }
}

bool mqtt_connected() {
  return mqtt.connected();
}

bool mqtt_take_usage(UsageData &out) {
  if (!usage_pending) return false;
  usage_pending = false;
  out = pending_usage;
  return true;
}

bool mqtt_take_thinking(bool &thinking) {
  if (!thinking_pending) return false;
  thinking_pending = false;
  thinking = thinking_value;
  return true;
}
