/**
 * GUI adapted from the brilliant ESPUI written by: Lukas Bachschwell
 * and a demo by Ian Gray @iangray1000
 *
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an Access
 * Point wifi with the SSID HOSTNAME (defined below). You can then connect and use
 * the controls on the "Wifi Credentials" tab to store credentials into the nvmem.
 *
 */

// ToDo:  display total monthly watering run time.  Link separate controllers.

// Tested on ESP32 Dev module  and ESP12-F (esp8266), make sure these match your board,
// otherwise strange results will occur.
// Also uses ESP Async WebServer 3.6.0 and Async TCP 3.3.5 other versions may not work

#include "config.h"
#include <Arduino.h>

#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#define TEMP_PIN 21
#define RELAY8
#else

// esp8266
#define DEBUG true  //set to true for debug output, false for no debug output
#define Serial \
  if (DEBUG) Serial
#define TEMP_PIN D1
//#define RELAY8  // some esp8266 boards may have 8 relays
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
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

#include <ArduinoHA.h>
#define ARDUINOHA_DEBUG
#define BROKER_ADDR IPAddress(192, 168, 0, 223)
WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);
HASensorNumber analogSensor("FrontGardenTemperature", HASensorNumber::PrecisionP1);
HAButton button1("valve1");
HAButton button2("valve2");
HAButton button3("valve3");
HAButton button4("valve4");
HAButton button5("valve5");
HAButton button6("valve6");
HAButton button7("valve7");
HAButton button8("valve8");
HASwitch switch1("disableSwitch");  // to turn the stand alone watering controller off

#include <WiFiUdp.h>
WiFiUDP ntpUDP;

#include <NTPClient.h>         // version 3.2.1 for ntp time client
NTPClient timeClient(ntpUDP);  //https://github.com/arduino-libraries/NTPClient/blob/master/NTPClient.h
#include <Timezone.h>          // https://github.com/JChristensen/Timezone

#ifdef ERASE_FLASH
#include <nvs_flash.h>
#endif
#include <Preferences.h>
Preferences preferences;

#include <arduino-timer.h>            // ver 3.0.1
auto timer = timer_create_default();  // create a timer for auto shut down of valves

#include <TimeLib.h>
#include <ElegantOTA.h>  // version 3.1.6 https://github.com/s00500/ESPUI

#include <CircularBuffer.hpp>         // version 1.4.0 https://github.com/rlogiacco/CircularBuffer
CircularBuffer<float, 24> dayBuffer;  // store 24 hour temp samples

#include <ESPUI.h>  // version 2.2.4  uses EsoAsyncWebServer 3.6.0, AsynchTCP version 3.35 WebSockets 2.6.1 and Arduinojson 6.21.5
//Function Prototypes
void connectWifi();
extern void setUpUI();
extern void textCallback(Control *sender, int type);
extern void generalCallback(Control *sender, int type);
extern void valveButtonCallback(Control *sender, int type);
extern void hourCallback(Control *sender, int type);
extern void minuteCallback(Control *sender, int type);
extern void SaveWifiDetailsCallback(Control *sender, int type);
extern void SaveSheduleCallback(Control *sender, int type);
extern void paramCallback(Control *sender, int type, int param);
extern void slideCallback(Control *sender, int type);
extern void ComputeAveTemp(void);
extern void controlRelays(void);
extern void relayConfig();
extern void webPrint(const char *format, ...);
extern void allOff();

//ESPUI=================================================================================================================

