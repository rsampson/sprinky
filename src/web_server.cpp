#include "web_server.h"
#include "web_assets.h"
#include "sprinky.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <TimeLib.h>

#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

AsyncWebServer server(80);

extern bool valveIsOpen[];

// Active season profile (0=Summer, 1=Fall, 2=Winter, 3=Spring). Schedule
// settings are stored per season under "s<n>_" key prefixes; only one season's
// values are loaded into `state` at a time.
static uint8_t curSeason = 0;
static const uint8_t NUM_SEASONS = 4;

// Build a season-scoped Preferences key, e.g. seasonKey(buf, 2, "slide3") -> "s2_slide3".
// NVS keys are capped at 15 chars on ESP32; the longest here is "s0_activeDays" (13).
static void seasonKey(char *out, uint8_t s, const char *base) {
  sprintf(out, "s%u_%s", s, base);
}

static const char *seasonName(uint8_t s) {
  static const char *names[NUM_SEASONS] = { "Summer", "Fall", "Winter", "Spring" };
  return (s < NUM_SEASONS) ? names[s] : "?";
}

// Load one season's persisted schedule (hour/minute/days/per-valve runtime) into
// state; mirrors handleSchedule()'s per-season writes. Declared here so both
// handleSchedule() and handleSeason() can call it (defined further below).
static void loadSeason(uint8_t s);

static void sendJson(AsyncWebServerRequest *request, JsonDocument &doc) {
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  serializeJson(doc, *response);
  request->send(response);
}

static void handleStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(3072);

  char timeBuf[10];
  char dateBuf[7];
  time_t t = now();
  sprintf(timeBuf, "%02d:%02d:%02d", hour(t), minute(t), second(t));
  sprintf(dateBuf, "%02d/%02d", month(t), day(t));
  doc["hostname"] = HOSTNAME;
  doc["time"] = timeBuf;
  doc["date"] = dateBuf;
  doc["timezone"] = tzName();
  doc["timezoneCode"] = tzCode();
  doc["tempF"] = (int)state.cur_temp;
  doc["avgTempF"] = state.avg_temp;
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["lastRunMinutes"] = state.lastRunMinutes;
  doc["disabled"] = state.wateringDisabled;
  doc["tempScaling"] = state.tempScaling;
  doc["runHour"] = state.runHour;
  doc["runMinute"] = state.runMinute;
  doc["activeDays"] = state.activeDays;
  doc["season"] = curSeason;
  doc["ssid"] = stored_ssid;
  doc["apMode"] = ap_mode;

  fetchDebugText();
  doc["log"] = charBuf;

  JsonArray valves = doc.createNestedArray("valves");
  for (int i = 0; i < NUM_RELAYS; i++) {
    JsonObject v = valves.createNestedObject();
    char base[10];
    sprintf(base, "name%d", i + 1);
    char key[14];
    seasonKey(key, curSeason, base);
    char defVal[15];
    sprintf(defVal, "valve %d", i + 1);
    v["name"] = preferences.getString(key, defVal);
    v["runtime"] = state.runtime[i];
    v["on"] = valveIsOpen[i];
  }

  sendJson(request, doc);
}

static void handleValve(AsyncWebServerRequest *request, JsonVariant &json, int index) {
  if (index < 0 || index >= NUM_RELAYS) {
    request->send(400, "text/plain", "invalid valve index");
    return;
  }
  JsonObject body = json.as<JsonObject>();
  bool on = body["on"] | false;

  // Either branch ends any schedule-driven cycle: clear runCycle so
  // controlRelays() stops sequencing (and doesn't re-open a valve with no
  // safety timer armed, since shutOff() cancelled it).
  state.runCycle = false;
  if (on) {
    shutOff((void *)0);
    relayOn(index);
    timer.in(60000, shutOff);
  } else {
    shutOff((void *)0);
  }
  request->send(200, "text/plain", "ok");
}

static void handleWatering(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  bool disable = body["disable"] | false;
  state.wateringDisabled = disable;
  // Disabling must stop any watering already in progress, not just block future
  // cycles: controlRelays() returns early when state.wateringDisabled is set, so
  // without this an open valve would stay open until the 80-min safety timer.
  if (disable) {
    shutOff((void *)0);
    state.runCycle = false;
  }
  preferences.putBool("disable", disable);
  request->send(200, "text/plain", "ok");
}

