#include "web_server.h"
#include "web_assets.h"
#include "sprinky.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <TimeLib.h>
#if defined(ESP32)
#include <atomic>
#endif

#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

AsyncWebServer server(80);

extern bool valveIsOpen[];

// Active season profile (0=Summer, 1=Fall, 2=Winter, 3=Spring). Schedule
// settings are stored per season under "s<n>_" key prefixes; only one season's
// values are loaded into `state` at a time. Also read by relay.cpp to pick the
// season's ET reference day for temperature scaling.
uint8_t curSeason = 0;
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
  DynamicJsonDocument doc(4096);

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
  doc["freeHeap"] = ESP.getFreeHeap();
#if defined(ESP32)
  doc["maxBlock"] = ESP.getMaxAllocHeap();
#else
  doc["maxBlock"] = ESP.getMaxFreeBlockSize();  // largest contiguous allocation (fragmentation)
#endif
  doc["ip"] = WiFi.localIP().toString();
  doc["lastRunMinutes"] = state.lastRunMinutes;
  doc["disabled"] = state.wateringDisabled;
  doc["tempScaling"] = state.tempScaling;
  doc["etSource"] = etSource;
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

// --- Valve/timer commands from the web UI, carried out by loop() ---
// On ESP32 the request handlers run in the AsyncTCP task, concurrently with
// loop(). arduino-timer and the valve state aren't thread-safe, so a handler
// touching them could, e.g., have a just-armed valve shutoff wiped by
// loop()'s timer.tick(). Handlers therefore only post a command here and
// processWebCommands(), called from loop(), does the work: only loop() ever
// touches the timer or the valves. One slot, latest request wins -- an
// earlier, unprocessed request is superseded by the newer one, which is what
// the user asked for last. Reboot has its own flag so nothing overwrites it.
enum : int {
  CMD_NONE = 0,
  CMD_STOP,           // close all valves, end any cycle
  CMD_RUN,            // start a watering cycle now
  CMD_VALVE_ON = 16,  // + index: manual valve test
};
#if defined(ESP32)
template <typename T> using CmdSlot = std::atomic<T>;
#else
// ESP8266: ESPAsyncTCP callbacks run only while loop() yields, never in the
// middle of it, so a plain read-then-clear can't be interrupted. (Its
// toolchain also lacks the __atomic_exchange helpers std::atomic needs.)
template <typename T> struct CmdSlot {
  volatile T v;
  CmdSlot(T init) : v(init) {}
  void operator=(T x) { v = x; }
  T exchange(T x) { T old = v; v = x; return old; }
};
#endif
static CmdSlot<int> pendingCmd{ CMD_NONE };
static CmdSlot<bool> pendingReboot{ false };

void processWebCommands() {
  static unsigned long rebootAtMs = 0;
  if (pendingReboot.exchange(false)) {
    rebootAtMs = millis() + 500;  // let the HTTP response go out first
    if (rebootAtMs == 0) rebootAtMs = 1;
  }
  if (rebootAtMs != 0 && (long)(millis() - rebootAtMs) >= 0) {
    allOff();
    ESP.restart();
  }

  int cmd = pendingCmd.exchange(CMD_NONE);
  if (cmd == CMD_STOP) {
    shutOff(nullptr);  // also clears runCycle
  } else if (cmd == CMD_RUN) {
    startCycle();
  } else if (cmd >= CMD_VALVE_ON && cmd < CMD_VALVE_ON + NUM_RELAYS) {
    // Ends any schedule-driven cycle first (shutOff clears runCycle), so
    // controlRelays() stops sequencing and relayOn() arms the manual limit.
    shutOff(nullptr);
    relayOn(cmd - CMD_VALVE_ON);
    timer.in(60000, shutOff);
  }
}

static void handleValve(AsyncWebServerRequest *request, JsonVariant &json, int index) {
  if (index < 0 || index >= NUM_RELAYS) {
    request->send(400, "text/plain", "invalid valve index");
    return;
  }
  JsonObject body = json.as<JsonObject>();
  bool on = body["on"] | false;
  pendingCmd = on ? CMD_VALVE_ON + index : CMD_STOP;
  request->send(200, "text/plain", "ok");
}

static void handleWatering(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  bool disable = body["disable"] | false;
  state.wateringDisabled = disable;
  // Disabling must stop any watering already in progress, not just block future
  // cycles: controlRelays() returns early when state.wateringDisabled is set, so
  // without this an open valve would stay open until the cycle's safety timer.
  if (disable) {
    pendingCmd = CMD_STOP;
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
  pendingCmd = CMD_RUN;
  request->send(200, "text/plain", "ok");
}

static void handleStop(AsyncWebServerRequest *request) {
  pendingCmd = CMD_STOP;
  webPrint("Watering sequence cancelled\n");
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
  time_t t = currentLocalTime();
  if (t) setTime(t);  // 0 = no trustworthy NTP time; keep the current clock
  request->send(200, "text/plain", "ok");
}

static void handleReboot(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "rebooting");
  pendingReboot = true;  // processWebCommands() restarts after ~500ms
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

  state.tempScaling = preferences.getBool("tempScale", false);  // default off: set up baseline run times first

  for (int i = 0; i < NUM_RELAYS; i++) {
    char base[10];
    sprintf(base, "slide%d", i + 1);
    seasonKey(key, s, base);
    // Clamp like handleSchedule() does on save: a corrupted or legacy NVS
    // value (e.g. "-1" -> ~4.3e9 as unsigned) would otherwise open a valve
    // for days.
    long runtime = preferences.getString(key, "300").toInt();
    if (runtime < 0) runtime = 0;
    if (runtime > 3600) runtime = 3600;  // 60 min/valve cap
    state.runtime[i] = (unsigned long)runtime;
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

// Calendar (meteorological) season for a date, as a profile index:
// Jun-Aug Summer, Sep-Nov Fall, Dec-Feb Winter, Mar-May Spring. Flipped for
// the southern hemisphere (negative LATITUDE_DEG).
static uint8_t calendarSeason(time_t t) {
  static const uint8_t byMonth[12] = { 2, 2, 3, 3, 3, 0, 0, 0, 1, 1, 1, 2 };  // Jan..Dec
  uint8_t s = byMonth[month(t) - 1];
  return (LATITUDE_DEG < 0) ? (s + 2) % NUM_SEASONS : s;
}

// Switch the active season profile when the calendar crosses a season
// boundary (Mar/Jun/Sep/Dec 1). Only boundary crossings switch -- the last
// calendar season seen is persisted as "calSeason" -- so a manual pick from
// the dropdown holds until the next boundary. A device with no "calSeason"
// yet treats its first synced boot as a crossing. Called from loop().
void updateAutoSeason() {
  time_t t = now();
  if (year(t) < 2024) return;      // clock not NTP-synced yet
  if (state.runCycle) return;      // never swap run times mid-cycle; retry after

  uint8_t cal = calendarSeason(t);
  if (preferences.isKey("calSeason") && preferences.getUChar("calSeason", 0) == cal) return;

  preferences.putUChar("calSeason", cal);
  if (cal == curSeason) return;  // already on it (e.g. picked manually ahead of time)
  curSeason = cal;
  preferences.putUChar("curSeason", curSeason);
  loadSeason(curSeason);
  webPrint("Season auto-switched to %s: start %02d:%02d, days 0x%02X\n",
           seasonName(curSeason), state.runHour, state.runMinute, state.activeDays);
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
  server.on("/api/stop", HTTP_POST, handleStop);
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
