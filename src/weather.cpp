#include <Arduino.h>
#include "weather.h"
#include "sprinky.h"
#include <ArduinoJson.h>
#include <TimeLib.h>

#if defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#else  // esp8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#endif

// Yesterday's ET0 as last fetched, keyed by its local calendar day (YYYYMMDD).
// RAM only: a reboot just fetches it again.
static float cachedMm = 0;
static long cachedKey = -1;
static unsigned long lastAttemptMs = 0;
static bool attempted = false;  // first attempt after boot ignores the 01:00 / 1 h gates

static const unsigned long RETRY_MS = 3600000UL;

// Site location and where it came from. Persisted as "locSrc" (+ "lat"/"lon"
// unless LOC_DEFAULT) so a failed lookup after a reboot keeps the last result.
enum : uint8_t { LOC_DEFAULT = 0, LOC_AUTO = 1, LOC_MANUAL = 2 };
float siteLat = LATITUDE_DEG;
float siteLon = LONGITUDE_DEG;
static uint8_t locSrc = LOC_DEFAULT;
static bool geoDone = false;  // IP lookup succeeded this boot
static bool geoAttempted = false;
static unsigned long lastGeoMs = 0;

// Location change posted by a web handler (AsyncTCP task on ESP32), applied in
// loop() so it can't race a fetch in progress. Values first, flag last.
static volatile float pendingLat, pendingLon;
static volatile bool pendingAuto;
static volatile bool locPending = false;

const char *siteLocSource() {
  return locSrc == LOC_MANUAL ? "manual" : locSrc == LOC_AUTO ? "auto" : "default";
}

void loadSiteLocation() {
  locSrc = preferences.getUChar("locSrc", LOC_DEFAULT);
  if (locSrc != LOC_DEFAULT) {
    siteLat = preferences.getFloat("lat", LATITUDE_DEG);
    siteLon = preferences.getFloat("lon", LONGITUDE_DEG);
  }
}

void requestManualLocation(float lat, float lon) {
  pendingLat = lat;
  pendingLon = lon;
  pendingAuto = false;
  locPending = true;
}

void requestAutoLocation() {
  pendingAuto = true;
  locPending = true;
}

// Moving the site invalidates the cached ET0 and makes the next updateWeather()
// fetch it at once (attempted = false skips the 01:00 / 1 h gates).
static void setLocation(float lat, float lon, uint8_t src) {
  const bool moved = fabsf(lat - siteLat) > 0.0001f || fabsf(lon - siteLon) > 0.0001f;
  siteLat = lat;
  siteLon = lon;
  if (moved || src != locSrc) {  // only write flash when something changed
    preferences.putUChar("locSrc", src);
    if (src != LOC_DEFAULT) {
      preferences.putFloat("lat", lat);
      preferences.putFloat("lon", lon);
    }
  }
  locSrc = src;
  if (moved) {
    cachedKey = -1;
    attempted = false;
  }
}

static void applyPendingLocation() {
  if (!locPending) return;
  locPending = false;
  if (pendingAuto) {
    // Back to the config.h default until the lookup (due now) replaces it.
    setLocation(LATITUDE_DEG, LONGITUDE_DEG, LOC_DEFAULT);
    geoDone = false;
    geoAttempted = false;
    webPrint("Location: switched to automatic lookup\n");
  } else {
    setLocation(pendingLat, pendingLon, LOC_MANUAL);
    webPrint("Location: set to %s, %s\n", String(siteLat, 4).c_str(), String(siteLon, 4).c_str());
  }
}

static long dayKey(time_t t) {
  return (long)year(t) * 10000 + month(t) * 100 + day(t);
}

static bool haveYesterday(time_t t) {
  return cachedKey == dayKey(t - SECS_PER_DAY);
}

bool weatherEt0(float &mm) {
  if (!haveYesterday(now())) return false;
  mm = cachedMm;
  return true;
}

