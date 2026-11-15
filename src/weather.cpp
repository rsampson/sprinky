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

// Open-Meteo current air temperature, getTempF()'s fallback when no sensor
// works. Fetched only while that fallback is being asked for.
static float cachedTempF = 0;
static unsigned long tempFetchedMs = 0;
static bool haveTemp = false;
static unsigned long tempAttemptMs = 0;
static bool tempAttempted = false;
static unsigned long tempWantedMs = 0;
static bool tempWanted = false;
static const unsigned long TEMP_REFRESH_MS = 900000UL;  // Open-Meteo updates "current" every 15 min
static const unsigned long TEMP_MAX_AGE_MS = 3600000UL;

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

// --- Rain skip ---
// Decided once, when a scheduled run is due, from one Open-Meteo request:
//   1. recent rain: any RAIN_SKIP tier met (the long-standing rule);
//   2. rain credit: rain banked over the last RAIN_CREDIT_DAYS, less the ET0
//      that has used it up since, still covers at least half a day;
//   3. forecast: enough likely rain in the next RAIN_FORECAST_HOURS.
// Nothing is stored between runs except "the last run was skipped on a
// forecast" (fcSkip), so a forecast that fails can't skip twice running.
static const int RAIN_CREDIT_DAYS = 7;
static const int RAIN_FORECAST_HOURS = 24;
static const float RAIN_CREDIT_SKIP_FRACTION = 0.5f;  // of yesterday's ET0
static const int RAIN_FORECAST_MAX_GAP_DAYS = 2;      // no forecast skip if the next run is further off

static float rainCreditMm = -1;  // as of the last check; <0 until one succeeds
static char lastSkip[32] = "";   // "MM/DD reason" of the last skipped run, RAM only

float weatherRainCredit() {
  return rainCreditMm;
}

const char *lastSkipReason() {
  return lastSkip;
}

void noteSkip(const char *why) {
  const time_t t = now();
  snprintf(lastSkip, sizeof(lastSkip), "%02d/%02d %s", month(t), day(t), why);
}

// Days until the next active day after today (1-7).
static int daysToNextRun() {
  for (int k = 1; k <= 7; k++) {
    if (state.activeDays & (1 << ((weekday() - 1 + k) % 7))) return k;
  }
  return 7;
}

// Likely rain in the forecast hours: the amounts in `fc`, counting only hours
// whose probability is at least RAIN_FORECAST_MIN_PROB. Parsed from `body` in
// a second pass so the two big hourly arrays are never in memory together.
static bool forecastRain(const String &body, const float *fc, int firstForecast, int hours, float &mm) {
  StaticJsonDocument<64> filter;
  filter["hourly"]["precipitation_probability"] = true;
  // +48: the copied key strings.
  DynamicJsonDocument doc(2 * JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(hours) + 48);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  mm = 0;
  int i = 0;
  for (JsonVariant v : doc["hourly"]["precipitation_probability"].as<JsonArray>()) {
    if (i >= firstForecast && i < hours && v.as<int>() >= RAIN_FORECAST_MIN_PROB) mm += fc[i - firstForecast];
    i++;
  }
  return i == hours;
}

