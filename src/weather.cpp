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

extern bool valveIsOpen[NUM_RELAYS];  // relay.cpp

// Yesterday's ET0 as last fetched, keyed by its local calendar day (YYYYMMDD).
// RAM only: a reboot just fetches it again.
static float cachedMm = 0;
static long cachedKey = -1;
static unsigned long lastAttemptMs = 0;
static bool attempted = false;  // first attempt after boot ignores the 01:00 / 1 h gates

static const unsigned long RETRY_MS = 3600000UL;

static long dayKey(time_t t) {
  return (long)year(t) * 10000 + month(t) * 100 + day(t);
}

bool weatherEt0(float &mm) {
  if (cachedKey != dayKey(now() - SECS_PER_DAY)) return false;
  mm = cachedMm;
  return true;
}

// Blocking: DNS (up to ~10 s on ESP8266) + connect + 4 s read. Only called when
// no valve is open, so valve timing is unaffected; a cycle due meanwhile still
// starts within the 5-minute catch-up window.
static void fetchEt0() {
  const uint32_t heapBefore = ESP.getFreeHeap();
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE_DEG, 4) +
               "&longitude=" + String(LONGITUDE_DEG, 4) +
               "&daily=et0_fao_evapotranspiration&past_days=1&forecast_days=0&timezone=auto";
  String body;
  {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(4000);
#if defined(ESP32)
    http.setConnectTimeout(3000);  // default 5000 ms
#endif
    if (!http.begin(client, url)) {
      webPrint("Weather: fetch failed (begin), retry in 1h\n");
      return;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
      webPrint("Weather: fetch failed (HTTP %d), retry in 1h\n", code);
      http.end();
      return;
    }
    body = http.getString();  // de-chunks the response
    http.end();
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
  if (!date || strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
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
  webPrint("Weather: ET0 %d.%dmm for %s (heap %u->%u)\n", (int)(mm * 10) / 10, (int)(mm * 10) % 10, date,
           (unsigned)heapBefore, (unsigned)ESP.getFreeHeap());
}

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED || ap_mode) return;
  const time_t t = now();
  if (year(t) < 2024) return;  // clock not NTP-synced yet
  if (state.runCycle) return;
  for (int i = 0; i < NUM_RELAYS; i++)
    if (valveIsOpen[i]) return;
  if (cachedKey == dayKey(t - SECS_PER_DAY)) return;  // already have yesterday's
  // After the boot attempt: wait until 01:00 (yesterday's value has settled),
  // then retry at most hourly. Unsigned subtraction is millis()-rollover safe.
  if (attempted && (hour(t) < 1 || millis() - lastAttemptMs < RETRY_MS)) return;

  attempted = true;
  lastAttemptMs = millis();
#if defined(ESP32)
  // A slow DNS/connect can outlast the 5 s loop watchdog and reset the board.
  // Safe to suspend it here: no valve is open (checked above).
  disableLoopWDT();
  fetchEt0();
  enableLoopWDT();
#else
  fetchEt0();  // HTTPClient yields, which feeds the ESP8266 watchdog
#endif
}
