#pragma once

#include <time.h>

struct ModelUsage {
  char name[16];
  int pct;
};

struct UsageData {
  int session_used_pct;
  time_t session_resets_at;
  long tokens_used;
  long tokens_limit;
  int week_used_pct;
  time_t week_resets_at;
  int daily_pct[7];
  ModelUsage models[3];
  int model_count;
  bool thinking;
};

// Blocks until connected (or forever, retrying) — call once from setup().
void wifi_connect();

// True while the WiFi link is up (for the status-bar indicator).
bool wifi_is_connected();

// Parses a usage JSON document (the mock_usage.json schema) into `out`.
// Returns false on parse failure, leaving `out` untouched. Shared by the
// HTTP fetch path and the MQTT message handler.
bool parse_usage_json(const char *json, UsageData &out);

// Fetches DATA_URL and parses it into `out`. Returns false on network/parse
// failure, leaving `out` untouched so the caller can keep showing stale data.
bool fetch_usage_data(UsageData &out);