String stored_ssid, stored_pass, stored_hour, stored_minute;
//UI handles
uint16_t wifi_ssid_text, wifi_pass_text;
uint16_t tempLabel, debugLabel, timeLabel, signalLabel, bootLabel;
uint16_t groupbutton, button2Label, button3Label, button4Label, button5Label, button6Label, button7Label, button8Label;
#define button1Label groupbutton
uint16_t valve1Label, valve2Label, valve3Label, valve4Label, valve5Label, valve6Label, valve7Label, valve8Label;
uint16_t slide1Label, slide2Label, slide3Label, slide4Label, slide5Label, slide6Label, slide7Label, slide8Label;
uint16_t aveTempLabel, runtimeLabel, mainSwitcher, mainText, mainTime;
uint16_t hourNumber, minuteNumber;
uint16_t groupsliders;
uint16_t TimeZoneLabel;
#define slideID1 groupsliders
uint16_t slideID2, slideID3, slideID4, slideID5, slideID6, slideID7, slideID8;
// Input values
uint16_t runHour = 2;     // hour to start running
uint16_t runMinute = 10;  // minute to start running
//ESPUI==================================================================================================================

const size_t bufferSize = 400;  // debug buffer
char charBuf[bufferSize];

bool disable = false;      // flag to disable/enable watering
unsigned long runtime[8];  // valve on times in seconds
// button color
char stylecol2[30];
// temperature measuring stuff ********************************************

#ifdef DS18B20
// sensor libraries
#include <OneWire.h>
#include <DallasTemperature.h>
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
  // map diode voltage to temperature F  ( diode mv values recorded from freezing and boiling water)
  tempF = float(map(sensorValue, 640, 402, 32, 212));  // 1n914 diode @ .44 ma (10k / 5v), Wemos mini devides by  .3125
  //tempF = map(sensorValue, 200, 126, 32, 212); // 1n914 diode @ .44 ma (10k / 5v)
#endif
  // report temperature to home assistant
  analogSensor.setValue(tempF);
  return (tempF);
}

float avg_temp = 65;
// temp stuff ***************************************************************

String Days[] = {
  "Undefined",
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday"
};

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

// Australia Eastern Time Zone (Sydney, Melbourne)
TimeChangeRule aEDT = { "AEDT", First, Sun, Oct, 2, 660 };  // UTC + 11 hours
TimeChangeRule aEST = { "AEST", First, Sun, Apr, 3, 600 };  // UTC + 10 hours
Timezone ausET(aEDT, aEST);

// Moscow Standard Time (MSK, does not observe DST)
TimeChangeRule msk = { "MSK", Last, Sun, Mar, 1, 180 };
Timezone tzMSK(msk);

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = { "CEST", Last, Sun, Mar, 2, 120 };  // Central European Summer Time
TimeChangeRule CET = { "CET ", Last, Sun, Oct, 3, 60 };    // Central European Standard Time
Timezone CE(CEST, CET);

// United Kingdom (London, Belfast)
TimeChangeRule BST = { "BST", Last, Sun, Mar, 1, 60 };  // British Summer Time
TimeChangeRule GMT = { "GMT", Last, Sun, Oct, 2, 0 };   // Standard Time
Timezone UK(BST, GMT);

// UTC
TimeChangeRule utcRule = { "UTC", Last, Sun, Mar, 1, 0 };  // UTC
Timezone UTC(utcRule);

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = { "EDT", Second, Sun, Mar, 2, -240 };  // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = { "EST", First, Sun, Nov, 2, -300 };   // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

// US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = { "CDT", Second, Sun, Mar, 2, -300 };
TimeChangeRule usCST = { "CST", First, Sun, Nov, 2, -360 };
Timezone usCT(usCDT, usCST);

// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = { "MDT", Second, Sun, Mar, 2, -360 };
TimeChangeRule usMST = { "MST", First, Sun, Nov, 2, -420 };
Timezone usMT(usMDT, usMST);

// Arizona is US Mountain Time Zone but does not use DST
Timezone usAZ(usMST);

// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = { "PDT", Second, Sun, Mar, 2, -420 };
TimeChangeRule usPST = { "PST", First, Sun, Nov, 2, -480 };
Timezone usPT(usPDT, usPST);

Timezone *tz = &usPT;

time_t getNtpTime(void) {  // return time zone and DST adjusted time from server
  time_t serv_time = tz->toLocal(timeClient.getEpochTime());
  return (serv_time);
}