static void handleTempScaling(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  state.tempScaling = body["enabled"] | false;
  preferences.putBool("tempScale", state.tempScaling);
  webPrint("Temperature scaling turned %s\n", state.tempScaling ? "ON" : "OFF");
  request->send(200, "text/plain", "ok");
}

static void handleRun(AsyncWebServerRequest *request) {
  startCycle();
  request->send(200, "text/plain", "ok");
}

static void handleSchedule(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();

  int hour = body["hour"] | state.runHour;
  int minute = body["minute"] | state.runMinute;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    request->send(400, "text/plain", "invalid hour/minute");
    return;
  }

  // A Save may also switch which season these settings belong to. Load that
  // season's own saved schedule first, so any field this request doesn't
  // specify defaults to its saved value -- not whatever was left in `state`
  // from the previously active season, which would otherwise silently
  // overwrite the target season's profile with stale data.
  uint8_t season = body["season"] | curSeason;
  if (season < NUM_SEASONS && season != curSeason) {
    curSeason = season;
    preferences.putUChar("curSeason", curSeason);
    loadSeason(curSeason);
    hour = body["hour"] | state.runHour;
    minute = body["minute"] | state.runMinute;
  }

  uint8_t prevHour = state.runHour;
  uint8_t prevMinute = state.runMinute;
  state.runHour = (uint8_t)hour;
  state.runMinute = (uint8_t)minute;
  state.activeDays = body["activeDays"] | state.activeDays;
  // Only clear the once-per-day auto-run latch if the run time itself
  // changed -- saving for an unrelated reason (renaming a valve, toggling a
  // day) shouldn't risk re-firing a cycle that already ran today.
  if (state.runHour != prevHour || state.runMinute != prevMinute) {
    resetAutoRunLatch();
  }

  char key[14];
  seasonKey(key, curSeason, "hour");
  preferences.putString(key, String(state.runHour));
  seasonKey(key, curSeason, "minute");
  preferences.putString(key, String(state.runMinute));
  seasonKey(key, curSeason, "activeDays");
  preferences.putUChar(key, state.activeDays);

  JsonArray valves = body["valves"].as<JsonArray>();
  for (int i = 0; i < NUM_RELAYS && i < (int)valves.size(); i++) {
    JsonObject v = valves[i].as<JsonObject>();
    const char *name = v["name"] | "";
    int runtime = v["runtime"] | (int)state.runtime[i];
    // Clamp to a sane range: an unvalidated negative value would wrap to a
    // huge one when stored into the unsigned runtime field, letting a valve
    // stay open far longer than intended (see relay.cpp's temp_adjust math).
    if (runtime < 0) runtime = 0;
    if (runtime > 3600) runtime = 3600;  // 60 min/valve cap

    state.runtime[i] = (unsigned long)runtime;

    char base[10];
    sprintf(base, "name%d", i + 1);
    seasonKey(key, curSeason, base);
    preferences.putString(key, name);

    sprintf(base, "slide%d", i + 1);
    seasonKey(key, curSeason, base);
    preferences.putString(key, String(runtime));
  }
  webPrint("Saved %s schedule: start %02d:%02d, days 0x%02X\n",
           seasonName(curSeason), state.runHour, state.runMinute, state.activeDays);
  request->send(200, "text/plain", "ok");
}

// Switch the active season profile and load its stored schedule into `state`,
// so controlRelays() starts running the newly selected season immediately.
static void handleSeason(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  int season = body["season"] | -1;
  if (season < 0 || season >= NUM_SEASONS) {
    request->send(400, "text/plain", "invalid season");
    return;
  }
  curSeason = (uint8_t)season;
  preferences.putUChar("curSeason", curSeason);
  loadSeason(curSeason);
  webPrint("Loaded %s schedule: start %02d:%02d, days 0x%02X\n",
           seasonName(curSeason), state.runHour, state.runMinute, state.activeDays);
  request->send(200, "text/plain", "ok");
}

static void handleWifi(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  stored_ssid = body["ssid"] | stored_ssid;
  stored_pass = body["pass"] | stored_pass;
  preferences.putString("ssid", stored_ssid);
  preferences.putString("pass", stored_pass);
  request->send(200, "text/plain", "ok");
}