// True = skip this run. byForecast is set when the forecast was the reason.
static bool rainSkipCheck(bool &byForecast) {
  const int tiers = sizeof(RAIN_SKIP) / sizeof(RAIN_SKIP[0]);
  const int pastH = RAIN_SKIP[tiers - 1].hours;
  // Hourly values are the preceding hour's total. Index pastH is stamped with
  // the current hour, pastH + 1 is the hour in progress, the forecast follows.
  const int newest = pastH + 1;
  const int hours = pastH + 2 + RAIN_FORECAST_HOURS;
  const int days = RAIN_CREDIT_DAYS + 1;  // + today, unused: it mixes fallen and forecast rain
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(siteLat, 4) +
               "&longitude=" + String(siteLon, 4) +
               "&daily=precipitation_sum,et0_fao_evapotranspiration&past_days=" + String(RAIN_CREDIT_DAYS) +
               "&forecast_days=1&hourly=precipitation,precipitation_probability&past_hours=" + String(pastH) +
               "&forecast_hours=" + String(RAIN_FORECAST_HOURS + 2) + "&timezone=auto&timeformat=unixtime";
  String body;
  const int code = httpGet(url, body);
  if (code != HTTP_CODE_OK) {
    webPrint("Rain check failed (HTTP %d), watering anyway\n", code);
    return false;
  }

  StaticJsonDocument<3 * JSON_OBJECT_SIZE(2)> filter;
  filter["hourly"]["precipitation"] = true;
  filter["daily"]["precipitation_sum"] = true;
  filter["daily"]["et0_fao_evapotranspiration"] = true;
  // +96: the copied key strings. Too small and deserializeJson() fails with
  // NoMemory.
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) +
                          JSON_ARRAY_SIZE(hours) + 2 * JSON_ARRAY_SIZE(days) + 96);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    webPrint("Rain check failed (json), watering anyway\n");
    return false;
  }

  // Hourly: tier sums back from the newest hour, rain so far today, forecast.
  float tierSum[sizeof(RAIN_SKIP) / sizeof(RAIN_SKIP[0])] = {};
  float fc[RAIN_FORECAST_HOURS] = {};
  float today = 0;
  const int hourNow = hour();
  int n = 0;
  for (JsonVariant v : doc["hourly"]["precipitation"].as<JsonArray>()) {
    const float mm = v.as<float>();  // null reads as 0
    if (n <= newest) {
      const int back = newest - n;  // 0 = the hour in progress
      for (int t = 0; t < tiers; t++) {
        if (back < RAIN_SKIP[t].hours) tierSum[t] += mm;
      }
      if (back <= hourNow) today += mm;
    } else if (n < hours) {
      fc[n - newest - 1] = mm;
    }
    n++;
  }

  // Daily: the last RAIN_CREDIT_DAYS whole days, oldest first.
  float rain[RAIN_CREDIT_DAYS], et[RAIN_CREDIT_DAYS];
  int nr = 0, ne = 0;
  bool etOk = true;
  for (JsonVariant v : doc["daily"]["precipitation_sum"].as<JsonArray>()) {
    if (nr < RAIN_CREDIT_DAYS) rain[nr] = v.as<float>();
    nr++;
  }
  for (JsonVariant v : doc["daily"]["et0_fao_evapotranspiration"].as<JsonArray>()) {
    if (ne < RAIN_CREDIT_DAYS) {
      et[ne] = v.as<float>();
      // A missing ET0 reads as 0 and would leave the credit undrained.
      if (v.isNull() || !isfinite(et[ne]) || et[ne] < 0.0f || et[ne] > 20.0f) etOk = false;
    }
    ne++;
  }
  if (n != hours || nr != days || ne != days || !etOk) {
    webPrint("Rain check failed (short), watering anyway\n");
    return false;
  }

  // Rain credit: each day banks its rain beyond what only wets the surface,
  // up to what the soil holds, then that day's ET0 uses some up.
  float credit = 0;
  for (int d = 0; d < RAIN_CREDIT_DAYS; d++) {
    credit = min(RAIN_CREDIT_MAX_MM, credit + max(0.0f, rain[d] - RAIN_INTERCEPT_MM));
    credit = max(0.0f, credit - et[d]);
  }
  credit = min(RAIN_CREDIT_MAX_MM, credit + max(0.0f, today - RAIN_INTERCEPT_MM));
  rainCreditMm = credit;
  const float need = RAIN_CREDIT_SKIP_FRACTION * et[RAIN_CREDIT_DAYS - 1];

  String totals;
  for (int t = 0; t < tiers; t++) {
    totals += (t ? ", " : "") + String(RAIN_SKIP[t].hours) + "h " + tenths(tierSum[t]) + "mm";
  }
  webPrint("Rain fallen: %s. Still banked in soil: %smm (skip at %smm = half of yesterday's ET0)\n",
           totals.c_str(), tenths(credit).c_str(), tenths(need).c_str());

  for (int t = 0; t < tiers; t++) {
    if (tierSum[t] >= RAIN_SKIP[t].mm) {
      webPrint("Skipping watering: %smm of rain in the last %dh (limit %smm)\n", tenths(tierSum[t]).c_str(),
               RAIN_SKIP[t].hours, tenths(RAIN_SKIP[t].mm).c_str());
      noteSkip("recent rain");
      return true;
    }
  }
  if (credit > 0.0f && credit >= need) {
    webPrint("Skipping watering: earlier rain still covers today's need\n");
    noteSkip("rain still in soil");
    return true;
  }

  float likely = 0;
  if (!forecastRain(body, fc, newest + 1, hours, likely)) {
    webPrint("Watering: not enough recent rain, and the forecast couldn't be read\n");
    return false;
  }
  const String fcText = tenths(likely) + "mm of rain is at least " + String(RAIN_FORECAST_MIN_PROB) +
                        "% likely in the next " + String(RAIN_FORECAST_HOURS) + "h";
  if (likely < RAIN_FORECAST_MM) {
    webPrint("Watering: not enough recent rain, and only %s (skip at %smm)\n", fcText.c_str(),
             tenths(RAIN_FORECAST_MM).c_str());
    return false;
  }
  if (preferences.getBool("fcSkip", false)) {
    webPrint("Watering: %s, but the last run was already skipped on a forecast\n", fcText.c_str());
    return false;
  }
  const int gap = daysToNextRun();
  if (gap > RAIN_FORECAST_MAX_GAP_DAYS) {
    webPrint("Watering: %s, but the next run is %d days away\n", fcText.c_str(), gap);
    return false;
  }
  webPrint("Skipping watering: %s\n", fcText.c_str());
  noteSkip("rain forecast");
  byForecast = true;
  return true;
}

