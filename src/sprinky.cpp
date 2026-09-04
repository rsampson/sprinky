/**
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an
 * Access Point wifi with the SSID HOSTNAME (defined below). You can then
 * connect and use the controls on the "Wifi Credentials" tab to store
 * credentials into the nvmem.
 *
 */

// Tested on ESP32 Dev module  and ESP12-F (esp8266), make sure these match your
// board, otherwise strange results may occur.


#include <Arduino.h>

#include "sprinky.h"


#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#define TEMP_PIN 21
#else  // esp8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#define TEMP_PIN D1
// #define RELAY8  // some esp8266 boards may have 8 relays
#endif

#include <Preferences.h>
Preferences preferences;

#include <arduino-timer.h>  // ver 3.0.1
TimerType timer =  timer_create_default();  // create a timer for auto shut down of valves

#include <ElegantOTA.h>
#include <TimeLib.h>

#include <CircularBuffer.hpp>         // version 1.4.0 https://github.com/rlogiacco/CircularBuffer
CircularBuffer<float, 24> dayBuffer;  // store 24 hour temp samples

#include "web_server.h"
// Shared prototypes live in sprinky.h (included above).


SprinklerState state = { .wateringDisabled = false,
                         .runCycle = false,
                         .tempScaling = true,
                         .runHour = 2,
                         .runMinute = 10,
                         .runtime = { 300, 300, 300, 300, 300, 300, 300, 300 },
                         .start_time_ms = 0,
                         .temp_adjust = 1000,
                         .cur_temp = 70.0f,
                         .avg_temp = 65.0f,
                         .lastRunMinutes = 0,
                         .activeDays = 0x7F };

char charBuf[bufferSize];
// temperature measuring stuff ********************************************

#ifdef DS18B20
// sensor libraries
#include <DallasTemperature.h>
#include <OneWire.h>
OneWire oneWire(TEMP_PIN);            // sensor hooked to TEMP_PIN
DallasTemperature sensors(&oneWire);  // version 4.0.3
#endif

int getTempF() {
  float tempF;
#ifdef DS18B20
  if (sensors.getDeviceCount() != 0) {
    sensors.requestTemperatures();  // Send the command to get temperatures
    tempF = float(sensors.getTempFByIndex(0));
  } else {
    tempF = 70;  // sensor failed, fake it
  }
#else
  int sensorValue = analogRead(A0);  // read diode voltage attached to A0 pin
  if (sensorValue < 100) {
    // no diode connected -- an unbiased floating pin reads near 0, well below
    // the 402-640 range a real diode produces across its calibrated 32-212F span
    tempF = 70;  // no sensor found, fake it
  } else {
    // map diode voltage to temperature F  ( diode mv values recorded from
    // freezing and boiling water)
    tempF = float(
      map(sensorValue, 640, 402, 32,
          212));  // 1n914 diode @ .44 ma (10k / 5v), Wemos mini devides by .3125
                  // tempF = map(sensorValue, 200, 126, 32, 212); // 1n914 diode @ .44 ma (10k /
                  // 5v)
  }
#endif
  return (tempF);
}

// temp stuff ***************************************************************

// Days array is now defined in time_manager.cpp

void getBootReasonMessage(char *buffer, int bufferlength) {
#if defined(ARDUINO_ARCH_ESP32)
  esp_reset_reason_t reset_reason = esp_reset_reason();

  switch (reset_reason) {
    case ESP_RST_UNKNOWN:
      snprintf(buffer, bufferlength, "Reset reason can not be determined");
      break;
    case ESP_RST_POWERON:
      snprintf(buffer, bufferlength, "Reset due to power-on event");
      break;
    case ESP_RST_SW:
      snprintf(buffer, bufferlength, "Software reset via esp_restart");
      break;
    case ESP_RST_PANIC:
      snprintf(buffer, bufferlength, "Software reset due to exception/panic");
      break;
    case ESP_RST_BROWNOUT:
      snprintf(buffer, bufferlength, "Brownout reset (software or hardware)");
      break;
    default:
      snprintf(buffer, bufferlength, "Unknown reset cause %d", reset_reason);
      break;
  }
#endif

#if defined(ARDUINO_ARCH_ESP8266)
  rst_info *resetInfo;
  resetInfo = ESP.getResetInfoPtr();

  switch (resetInfo->reason) {
    case REASON_DEFAULT_RST:
      snprintf(buffer, bufferlength, "Normal startup by power on");
      break;
    case REASON_WDT_RST:
      snprintf(buffer, bufferlength, "Hardware watch dog reset");
      break;
    case REASON_EXCEPTION_RST:
      snprintf(buffer, bufferlength, "Exception reset");
      break;
    case REASON_SOFT_WDT_RST:
      snprintf(buffer, bufferlength, "Software watch dog reset");
      break;
    case REASON_SOFT_RESTART:
      snprintf(buffer, bufferlength, "Software restart ,system_restart");
      break;
    case REASON_EXT_SYS_RST:
      snprintf(buffer, bufferlength, "External system reset");
      break;
    default:
      snprintf(buffer, bufferlength, "Unknown reset cause %d", resetInfo->reason);
      break;
  };
#endif
}

