#include "net.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Manual UTC "timegm" — avoids depending on a TZ-aware timegm() that may not
// exist in the ESP32 core's newlib build. Only used for display purposes
// (reset-time labels/countdowns), so a plain proleptic-Gregorian day count
// is accurate enough.
static time_t timegm_utc(const struct tm *tm) {
  static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int year = tm->tm_year + 1900;
  int month = tm->tm_mon + 1; // 1-12
  long days = 0;

  for (int y = 1970; y < year; y++) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    days += leap ? 366 : 365;
  }

  bool leap_this_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  for (int m = 1; m < month; m++) {
    days += mdays[m - 1];
    if (m == 2 && leap_this_year) days += 1;
  }

  days += tm->tm_mday - 1;
  return days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
}

static time_t parse_iso8601_utc(const char *s) {
  if (!s || !*s) return 0;
  struct tm tm = {};
  if (sscanf(s, "%d-%d-%dT%d:%d:%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
             &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
    return 0;
  }
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
  return timegm_utc(&tm);
}

void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

bool wifi_is_connected() {
  return WiFi.status() == WL_CONNECTED;
}

bool parse_usage_json(const char *json, UsageData &out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("parse_usage_json: JSON parse failed: %s\n", err.c_str());
    return false;
  }

  UsageData parsed = {};

  parsed.session_used_pct = doc["today"]["session_used_pct"] | 0;
  parsed.session_resets_at =
      parse_iso8601_utc(doc["today"]["session_resets_at"] | "");
  parsed.tokens_used = doc["today"]["tokens_used"] | 0L;
  parsed.tokens_limit = doc["today"]["tokens_limit"] | 0L;

  parsed.week_used_pct = doc["week"]["used_pct"] | 0;
  parsed.week_resets_at = parse_iso8601_utc(doc["week"]["resets_at"] | "");

  int i = 0;
  for (JsonVariantConst v : doc["week"]["daily_pct"].as<JsonArrayConst>()) {
    if (i >= 7) break;
    parsed.daily_pct[i++] = v.as<int>();
  }
  while (i < 7) parsed.daily_pct[i++] = 0;

  int m = 0;
  for (JsonObjectConst mo : doc["models"].as<JsonArrayConst>()) {
    if (m >= 3) break;
    const char *name = mo["name"] | "";
    strncpy(parsed.models[m].name, name, sizeof(parsed.models[m].name) - 1);
    parsed.models[m].name[sizeof(parsed.models[m].name) - 1] = '\0';
    parsed.models[m].pct = mo["pct"] | 0;
    m++;
  }
  parsed.model_count = m;

  parsed.thinking = doc["thinking"] | false;

  out = parsed;
  return true;
}

bool fetch_usage_data(UsageData &out) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("fetch_usage_data: WiFi not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // gist raw content — skip cert validation

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // gist raw URLs 302
  if (!http.begin(client, DATA_URL)) {
    Serial.println("fetch_usage_data: http.begin failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("fetch_usage_data: GET failed, code=%d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  return parse_usage_json(body.c_str(), out);
}
