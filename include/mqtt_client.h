#pragma once

#include "net.h"

// Sets up the TLS MQTT connection (HiveMQ Cloud) and subscriptions.
// Call once from setup(), after wifi_connect().
void mqtt_init();

// Drives the MQTT client; handles reconnect with backoff. Call every loop().
void mqtt_loop();

// True while the broker connection is up (used to gate the HTTP fallback).
bool mqtt_connected();

// If a new usage payload arrived since the last call, copies it into `out`
// and returns true (consumes the pending flag).
bool mqtt_take_usage(UsageData &out);

// If a thinking on/off message arrived since the last call, stores it in
// `thinking` and returns true (consumes the pending flag).
bool mqtt_take_thinking(bool &thinking);
