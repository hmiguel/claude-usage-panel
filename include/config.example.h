#pragma once

// Copy this file to config.h and fill in your values.
// config.h is gitignored — credentials never get committed.

// Wi-Fi credentials. The board has no 5 GHz radio — use a 2.4 GHz network.
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// --- MQTT (HiveMQ Cloud) — primary transport, push-based ---
#define MQTT_HOST "your-cluster-id.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_USER "your-mqtt-username"
#define MQTT_PASS "your-mqtt-password"
// Full usage JSON (same schema as mock_usage.json), published retained so
// the panel gets the latest state immediately on connect.
#define MQTT_TOPIC_USAGE    "claude/usage"
// Lightweight "1"/"0" for the thinking LED — instant updates.
#define MQTT_TOPIC_THINKING "claude/thinking"

// POSIX TZ spec for the on-screen clock (default: Portugal mainland,
// WET/WEST with automatic DST). Epoch math elsewhere is unaffected.
#define TZ_SPEC "WET0WEST,M3.5.0/1,M10.5.0"

// --- Backlight power management (touch wakes instantly) ---
#define BACKLIGHT_DIM_AFTER_MS  (2 * 60 * 1000UL)   // dim after 2 min idle
#define BACKLIGHT_DIM_PCT       20                   // dimmed brightness %
#define BACKLIGHT_OFF_AFTER_MS  (15 * 60 * 1000UL)  // off after 15 min idle
