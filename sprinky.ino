/**
 * GUI adapted from the brilliant ESPUI written by: Lukas Bachschwell
 * and a demo by Ian Gray @iangray1000
 *
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an
 * Access Point wifi with the SSID HOSTNAME (defined below). You can then
 * connect and use the controls on the "Wifi Credentials" tab to store
 * credentials into the nvmem.
 *
 */

// Tested on ESP32 Dev module  and ESP12-F (esp8266), make sure these match your
// board, otherwise strange results may occur.


#include "sprinky.h"


#if defined(ESP32)
#include <WiFi.h>
#define TEMP_PIN 21
#else  // esp8266
#include <ESP8266WiFi.h>
#define DEBUG true  // set to true for debug output, false for no debug output
#define Serial \
  if (DEBUG) \
  Serial
#define TEMP_PIN D1
// #define RELAY8  // some esp8266 boards may have 8 relays
#include <umm_malloc/umm_heap_select.h>
#ifndef CORE_MOCK
#ifndef MMU_IRAM_HEAP
#warning Try MMU option '2nd heap shared' in 'tools' IDE menu (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#option-summary)
#warning use decorators: { HeapSelectIram doAllocationsInIRAM; ESPUI.addControl(...) ... } (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#how-to-select-heap)
#warning then check http://<ip>/heap
#endif  // MMU_IRAM_HEAP
#ifndef DEBUG_ESP_OOM
#error on ESP8266 and ESPUI, you must define OOM debug option when developping
#endif
#endif
#endif

WiFiClient client;
// UDP instance is now defined in time_manager.cpp

#ifdef USE_WITH_HA
#include <ArduinoHA.h>
#define ARDUINOHA_DEBUG
HADevice device;
HAMqtt mqtt(client, device);
HASensorNumber analogSensor("GardenTemperature", HASensorNumber::PrecisionP1);
HASwitch switch1("valve1");
HASwitch switch2("valve2");
HASwitch switch3("valve3");
HASwitch switch4("valve4");
#ifdef RELAY8
HASwitch switch5("valve5");
HASwitch switch6("valve6");
HASwitch switch7("valve7");
HASwitch switch8("valve8");
HASwitch
  switch9("disableSwitch");  // to turn the stand alone watering controller off
#endif
#endif

// #ifdef ERASE_FLASH
// #include <nvs_flash.h>
// #endif
#include <Preferences.h>
Preferences preferences;

#include <arduino-timer.h>  // ver 3.0.1
TimerType timer =
  timer_create_default();  // create a timer for auto shut down of valves

#include <ElegantOTA.h>
//#include <ArduinoOTA.h>
#include <TimeLib.h>

#include <CircularBuffer.hpp>         // version 1.4.0 https://github.com/rlogiacco/CircularBuffer
CircularBuffer<float, 24> dayBuffer;  // store 24 hour temp samples

#include <ESPUI.h>  // version 2.2.4  uses EsoAsyncWebServer 3.6.0, AsynchTCP version 3.35 WebSockets 2.6.1 and Arduinojson 6.21.5
// Function Prototypes
void connectWifi();
extern void setUpUI();
extern void textCallback(Control *sender, int type);
extern void generalCallback(Control *sender, int type);
extern void valveButtonCallback(Control *sender, int type);
extern void hourCallback(Control *sender, int type);
extern void minuteCallback(Control *sender, int type);
extern void SaveWifiDetailsCallback(Control *sender, int type);
extern void paramCallback(Control *sender, int type, int param);
extern void slideCallback(Control *sender, int type);
extern void ComputeAveTemp(void);
extern void controlRelays(void);
extern void relayConfig();
extern void webPrint(const char *format, ...);
extern void allOff();


UIControls ui;
SprinklerState state = { .disable = false,
                         .runCycle = false,
                         .runHour = 2,
                         .runMinute = 10,
                         .runtime = { 300, 300, 300, 300, 300, 300, 300, 300 },
                         .start_time_ms = 0,
                         .temp_adjust = 1000,
                         .avg_temp = 65.0f };