static void handleTimezone(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  String tzstring = body["tz"] | "UTC";
  tz = TZstringToPointer(tzstring);
  preferences.putString("timezone", tzstring);
  printTZ();
  setTime(currentLocalTime());
  request->send(200, "text/plain", "ok");
}

static void handleReboot(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "rebooting");
  timer.in(500, [](void *) -> bool {
    ESP.restart();
    return false;
  });
}

static AsyncCallbackJsonWebHandler *jsonHandler(const char *uri, ArJsonRequestHandlerFunction fn) {
  return new AsyncCallbackJsonWebHandler(uri, fn);
}

// Load one season's persisted schedule (hour/minute/days/per-valve runtime) into
// state; mirrors handleSchedule()'s per-season writes. tempScaling is a global
// sensor setting, not per-season, so it stays under its own key.
static void loadSeason(uint8_t s) {
  char key[14];
  seasonKey(key, s, "hour");
  state.runHour = preferences.getString(key, "8").toInt();
  seasonKey(key, s, "minute");
  state.runMinute = preferences.getString(key, "0").toInt();
  seasonKey(key, s, "activeDays");
  state.activeDays = preferences.getUChar(key, 0x7F);

  state.tempScaling = preferences.getBool("tempScale", true);

  for (int i = 0; i < NUM_RELAYS; i++) {
    char base[10];
    sprintf(base, "slide%d", i + 1);
    seasonKey(key, s, base);
    state.runtime[i] = preferences.getString(key, "300").toInt();
  }
}

// One-time migration: if no season profiles exist yet, seed Summer (season 0)
// from the pre-seasonal flat keys so a deployed device keeps its schedule.
static void seedSeasonFromLegacy() {
  if (preferences.isKey("curSeason") || preferences.isKey("s0_hour")) return;
  if (!preferences.isKey("hour")) return;  // nothing to migrate (fresh device)

  char key[14];
  seasonKey(key, 0, "hour");
  preferences.putString(key, preferences.getString("hour", "8"));
  seasonKey(key, 0, "minute");
  preferences.putString(key, preferences.getString("minute", "0"));
  seasonKey(key, 0, "activeDays");
  preferences.putUChar(key, preferences.getUChar("activeDays", 0x7F));
  // Clear the legacy keys once migrated so they don't sit in NVS unread
  // forever -- this is a one-time bridge, not a permanent fixture.
  preferences.remove("hour");
  preferences.remove("minute");
  preferences.remove("activeDays");

  for (int i = 0; i < NUM_RELAYS; i++) {
    char base[10];
    char legacy[10];

    sprintf(base, "name%d", i + 1);
    sprintf(legacy, "name%d", i + 1);
    if (preferences.isKey(legacy)) {
      seasonKey(key, 0, base);
      preferences.putString(key, preferences.getString(legacy, ""));
      preferences.remove(legacy);
    }

    sprintf(base, "slide%d", i + 1);
    sprintf(legacy, "slide%d", i + 1);
    if (preferences.isKey(legacy)) {
      seasonKey(key, 0, base);
      preferences.putString(key, preferences.getString(legacy, "300"));
      preferences.remove(legacy);
    }
  }
}

void setUpWebServer() {
  curSeason = preferences.getUChar("curSeason", 0);
  if (curSeason >= NUM_SEASONS) curSeason = 0;
  seedSeasonFromLegacy();
  loadSeason(curSeason);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/css", STYLE_CSS);
  });
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "application/javascript", APP_JS);
  });

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/run", HTTP_POST, handleRun);
  server.on("/api/reboot", HTTP_POST, handleReboot);

  server.addHandler(jsonHandler("/api/watering", handleWatering));
  server.addHandler(jsonHandler("/api/tempscaling", handleTempScaling));
  server.addHandler(jsonHandler("/api/schedule", handleSchedule));
  server.addHandler(jsonHandler("/api/season", handleSeason));
  server.addHandler(jsonHandler("/api/wifi", handleWifi));
  server.addHandler(jsonHandler("/api/timezone", handleTimezone));

  for (int i = 0; i < NUM_RELAYS; i++) {
    char uri[16];
    sprintf(uri, "/api/valve/%d", i);
    // captured by value into the lambda, one handler per valve index
    server.addHandler(new AsyncCallbackJsonWebHandler(uri, [i](AsyncWebServerRequest *request, JsonVariant &json) {
      handleValve(request, json, i);
    }));
  }

  server.begin();
}
