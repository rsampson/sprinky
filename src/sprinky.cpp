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
#include "weather.h"


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
                         .tempScaling = false,  // off until the user enables it (see README)
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
static bool dsPresent = false;        // DS18B20 found at boot
static DeviceAddress dsAddr;

// Non-blocking: return the conversion started on the previous call (1s ago;
// a 9-bit conversion takes ~94ms), then start the next one. False if the
// reading is implausible -- -196.6F is the library's "disconnected" value and
// 185F (85C) is the power-on-reset value a glitching sensor returns.
static bool readDs18b20(float &tempF) {
  if (!dsPresent) return false;
  tempF = sensors.getTempF(dsAddr);
  sensors.requestTemperatures();
  return isfinite(tempF) && tempF > -40.0f && tempF < 150.0f && tempF != 185.0f;
}
#endif

// Average of a few A0 samples in millivolts, or -1 if they disagree by more
// than 15mV -- a floating (unconnected) ADC pin wanders, a biased diode doesn't.
static float readDiodeMilliVolts() {
  float lo = 1e9f, hi = -1e9f, sum = 0;
  for (int i = 0; i < 4; i++) {
#if defined(ESP32)
    float mv = (float)analogReadMilliVolts(A0);
#else
    float mv = analogRead(A0) * ESP8266_A0_FULL_SCALE_MV / 1023.0f;
#endif
    if (mv < lo) lo = mv;
    if (mv > hi) hi = mv;
    sum += mv;
  }
  return (hi - lo > 15.0f) ? -1.0f : sum / 4;
}

// Silicon diode on A0, linear between the ice/boiling-water calibration
// points. False if the voltage isn't that of a forward-biased diode.
static bool readDiode(float &tempF) {
  float mv = readDiodeMilliVolts();
  if (mv < DIODE_MV_MIN || mv > DIODE_MV_MAX) return false;
  tempF = 32.0f + (mv - DIODE_MV_AT_32F) * (212.0f - 32.0f) / (DIODE_MV_AT_212F - DIODE_MV_AT_32F);
  return true;
}

bool tempFromWeather = false;

// Outside temperature, trying each source in turn on every call: DS18B20,
// then the A0 diode, then Open-Meteo's current air temperature (display only:
// Hargreaves scaling ignores it, see tempFromWeather), then a fixed 70F.
// Always returns a sane value, so a failed or missing sensor can't disturb
// the watering cycle. Logs to the Status page whenever the source changes.
int getTempF() {
  static const char *lastSource = nullptr;
  const char *source;
  float tempF;
#ifdef DS18B20
  if (readDs18b20(tempF)) source = "DS18B20";
  else
#endif
  if (readDiode(tempF)) source = "diode on A0";
  else if (weatherTempF(tempF)) source = "Open-Meteo";
  else {
    tempF = 70.0f;
    source = "none (fixed 70F)";
  }
  tempFromWeather = (strcmp(source, "Open-Meteo") == 0);
  if (source != lastSource) {
    webPrint("Temp source: %s\n", source);
    lastSource = source;
  }
  return (int)tempF;
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
  // Where it crashed: decode epc1 against this build's firmware.elf, e.g.
  // xtensa-lx106-elf-addr2line -pfiaC -e .pio/build/esp12e/firmware.elf <epc1>
  if (resetInfo->reason == REASON_EXCEPTION_RST || resetInfo->reason == REASON_WDT_RST ||
      resetInfo->reason == REASON_SOFT_WDT_RST) {
    size_t n = strlen(buffer);
    snprintf(buffer + n, bufferlength - n, " (exccause %u, epc1 0x%08x, excvaddr 0x%08x)",
             resetInfo->exccause, resetInfo->epc1, resetInfo->excvaddr);
  }
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
  loadSiteLocation();

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
  if (sensors.getDeviceCount() != 0 && sensors.getAddress(dsAddr, 0)) {
    dsPresent = true;
    // Outdoor temperature doesn't need 12-bit (0.06F) precision; 9-bit
    // converts in ~94ms instead of ~750ms.
    sensors.setResolution(9);
    // One blocking conversion now so the first getTempF() has a real reading,
    // then switch to non-blocking: getTempF() collects each conversion a
    // second after starting it, so loop() never waits on the sensor.
    sensors.requestTemperatures();
    sensors.setWaitForConversion(false);
    Serial.println("temp sensor configured");
  } else {
    Serial.println("!!temp sensor configuration failed, using A0 diode fallback!!");
  }
#endif
#if defined(ESP32)
  // 2.5dB attenuation spans ~0-1.25V: better resolution for a ~0.4-0.7V diode
  analogSetPinAttenuation(A0, ADC_2_5db);
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

#if defined(ESP32)
  // Reset the board if loop() ever hangs (task WDT, ~5s), so setup()'s
  // allOff() closes the valves. arduino-esp32 leaves this off by default; the
  // ESP8266's hardware watchdog already does the equivalent.
  enableLoopWDT();
#endif

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
  processWebCommands(); // web UI valve/run/reboot requests (only loop() touches valves/timer)
  controlRelays();      // activate relay if correct time
  valveWatchdog();      // last-resort: close any valve open past its limit
  ElegantOTA.loop();

  if (millis() - lastHousekeepingMs >= 1000) {  // once per second housekeeping (rollover-safe)
    timer.tick();                        // tick the timer (to shut down valve tests after two minutes)
    state.cur_temp = getTempF();         // sample sensor here (loop ctx); web handlers read the cache
    updateHourlyTempAverage();
    updateAutoSeason();                  // follow calendar seasons unless manually overridden
    updateWeather();                     // fetch yesterday's Open-Meteo ET0 when due (never mid-cycle)
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  // toggle the LED
    lastHousekeepingMs = millis();
  }
#if !defined(ESP32)
  // We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}