String stored_hour, stored_minute;
char charBuf[bufferSize];
char stylecol2[30];
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
  // map diode voltage to temperature F  ( diode mv values recorded from
  // freezing and boiling water)
  tempF = float(
    map(sensorValue, 640, 402, 32,
        212));  // 1n914 diode @ .44 ma (10k / 5v), Wemos mini devides by .3125
                // tempF = map(sensorValue, 200, 126, 32, 212); // 1n914 diode @ .44 ma (10k /
                // 5v)
#endif
#ifdef USE_WITH_HA
  // report temperature to home assistant
  analogSensor.setValue(tempF);
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

// Timezone rules, variables, and getNtpTime() are now defined in
// time_manager.cpp

char bootReasonMessage[BOOT_REASON_MESSAGE_SIZE];
String bootTime;

#ifdef USE_WITH_HA

void onMqttMessage(const char *topic, const uint8_t *payload, uint16_t length) {
  // This callback is called when message from MQTT broker is received.
  // Please note that you should always verify if the message's topic is the one
  // you expect. For example: if (memcmp(topic, "myCustomTopic") == 0) { ... }

  Serial.print("New message on topic: ");
  Serial.println(topic);
  Serial.print("Data: ");
  Serial.println((const char *)payload);

  mqtt.publish("myPublishTopic", "hello");
}

void onMqttConnected() {
  Serial.println("Connected to the broker!");

  // You can subscribe to custom topic if you need
  mqtt.subscribe("myCustomTopic");
}

void onMqttDisconnected() {
  Serial.println("Disconnected from the broker!");
}

void onMqttStateChanged(HAMqtt::ConnectionState state) {
  Serial.print("MQTT state changed to: ");
  Serial.println(static_cast<int8_t>(state));
}

// Array of valve switch pointers for easy indexing
static HASwitch *valveSwitches[] = {
  &switch1,
  &switch2,
  &switch3,
  &switch4,
#ifdef RELAY8
  &switch5,
  &switch6,
  &switch7,
  &switch8,
#endif
};
static constexpr int NUM_VALVE_SWITCHES =
  sizeof(valveSwitches) / sizeof(valveSwitches[0]);

// Turn off all valve switches in Home Assistant (report state back)
static void allValveSwitchesOff() {
  for (int i = 0; i < NUM_VALVE_SWITCHES; i++) {
    valveSwitches[i]->setState(false);
  }
}

void onValveSwitchCommand(bool switchState, HASwitch *sender) {
  if (switchState) {
    // Turning a valve ON — find which one and activate it
    for (int i = 0; i < NUM_VALVE_SWITCHES; i++) {
      if (sender == valveSwitches[i]) {
        relayOn(i);
        // Turn off all other valve switches in HA (only one valve at a time)
        for (int j = 0; j < NUM_VALVE_SWITCHES; j++) {
          if (j != i)
            valveSwitches[j]->setState(false);
        }
        break;
      }
    }
    timer.in(60000,
             shutOff);  // turn off any manually activated valve after a minute
  } else {
    // Turning a valve OFF
    allOff();
    allValveSwitchesOff();
  }
  sender->setState(switchState);  // report state back to Home Assistant
}

void onDisableSwitchCommand(bool switchState, HASwitch *sender) {
  if (sender == &switch9) {
    // the switch9 has been toggled
    // switchState == true means ON state (watering disabled)
    if (switchState) {
      state.disable = true;
      ESPUI.updateControlLabel(ui.mainSwitcher, "Watering off");
      preferences.putBool("disable", true);
    } else {
      state.disable = false;
      ESPUI.updateControlLabel(ui.mainSwitcher, "Watering on");
      preferences.putBool("disable", false);
    }
  }
  sender->setState(switchState);  // report state back to Home Assistant
}
#endif  // USE_WITH_HA


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
  setTime(getNtpTime());
  setSyncProvider(getNtpTime);
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

  Serial.println("configuring Gui");
  setUpUI();

  ElegantOTA.begin(ESPUI.WebServer());

  printTZ(tz);

  state.disable = preferences.getBool("disable", "0");

  if (state.disable == true) {
    ESPUI.updateLabel(ui.waterLabel, "Watering OFF");
  } else {
    ESPUI.updateLabel(ui.waterLabel, "Watering ON");
  }

  ESPUI.updateLabel(ui.aveTempLabel, "24 hour average temperature: " + String(state.avg_temp) + " F");

  //  boot up message
  char buf1[20];
  time_t t = now();
  sprintf(buf1, "%02d:%02d:%02d %02d/%02d", hour(t), minute(t), second(t),
          month(t), day(t));
  webPrint("%s up at: %s on %s\n", HOSTNAME, buf1,  Days[weekday()]);
  getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  webPrint("Reset reason: %s\n", bootReasonMessage);