// Blocking: DNS (up to ~10 s on ESP8266) + connect + 4 s read. Only called when
// no valve is open, so valve timing is unaffected; a cycle due meanwhile still
// starts within the 5-minute catch-up window. Returns the HTTP status, or 0 if
// the request couldn't be started; body is set only on 200.
static int httpGet(const String &url, String &body) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
#if defined(ESP32)
  http.setConnectTimeout(3000);  // default 5000 ms
#endif
  if (!http.begin(client, url)) return 0;
  const int code = http.GET();
  if (code == HTTP_CODE_OK) body = http.getString();  // de-chunks the response
  http.end();
  return code;
}

// Approximate site location from the public IP address (ip-api.com: free,
// non-commercial, no key, plain HTTP). Usually city-level, plenty for weather.
static void lookupLocation() {
  geoAttempted = true;
  lastGeoMs = millis();
  String body;
  const int code = httpGet("http://ip-api.com/json/?fields=status,lat,lon", body);
  if (code != HTTP_CODE_OK) {
    webPrint("Location: lookup failed (HTTP %d), retry in 1h\n", code);
    return;
  }
  StaticJsonDocument<128> doc;  // {"status":"success","lat":32.7,"lon":-117.1}
  if (deserializeJson(doc, body) || strcmp(doc["status"] | "", "success") != 0 ||
      !doc["lat"].is<float>() || !doc["lon"].is<float>()) {
    webPrint("Location: bad lookup response, retry in 1h\n");
    return;
  }
  const float lat = doc["lat"], lon = doc["lon"];
  if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
    webPrint("Location: bad lookup response (range), retry in 1h\n");
    return;
  }
  geoDone = true;
  setLocation(lat, lon, LOC_AUTO);
  webPrint("Location: %s, %s from IP lookup\n", String(lat, 4).c_str(), String(lon, 4).c_str());
}

static void fetchEt0() {
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(siteLat, 4) +
               "&longitude=" + String(siteLon, 4) +
               "&daily=et0_fao_evapotranspiration&past_days=1&forecast_days=0&timezone=auto";
  String body;
  const int code = httpGet(url, body);
  if (code != HTTP_CODE_OK) {
    webPrint("Weather: fetch failed (HTTP %d), retry in 1h\n", code);
    return;
  }

  // {daily:{time:[x], et0_fao_evapotranspiration:[x]}}. If this overflows, the
  // last array is silently dropped and ET0 always reads null.
  StaticJsonDocument<JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + 2 * JSON_ARRAY_SIZE(1)> filter;
  filter["daily"]["time"][0] = true;
  filter["daily"]["et0_fao_evapotranspiration"][0] = true;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    webPrint("Weather: bad response (json), retry in 1h\n");
    return;
  }
  const char *date = doc["daily"]["time"][0];
  if (!date) {  // a malformed date just yields a key that never matches yesterday
    webPrint("Weather: bad response (date), retry in 1h\n");
    return;
  }
  JsonVariant v = doc["daily"]["et0_fao_evapotranspiration"][0];
  if (v.isNull()) {  // ArduinoJson reads null as 0.0, which would clamp runs to the minimum
    webPrint("Weather: bad response (null), retry in 1h\n");
    return;
  }
  const float mm = v.as<float>();
  if (!isfinite(mm) || mm < 0.0f || mm > 20.0f) {
    webPrint("Weather: bad response (range), retry in 1h\n");
    return;
  }

  cachedKey = atol(date) * 10000 + atol(date + 5) * 100 + atol(date + 8);  // atol stops at '-'
  cachedMm = mm;
  // integer tenths: avoids relying on %f support in webPrint's vsnprintf
  webPrint("Weather: ET0 %d.%dmm for %s\n", (int)(mm * 10) / 10, (int)(mm * 10) % 10, date);
}

// Integer tenths for webPrint: avoids relying on %f support in its vsnprintf.
static String tenths(float v) {
  return String((int)(v * 10) / 10) + "." + String((int)(v * 10) % 10);
}

