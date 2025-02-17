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

// Tested on ESP32 WROOM 32  and ESP12-F (esp8266)

#include <Arduino.h>
#include <ESPUI.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

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

#include <WiFiUdp.h>
WiFiUDP ntpUDP;

#include <NTPClient.h>         // for ntp time client
NTPClient timeClient(ntpUDP);  //https://github.com/arduino-libraries/NTPClient/blob/master/NTPClient.h

#include <Preferences.h>
Preferences preferences;

#include <arduino-timer.h>
auto timer = timer_create_default();  // create a timer for auto shut down of valves

#include <TimeLib.h>
#include <ElegantOTA.h>

#include <CircularBuffer.hpp>         // https://github.com/rlogiacco/CircularBuffer
CircularBuffer<float, 24> dayBuffer;  // store 24 hour temp samples

//Settings
#define HOSTNAME "testsprinky"

//Function Prototypes
void connectWifi();
void setUpUI();
void textCallback(Control *sender, int type);
void generalCallback(Control *sender, int type);
void valveButtonCallback(Control *sender, int type);
void hourCallback(Control *sender, int type);
void minuteCallback(Control *sender, int type);
void SaveWifiDetailsCallback(Control *sender, int type);
void SaveSheduleCallback(Control *sender, int type);
void paramCallback(Control *sender, int type, int param);
void slideCallback(Control *sender, int type);
void tempAdjRunTime(void);
void controlRelays(void);

//ESPUI=================================================================================================================
#include <ESPUI.h>
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

// temperature measuring stuff ********************************************
#define DS18B20
#ifdef DS18B20
// sensor libraries
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(TEMP_PIN);  // sensor hooked to TEMP_PIN
DallasTemperature sensors(&oneWire);
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
  tempF = float(map(sensorValue, 640, 402, 32, 212));  // 1n914 diode @ .44 ma (10k / 5v)
                                                       // Wemos mini devides by  .3125
                                                       //tempF = map(sensorValue, 200, 126, 32, 212); // 1n914 diode @ .44 ma (10k / 5v)
#endif
  return (tempF);
}
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

#define BOOT_REASON_MESSAGE_SIZE 150
char bootReasonMessage[BOOT_REASON_MESSAGE_SIZE];
String bootTime;
float avg_temp = 70.0;
char IP[] = "xxx.xxx.xxx.xxx";  // IP address string
extern void allOff();

void setup() {

  Serial.begin(115200);
  relayConfig();
  allOff();
  pinMode(LED_BUILTIN, OUTPUT);  // set heartbeat LED pin to OUTPUT
  digitalWrite(LED_BUILTIN, LOW);

  if (!preferences.begin("Settings")) {
    Serial.println("Failed to open preferences.");
    ESP.restart();
  }
  connectWifi();

  timeClient.begin();                // set up ntp time client and then freewheeling time
  timeClient.setTimeOffset(-28800);  // UTC to pacific standard time
  timeClient.update();
  setTime(timeClient.getEpochTime());  // todo: may need to do this periodically

#ifdef DS18B20  // temp sensor
  sensors.begin();
  if (sensors.getDeviceCount() != 0) {
    Serial.println("temp sensor configured");
  } else {
    Serial.println("!!temp sensor configuration failed!!");
  }
#endif
  dayBuffer.clear();
  //avg_temp = getTempF();

  Serial.println("configuring Gui");
  setUpUI();

  disable = preferences.getBool("disable", "0");
  ESPUI.updateSwitcher(mainSwitcher, disable);
  ESPUI.updateLabel(aveTempLabel, "24 hour average temperature: " + String(avg_temp) + " F");

  ElegantOTA.begin(ESPUI.WebServer());
  // boot up message
  // webPrint( "%s up at: %s on %s\n", HOSTNAME, timeClient.getFormattedTime(), Days[weekday()]); 
  // getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  // webPrint("Reset reason: %s\n", bootReasonMessage);

  Serial.println("We Are Go!");
}

long unsigned previousTime;

void loop() {
  timeClient.update();  // run ntp time client
  timer.tick();         // tick the timer (to shut down valve tests after two minutes)
  tempAdjRunTime();
  controlRelays();  // activate relay if correct time
  ElegantOTA.loop();

  if (millis() > previousTime + 1000) {                    // update gui once per second
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  // toggle the LED
    fetchDebugText();
    ESPUI.updateLabel(debugLabel, String(charBuf));
    ESPUI.updateTime(mainTime);
    ESPUI.updateLabel(tempLabel, String(getTempF()) + " deg F");
    ESPUI.updateLabel(signalLabel, String(WiFi.RSSI()) + " dbm");
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

    IPAddress ip = WiFi.localIP();  // display ip address
    ip.toString().toCharArray(IP, 16);
    webPrint("Wifi up, IP address = %s \n", IP);
    Serial.print(WiFi.RSSI());
    Serial.println(" dbm");

    if (!MDNS.begin(HOSTNAME)) {
      Serial.println("Error setting up MDNS responder!");
    }
  } else {
    Serial.println("\nCreating access point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(HOSTNAME);
  }
#if defined(ESP32)
  WiFi.setSleep(false);  //For the ESP32: turn off sleeping to increase UI responsivness (at the cost of power use)
#endif
}