#ifdef USE_WITH_HA
  // Unique ID must be set!
  byte mac[6];
  WiFi.macAddress(mac);
  device.setUniqueId(mac, sizeof(mac));

  String controllerName = HOSTNAME;
  controllerName += "_Sprinkler_Control";
  device.setName(controllerName.c_str());
  device.enableSharedAvailability();
  device.enableLastWill();

  mqtt.onMessage(onMqttMessage);
  mqtt.onConnected(onMqttConnected);
  mqtt.onDisconnected(onMqttDisconnected);
  mqtt.onStateChanged(onMqttStateChanged);

  analogSensor.setIcon("mdi:thermometer");
  analogSensor.setName("garden temperature");
  analogSensor.setUnitOfMeasurement("F");

  switch1.setIcon("mdi:water-pump");
  switch1.setName("Valve1");
  switch1.onCommand(onValveSwitchCommand);

  switch2.setIcon("mdi:water-pump");
  switch2.setName("Valve2");
  switch2.onCommand(onValveSwitchCommand);

  switch3.setIcon("mdi:water-pump");
  switch3.setName("Valve3");
  switch3.onCommand(onValveSwitchCommand);

  switch4.setIcon("mdi:water-pump");
  switch4.setName("Valve4");
  switch4.onCommand(onValveSwitchCommand);

  switch5.setIcon("mdi:water-pump");
  switch5.setName("Valve5");
  switch5.onCommand(onValveSwitchCommand);

  switch6.setIcon("mdi:water-pump");
  switch6.setName("Valve6");
  switch6.onCommand(onValveSwitchCommand);

  switch7.setIcon("mdi:water-pump");
  switch7.setName("Valve7");
  switch7.onCommand(onValveSwitchCommand);

  switch8.setIcon("mdi:water-pump");
  switch8.setName("Valve8");
  switch8.onCommand(onValveSwitchCommand);

  switch9.setName("Disable watering controller");
  switch9.setIcon("mdi:water-off");
  switch9.onCommand(onDisableSwitchCommand);

  if (mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD)) {
    // mqtt.enableOTA();
    // mqtt.onMessage(onMessage);
    // mqtt.subscribe("sensors/#");
    Serial.println("Connected to MQTT broker");
  } else {
    Serial.println("Failed to connect to MQTT broker");
  }
#endif  // USE_WITH_HA

  Serial.println("We Are Go!");
}

// displayTime() is now defined in time_manager.cpp

long unsigned previousTime;

void loop() {

#ifdef USE_WITH_HA
  mqtt.loop();
#endif
  handleWiFi();
  timeClient.update();  // run ntp time client
  controlRelays();      // activate relay if correct time
  ElegantOTA.loop();

  if (millis() > previousTime + 1000) {  // update gui once per second
    timer.tick();                        // tick the timer (to shut down valve tests after two minutes)
    ComputeAveTemp();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  // toggle the LED
    fetchDebugText();
    ESPUI.updateLabel(ui.debugLabel, String(charBuf));
    ESPUI.updateLabel(ui.tempLabel, String(getTempF()) + " deg F");
    ESPUI.updateLabel(ui.signalLabel, String(WiFi.RSSI()) + " dbm");
   
    // determine how to find the source of time
    if (ap_mode == false) {
      displayTime();
    } else {
      ESPUI.updateTime(ui.mainTime);  // get time from browser, we are not
                                      // connected to the NTP server
    }

    previousTime = millis();
  }
#if !defined(ESP32)
  // We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}