// Called when a scheduled cycle is due. Fails open: no WiFi or a bad response
// means water as usual.
bool recentRainSkip() {
  bool byForecast = false;
  bool skip = false;
  if (WiFi.status() != WL_CONNECTED || ap_mode) {
    webPrint("Rain check: no WiFi, watering anyway\n");
  } else {
#if defined(ESP32)
    disableLoopWDT();  // a slow DNS/connect can outlast the 5 s loop watchdog
    skip = rainSkipCheck(byForecast);
    enableLoopWDT();
#else
    skip = rainSkipCheck(byForecast);  // HTTPClient yields, which feeds the ESP8266 watchdog
#endif
  }
  // Flash is written only when this changes.
  if (preferences.getBool("fcSkip", false) != byForecast) preferences.putBool("fcSkip", byForecast);
  return skip;
}

bool weatherTempF(float &f) {
  tempWanted = true;
  tempWantedMs = millis();
  if (!haveTemp || millis() - tempFetchedMs > TEMP_MAX_AGE_MS) return false;
  f = cachedTempF;
  return true;
}

static void fetchCurrentTemp() {
  tempAttempted = true;
  tempAttemptMs = millis();
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(siteLat, 4) +
               "&longitude=" + String(siteLon, 4) +
               "&current=temperature_2m&temperature_unit=fahrenheit";
  String body;
  const int code = httpGet(url, body);
  if (code != HTTP_CODE_OK) {
    webPrint("Weather: temp fetch failed (HTTP %d)\n", code);
    return;
  }
  StaticJsonDocument<64> filter;
  filter["current"]["temperature_2m"] = true;
  StaticJsonDocument<96> doc;  // two objects + the copied key strings
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) ||
      !doc["current"]["temperature_2m"].is<float>()) {
    webPrint("Weather: temp fetch failed (json)\n");
    return;
  }
  const float f = doc["current"]["temperature_2m"];
  if (!isfinite(f) || f < -40.0f || f > 150.0f) {
    webPrint("Weather: temp fetch failed (range)\n");
    return;
  }
  cachedTempF = f;
  tempFetchedMs = millis();
  haveTemp = true;
}

// Unsigned subtraction is millis()-rollover safe.
static bool tempDue() {
  if (!tempWanted || millis() - tempWantedMs > 60000UL) return false;  // a sensor works again
  return !tempAttempted || millis() - tempAttemptMs >= TEMP_REFRESH_MS;
}

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
  const bool synced = year(t) >= 2024;  // ET0 needs the date; the rest doesn't
  if (!tempDue() && !geoDue() && !(synced && etDue(t))) return;
  if (state.runCycle || anyValveOpen()) return;

#if defined(ESP32)
  // A slow DNS/connect can outlast the 5 s loop watchdog and reset the board.
  // Safe to suspend it here: no valve is open (checked above).
  disableLoopWDT();
#endif
  // HTTPClient yields, which feeds the ESP8266 watchdog.
  if (geoDue()) lookupLocation();  // first, so weather is fetched for the new location
  if (tempDue()) fetchCurrentTemp();
  if (synced && etDue(t)) {
    attempted = true;
    lastAttemptMs = millis();
    fetchEt0();
  }
#if defined(ESP32)
  enableLoopWDT();
#endif
}