// Timezone rules, variables, and currentLocalTime() are now defined in
// time_manager.cpp

constexpr size_t BOOT_REASON_MESSAGE_SIZE = 150;
char bootReasonMessage[BOOT_REASON_MESSAGE_SIZE];


void setup() {

// #ifdef ERASE_FLASH
//   nvs_flash_erase();  // erase the NVS partition and...
//   nvs_flash_init();   // initialize the NVS partition.
// #endif

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  relayConfig();
  allOff();

  pinMode(LED_BUILTIN, OUTPUT);  // set heartbeat LED pin to OUTPUT
  digitalWrite(LED_BUILTIN, LOW);

  if (!preferences.begin("Settings")) {
    Serial.println("Failed to open preferences.");
    ESP.restart();
  }

  setupWiFi();

  timeClient.begin();  // set up ntp time client and then initialize time library
  timeClient.update();

  if (preferences.isKey(
        "timezone")) {  // initialize to UTC if TZ hasn't been set yet
    char tzstring[5];
    preferences.getString("timezone", tzstring,
                          5);  // set and store time zone selection
    Serial.println(tzstring);
    tz = TZstringToPointer(String(tzstring));
  } else {
    preferences.putString("timezone", "UTC");
    Serial.println("Initialize Time Zone to UTC");
  }
  setTime(currentLocalTime());
  setSyncProvider(currentLocalTime);
  setSyncInterval(300);  // sync time server every 5 minutes

#ifdef DS18B20  // temp sensor
  sensors.begin();
  if (sensors.getDeviceCount() != 0) {
    Serial.println("temp sensor configured");
  } else {
    Serial.println("!!temp sensor configuration failed!!");
  }
#endif
  dayBuffer.clear();

  Serial.println("configuring web server");
  setUpWebServer();

  ElegantOTA.begin(&server);

  printTZ();

  // Default false: a missing key must mean "watering enabled", not disabled.
  // (The old "0" literal was a const char* -> non-null -> true.)
  state.wateringDisabled = preferences.getBool("disable", false);

  //  boot up message
  char buf1[20];
  time_t t = now();
  sprintf(buf1, "%02d:%02d:%02d %02d/%02d", hour(t), minute(t), second(t),
          month(t), day(t));
  webPrint("%s up at: %s on %s\n", HOSTNAME, buf1,  Days[weekday()]);
  getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  webPrint("Reset reason: %s\n", bootReasonMessage);

  Serial.println("We Are Go!");
}

unsigned long lastHousekeepingMs = 0;

void loop() {

  handleWiFi();
  // Only poll NTP when actually connected: forceUpdate() blocks ~1s on a UDP
  // timeout otherwise, which would stall the loop (and the watering trigger
  // window) for the whole duration of a WiFi outage. TimeLib's now() keeps
  // free-running off millis() in the meantime, so timekeeping still advances.
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();  // run ntp time client
  }
  controlRelays();      // activate relay if correct time
  ElegantOTA.loop();

  if (millis() - lastHousekeepingMs >= 1000) {  // once per second housekeeping (rollover-safe)
    timer.tick();                        // tick the timer (to shut down valve tests after two minutes)
    state.cur_temp = getTempF();         // sample sensor here (loop ctx); web handlers read the cache
    updateHourlyTempAverage();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  // toggle the LED
    lastHousekeepingMs = millis();
  }
#if !defined(ESP32)
  // We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}