char bootReasonMessage[BOOT_REASON_MESSAGE_SIZE];
String bootTime;
char IP[] = "xxx.xxx.xxx.xxx";  // IP address string

void onButtonCommand(HAButton *sender) {
  if (sender == &button1) relayOn(0);
  else if (sender == &button2) relayOn(1);
  else if (sender == &button3) relayOn(2);
  else if (sender == &button4) relayOn(3);
  else if (sender == &button5) relayOn(4);
  else if (sender == &button6) relayOn(5);
  else if (sender == &button7) relayOn(6);
  else if (sender == &button8) relayOn(7);
  timer.in(60000, shutOff);  // turn off any manually activated valve after a minute
}

void onSwitchCommand(bool state, HASwitch *sender) {
  if (sender == &switch1) {
    // the switch1 has been toggled
    // state == true means ON state
    switch (state)
    {
    case true:
        disable = false;
        ESPUI.updateControlLabel(mainSwitcher, "Watering on");     
        preferences.putBool("disable", true);
        break;

    case false:
        disable = true;
        ESPUI.updateControlLabel(mainSwitcher, "Watering off");     
        preferences.putBool("disable", false);
        break;
    }
  } 
  sender->setState(state);  // report state back to the Home Assistant
}


void printTZ(Timezone *tzone) {
    String TZS;

    if  (tzone == &ausET) TZS = "Australia Eastern" ;
    else if (tzone == &tzMSK) TZS = "Moscow";
    else if (tzone == &CE) TZS = "Central European";
    else if (tzone == &UK) TZS = "British Standard";
    else if (tzone == &UTC) TZS = "Universal";
    else if (tzone == &usET) TZS = "Eastern Standard";
    else if (tzone == &usCT) TZS = "Central Standard";
    else if (tzone == &usMT) TZS = "Mountain Standard";
    else if (tzone == &usAZ) TZS = "Arizona";
    else if (tzone == &usPT) TZS = "Pacific Standard";
    else TZS = "Unknown TZ";

    TZS = TZS + " Time";
    Serial.println(TZS);
    ESPUI.updateControlValue(TimeZoneLabel, TZS); 
  }

void setup() {

#ifdef ERASE_FLASH
  nvs_flash_erase(); // erase the NVS partition and...
  nvs_flash_init(); // initialize the NVS partition.
#endif

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
  connectWifi();

  timeClient.begin();  // set up ntp time client and then initialize time library
  timeClient.update();

  if (preferences.isKey("timezone")) {  // initialize to UTC if TZ hasn't been set yet
     preferences.getBytes("timezone", tz, sizeof(Timezone *));  // set and store time zone selection
     Serial.println("Time Zone recovered from NVM");
  } else {
     preferences.putBytes("timezone", &UTC, sizeof(Timezone *));
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

  printTZ(tz);

  disable = preferences.getBool("disable", "0");
  ESPUI.updateSwitcher(mainSwitcher, disable);
  ESPUI.updateLabel(aveTempLabel, "24 hour average temperature: " + String(avg_temp) + " F");

  ElegantOTA.begin(ESPUI.WebServer());
  // boot up message
  webPrint("%s up at: %s on %s\n", HOSTNAME, timeClient.getFormattedTime(), Days[weekday()]);
  getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  webPrint("Reset reason: %s\n", bootReasonMessage);

  // Unique ID must be set!
  byte mac[6];
  WiFi.macAddress(mac);
  device.setUniqueId(mac, sizeof(mac));
  // set device's details (optional)
  device.setName("Sprinkler Controller");
  device.enableSharedAvailability();
  device.enableLastWill();
  // configure sensor (optional)
  analogSensor.setIcon("mdi:thermometer");
  analogSensor.setName("garden temperature");
  analogSensor.setUnitOfMeasurement("F");

  button1.setIcon("mdi:water-pump");
  button1.setName("Valve1");
  button1.onCommand(onButtonCommand);

  button2.setIcon("mdi:water-pump");
  button2.setName("Valve2");
  button2.onCommand(onButtonCommand);

  button3.setIcon("mdi:water-pump");
  button3.setName("Valve3");
  button3.onCommand(onButtonCommand);

  button4.setIcon("mdi:water-pump");
  button4.setName("Valve4");
  button4.onCommand(onButtonCommand);

  button5.setIcon("mdi:water-pump");
  button5.setName("Valve5");
  button5.onCommand(onButtonCommand);

  button6.setIcon("mdi:water-pump");
  button6.setName("Valve6");
  button6.onCommand(onButtonCommand);

  button7.setIcon("mdi:water-pump");
  button7.setName("Valve7");
  button7.onCommand(onButtonCommand);

  button8.setIcon("mdi:water-pump");
  button8.setName("Valve8");
  button8.onCommand(onButtonCommand);

  switch1.setName("Disable watering controller");
  switch1.setIcon("mdi:lightbulb");
  switch1.onCommand(onSwitchCommand);


  mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD);

  Serial.println("We Are Go!");
}