static bool rainSkipCheck() {
  const int tiers = sizeof(RAIN_SKIP) / sizeof(RAIN_SKIP[0]);
  // Hourly values are the preceding hour's total, so forecast_hours=2 adds the
  // hour in progress (stamped next hour). Newest last.
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(siteLat, 4) +
               "&longitude=" + String(siteLon, 4) +
               "&hourly=precipitation&past_hours=" + String(RAIN_SKIP[tiers - 1].hours) +
               "&forecast_hours=2&timezone=GMT";
  String body;
  const int code = httpGet(url, body);
  if (code != HTTP_CODE_OK) {
    webPrint("Rain check failed (HTTP %d), watering anyway\n", code);
    return false;
  }

  StaticJsonDocument<64> filter;
  filter["hourly"]["precipitation"] = true;
  // +32: the copied key strings "hourly" and "precipitation". Too small and
  // deserializeJson() fails with NoMemory.
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(1) +
                          JSON_ARRAY_SIZE(RAIN_SKIP[tiers - 1].hours + 2) + 32);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    webPrint("Rain check failed (json), watering anyway\n");
    return false;
  }
  JsonArray p = doc["hourly"]["precipitation"];
  if (p.isNull() || (int)p.size() < RAIN_SKIP[tiers - 1].hours) {
    webPrint("Rain check failed (short), watering anyway\n");
    return false;
  }

  // Walk back from the newest hour; check each tier when its window is summed.
  float sum = 0;
  bool skip = false;
  String totals;
  int t = 0;
  for (int h = 1; h <= RAIN_SKIP[tiers - 1].hours; h++) {
    sum += p[p.size() - h].as<float>();  // null reads as 0
    if (h == RAIN_SKIP[t].hours) {
      if (sum >= RAIN_SKIP[t].mm) skip = true;
      totals += (t ? ", " : "") + String(h) + "h " + tenths(sum) + "mm";
      t++;
    }
  }
  webPrint("Rain: %s -> %s\n", totals.c_str(), skip ? "skipping watering" : "watering");
  return skip;
}

// Called when a scheduled cycle is due. Fails open: no WiFi or a bad response
// means water as usual.
bool recentRainSkip() {
  if (WiFi.status() != WL_CONNECTED || ap_mode) return false;
#if defined(ESP32)
  disableLoopWDT();  // a slow DNS/connect can outlast the 5 s loop watchdog
  const bool skip = rainSkipCheck();
  enableLoopWDT();
  return skip;
#else
  return rainSkipCheck();  // HTTPClient yields, which feeds the ESP8266 watchdog
#endif
}

// Unsigned subtraction is millis()-rollover safe.
static bool etDue(time_t t) {
  if (haveYesterday(t)) return false;
  // After the boot attempt: wait until 01:00 (yesterday's value has settled),
  // then retry at most hourly.
  return !attempted || (hour(t) >= 1 && millis() - lastAttemptMs >= RETRY_MS);
}

static bool geoDue() {
  if (locSrc == LOC_MANUAL || geoDone) return false;
  return !geoAttempted || millis() - lastGeoMs >= RETRY_MS;
}

void updateWeather() {
  applyPendingLocation();
  if (WiFi.status() != WL_CONNECTED || ap_mode) return;
  const time_t t = now();
  if (year(t) < 2024) return;  // clock not NTP-synced yet
  if (!geoDue() && !etDue(t)) return;
  if (state.runCycle || anyValveOpen()) return;

#if defined(ESP32)
  // A slow DNS/connect can outlast the 5 s loop watchdog and reset the board.
  // Safe to suspend it here: no valve is open (checked above).
  disableLoopWDT();
#endif
  // HTTPClient yields, which feeds the ESP8266 watchdog.
  if (geoDue()) lookupLocation();  // first, so ET0 is fetched for the new location
  if (etDue(t)) {
    attempted = true;
    lastAttemptMs = millis();
    fetchEt0();
  }
#if defined(ESP32)
  enableLoopWDT();
#endif
}
