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

extern bool relayEnabled[];

static void sendJson(AsyncWebServerRequest *request, JsonDocument &doc) {
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  serializeJson(doc, *response);
  request->send(response);
}

static void handleStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(3072);

  char timeBuf[20];
  time_t t = now();
  sprintf(timeBuf, "%02d:%02d:%02d %02d/%02d", hour(t), minute(t), second(t), month(t), day(t));
  doc["time"] = timeBuf;
  doc["timezone"] = tzName();
  doc["timezoneCode"] = tzCode();
  doc["tempF"] = getTempF();
  doc["avgTempF"] = state.avg_temp;
  doc["rssi"] = WiFi.RSSI();
  doc["lastRunMinutes"] = state.lastRunMinutes;
  doc["disabled"] = state.disable;
  doc["runHour"] = state.runHour;
  doc["runMinute"] = state.runMinute;
  doc["ssid"] = stored_ssid;
  doc["apMode"] = ap_mode;

  fetchDebugText();
  doc["log"] = charBuf;

  JsonArray valves = doc.createNestedArray("valves");
  for (int i = 0; i < NUM_RELAYS; i++) {
    JsonObject v = valves.createNestedObject();
    char key[10];
    sprintf(key, "name%d", i + 1);
    char defVal[15];
    sprintf(defVal, "valve %d", i + 1);
    v["name"] = preferences.getString(key, defVal);
    v["runtime"] = state.runtime[i];
    v["on"] = relayEnabled[i];
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

  if (on) {
    shutOff((void *)0);
    state.runCycle = false;
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
  state.disable = disable;
  preferences.putBool("disable", disable);
  request->send(200, "text/plain", "ok");
}

static void handleRun(AsyncWebServerRequest *request) {
  allOff();
  timer.cancel();
  state.start_time_ms = millis();
  state.runCycle = true;
  request->send(200, "text/plain", "ok");
}

static void handleSchedule(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();

  state.runHour = body["hour"] | state.runHour;
  state.runMinute = body["minute"] | state.runMinute;
  preferences.putString("hour", String(state.runHour));
  preferences.putString("minute", String(state.runMinute));

  JsonArray valves = body["valves"].as<JsonArray>();
  for (int i = 0; i < NUM_RELAYS && i < (int)valves.size(); i++) {
    JsonObject v = valves[i].as<JsonObject>();
    const char *name = v["name"] | "";
    int runtime = v["runtime"] | state.runtime[i];

    state.runtime[i] = runtime;

    char nameKey[10];
    sprintf(nameKey, "name%d", i + 1);
    preferences.putString(nameKey, name);

    char slideKey[10];
    sprintf(slideKey, "slide%d", i + 1);
    preferences.putString(slideKey, String(runtime));
  }
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
  setTime(getNtpTime());
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

// Load persisted schedule (hour/minute/per-valve runtime) into state, mirrors handleSchedule()'s writes
static void loadSchedule() {
  state.runHour = preferences.getString("hour", "8").toInt();
  state.runMinute = preferences.getString("minute", "0").toInt();

  for (int i = 0; i < NUM_RELAYS; i++) {
    char slideKey[10];
    sprintf(slideKey, "slide%d", i + 1);
    state.runtime[i] = preferences.getString(slideKey, "300").toInt();
  }
}

void setUpWebServer() {
  loadSchedule();

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
  server.addHandler(jsonHandler("/api/schedule", handleSchedule));
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