void displayTime(void) {
  char buf1[20];
  time_t t = now();
  sprintf(buf1, "%02d:%02d:%02d %02d/%02d", hour(t), minute(t), second(t), month(t), day(t));
  ESPUI.updateLabel(timeLabel, buf1);
}


long unsigned previousTime;
bool ap_mode = true;

void loop() {
  mqtt.loop();

  timeClient.update();  // run ntp time client
  timer.tick();         // tick the timer (to shut down valve tests after two minutes)
  ComputeAveTemp();
  controlRelays();  // activate relay if correct time
  ElegantOTA.loop();

  if (millis() > previousTime + 1000) {                    // update gui once per second
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  // toggle the LED
    fetchDebugText();
    ESPUI.updateLabel(debugLabel, String(charBuf));
    ESPUI.updateLabel(tempLabel, String(getTempF()) + " deg F");
    ESPUI.updateLabel(signalLabel, String(WiFi.RSSI()) + " dbm");

    // determine how to find the source of time
    if (ap_mode == false) {
      displayTime();
    } else {
      ESPUI.updateTime(mainTime);  // get time from browser, we are not connect to the NTP server
    }

    previousTime = millis();
  }
#if !defined(ESP32)
  //We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}


void connectWifi() {
  int connect_timeout;

#if defined(ESP32)
  WiFi.setHostname(HOSTNAME);
#else
  WiFi.hostname(HOSTNAME);
#endif
  Serial.println("Begin wifi...");

  yield();

  stored_ssid = preferences.getString("ssid", "SSID");
  stored_pass = preferences.getString("pass", "PASSWORD");

  //Try to connect with stored credentials, fire up an access point if they don't work.
  Serial.println("Connecting to : " + stored_ssid);
  WiFi.mode(WIFI_STA);
#if defined(ESP32)
  WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());
#else
  WiFi.begin(stored_ssid, stored_pass);
#endif
  connect_timeout = 28;  //7 seconds
  while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) {
    delay(250);
    Serial.print(".");
    connect_timeout--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    ap_mode = false;
    IPAddress ip = WiFi.localIP();  // display ip address
    ip.toString().toCharArray(IP, 16);
    webPrint("Wifi up, IP address = %s \n", IP);
    Serial.print(WiFi.RSSI());
    Serial.println(" dbm");
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    if (!MDNS.begin(HOSTNAME)) {
      Serial.println("Error setting up MDNS responder!");
    }
    MDNS.addService("http", "tcp", 80);
  } else {
    ap_mode = true;
    Serial.println("\nCreating access point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(HOSTNAME);
  }

#if defined(ESP32)
  WiFi.setSleep(false);  //For the ESP32: turn off sleeping to increase UI responsivness (at the cost of power use)
#endif
}
