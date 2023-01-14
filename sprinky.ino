
//#define ESP32
#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#else
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#endif

#include <DNSServer.h>
#include <LittleFS.h>
#include <arduino-timer.h>
#include <TimeLib.h>
#include <ArduinoOTA.h>
#include <CircularBuffer.h> // https://github.com/rlogiacco/CircularBuffer
#include <Adafruit_SleepyDog.h>  // watchdog
#include <WebSocketsServer.h>
#include <ArduinoJson.h>


#define RELAY8

#ifdef RELAY8
#define NAME "sprinky8"
#else
#define NAME "sprinky4"
#endif

//  ********** Set watering schedule here **********

#define RUN_HOUR 2       // hour to start running
#define RUN_MINUTE 10    // minute to start running 

#define DURATION1 1      // how long to run staton 1, etc (winter)
#define DURATION2 2
#define DURATION3 2
#define DURATION4 4
#define DURATION5 2
#define DURATION6 4
#define DURATION7 1
#define DURATION8 1

int runtime1 = DURATION1;  // the runtimes will adjust by temperature
int runtime2 = DURATION2;
int runtime3 = DURATION3;
int runtime4 = DURATION4;
int runtime5 = DURATION5;
int runtime6 = DURATION6;
int runtime7 = DURATION7;
int runtime8 = DURATION8;

#define LED_ON LOW   // for working the built in LED
#define LED_OFF HIGH

#ifdef RELAY8
#define BLINK_LED 10  // 10 is a dummy LED for the 8 relay board, it does nothing
#else
#define BLINK_LED 5
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

const size_t bufferSize = 400;
char charBuf[bufferSize];

// Create a circular buffer for debug output on web page
CircularBuffer<char, (bufferSize - 40)> buff;
  
// write string into circular buffer for later printout on browser
void webPrint(char * format, ...)
{
  char buffer[256];
  
  va_list args;
  va_start (args, format);
  vsprintf (buffer, format, args);
  Serial.println(buffer);
  // prepend an html break
  buff.push('<');
  buff.push('b');  
  buff.push('r');
  buff.push('>');
  // put formated string into circular buffer
  for (int i = 0; i < strlen(buffer); i++)
    {
     buff.push(buffer[i]);
    }
  va_end (args);
}

// ********** WEB SERVER STUFF *****************

// Captive portal code from: https://github.com/esp8266/Arduino/blob/master/libraries/DNSServer/examples/CaptivePortal/CaptivePortal.ino
const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void handleFile(const String& file, const String& contentType) // stream data files to browser
{
  File f = LittleFS.open(file, "r");
  if (!f) {
    Serial.println("***Error opening " + file + " ***");
    webServer.send(200, "text/plain", "error sending " + file);
  } else {
    int file_size = f.size();
   //webServer.sendHeader("Content-Length", (String)(file_size));
    size_t size_returned = webServer.streamFile(f, contentType);
//    if ( size_returned != file_size) {
//      Serial.println("Sent less data than expected for " + file);
//      Serial.println("sent " + String(size_returned) + " expected " + String(file_size));
//    }
  }
  f.close();
}

int getTempF() {
  
#ifdef RELAY8
  int sensorValue = 450;  // dummy value until we have hardware support.
#else
  int sensorValue = analogRead(A0);  // read diode voltage attached to A0 pin
#endif 
  // map diode voltage to temperature F  ( diode mv values recorded from freezing and boiling water)
  int tempF = map(sensorValue, 640, 402, 32, 212); // 1n914 diode @ .44 ma (10k / 5v) 

  return(tempF);
}

void handleOperatonMessage() { // displays the recent operations/ debug info

  // read all of circular buffer into charBuff
  // circular buffer contains recent debug print out

     // trim old debug data   
    while(buff.available() < 40) {
      buff.shift();
    }
    
   // line up to first html break (<br>)
   //while(buff[0] != '<') {
   //   buff.shift();
   //}

  int qty = buff.size();
  int i = 0; 
  // unload ring buffer contents
   for(i = 0; i < qty; i++) {

    charBuf[i] = buff.shift();
    buff.push(charBuf[i]);
  }
   charBuf[i + 1] = 0; //terminate string
}

void sendSocket() {
  const int buflen = 600;
  char cbuff[buflen];
  StaticJsonDocument<500> doc;

  doc["name"] = NAME;
  doc["temp"] = getTempF();
  handleOperatonMessage();
  doc["operation"] = charBuf;
  
  serializeJson(doc, cbuff, buflen);

  webSocket.broadcastTXT(cbuff, strlen(cbuff));
}

auto timer = timer_create_default(); // create a timer with default settings
auto timer_led = timer_create_default(); // create a timer to blink led

bool toggle_led(void *) {
  digitalWrite(BLINK_LED, !digitalRead(BLINK_LED)); // toggle the LED
  controlRelays();  // activate relay if correct time
  sendSocket(); // ping browser to verify connection
  return true; // repeat? true
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("Disconnected!\n");
      break;
    case WStype_CONNECTED: {      // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("Connected from %d.%d.%d.%d url: %s\n", ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    default: {
        Serial.print("WStype = ");
        Serial.println(type);
      }
  }
}

bool manualOp = false;

bool shutOff(void *) {  // make timer api happy
  allOff();
  timer.cancel();
  manualOp = false;
  Serial.println("timer shut off ");
  return (false);
}

bool disable = false;  // flag to disable/enable watering

void handleParameters() {  // process the GET parameters sent from client

  String message;
  // message what we got
  for (int i = 0; i < webServer.args(); i++) {
    message += "  (";
    message += String(i) + ") ";
    message += webServer.argName(i) + "= ";
    message += webServer.arg(i);
  }
  Serial.println(message);

  char cBuf[200];
  message.toCharArray(cBuf, sizeof(cBuf));

  // set internal clock time
  int m;
  int h;
  int d;

  if (webServer.hasArg("minute")) m = webServer.arg("minute").toInt();
  if (webServer.hasArg("hour"))   h = webServer.arg("hour").toInt();
  if (webServer.hasArg("day"))    d = webServer.arg("day").toInt();

  if (webServer.hasArg("minute") ||  webServer.hasArg("hour") || webServer.hasArg("day")) {
    setTime(h, m, 0, d, 1, 2022); // set time, only care about minute hour and day
    webPrint("Time set: day %2d hour %2d minute %2d",day(),hour(),minute() );
  }
  
 // handle enable watering
  if (webServer.hasArg("onoff")) {
     if (webServer.arg("onoff") == "disable") {
       disable = true;
       Serial.println("water off");
       webPrint("water off");
     }

  } else {
       disable = false;
       Serial.println("water on");
  }
  
  // handle valve actuation from web client
  if (webServer.hasArg("sprinkler_valve")) {
    timer.in(60000, shutOff);  // turn off any manually activated valve after an minute
    manualOp = true;           // indicate in manual mode
    if      (webServer.arg("sprinkler_valve") == "v1") relayOn(0);   // only use 6 stations for now
    else if (webServer.arg("sprinkler_valve") == "v2") relayOn(1);
    else if (webServer.arg("sprinkler_valve") == "v3") relayOn(2);
    else if (webServer.arg("sprinkler_valve") == "v4") relayOn(3);
    else if (webServer.arg("sprinkler_valve") == "v5") relayOn(4);
    else if (webServer.arg("sprinkler_valve") == "v6") relayOn(5);
  }
}

void handleRoot()
{
  handleFile("/index.html", "text/html");
  if (webServer.args() > 0) handleParameters();
}

void handleStyle()
{
  handleFile("/style.css", "text/css");
}

void handleFavicon()
{
  handleFile("/favicon.ico", "image/x-icon");
}

// ********** RELAY STUFF *****************

//  8 Relay board data at:
//  http://wiki.sunfounder.cc/index.php?title=8_Channel_5V_Relay_Module&utm_source=thenewstack&utm_medium=website

#ifdef  RELAY8     // if using a board with 8 relays
int relay[8] = {16, 5, 4, 0, 2, 14, 12, 13}; // this is the output gpio pin ordering
#else              // else using board with 4 relays
int relay[4] = {16, 14, 12, 13};
#endif


// do not modify these definitions
#define SUNDAY    1
#define MONDAY    2
#define TUESDAY   3
#define WEDNESDAY 4
#define THURSDAY  5
#define FRIDAY    6
#define SATURDAY  7

#define START1  RUN_MINUTE
#define START2  START1 + runtime1
#define START3  START2 + runtime2
#define START4  START3 + runtime3
#define START5  START4 + runtime4
#define START6  START5 + runtime5
#define START7  START6 + runtime6
#define START8  START7 + runtime7

#ifdef  RELAY8
#define ON LOW
#define OFF HIGH
#else
#define ON HIGH
#define OFF LOW
#endif

// turn relay rl on, all others off
void relayOn(int rl) {
  if (digitalRead(relay[rl]) == OFF && !disable) {   // only turn ON  if it is currently OFF

    webPrint("Valve %1d on at %2d:%2d", rl + 1, hour(), minute());

    int i;
    for (i = 0; i < sizeof relay / sizeof relay[0]; i++) { // make sure only one relay is on at a time
      digitalWrite(relay[i], i == rl ? ON : OFF);
    }

  }
}

void allOff() {
  int i;
  for (i = 0; i < sizeof relay / sizeof relay[0]; i++) {
    digitalWrite(relay[i], OFF);
  }
}

void relayConfig( ) {
  int i;
  for (i = 0; i < sizeof relay / sizeof relay[0]; i++) {
    pinMode(relay[i], OUTPUT);
  }
}

void controlRelays() {
  if (!manualOp) {  // if not being operated manually or disabled
    //if (weekday() == FRIDAY ||  weekday() == TUESDAY || weekday() == THURSDAY) {
    if (hour() == RUN_HOUR) {
      if     (minute() < START1) {
        allOff();  // it's to soon in the hour
      }
      if     (minute() >= START1 && minute() < START2) {
        relayOn(0);
      }
      else if (minute() >= START2 && minute() < START3) {
        relayOn(1);
      }
      else if (minute() >= START3 && minute() < START4) {
        relayOn(2);
      }
      else if (minute() >= START4 && minute() < START5) {
        relayOn(3);
      }
#ifdef RELAY8
      else if (minute() >= START5 && minute() < START6) {
        relayOn(4);
      }
      else if (minute() >= START6 && minute() < START7) {
        relayOn(5);
      }
      else if (minute() >= START7 && minute() < START8) {
        relayOn(6);
      }
      else if (minute() >= START8 && minute() < START8 + runtime8) {
        relayOn(7);
      }
#endif
      else {
        allOff();  // not the run minute
      }
    } else  allOff(); // not the run hour
  }
  // measure temperature at noon to adjust watering times
  if (hour() == 2 && minute() == 10) {
    // expand watering time to 2x over a 40-110 degree temp range
   int temperature_adjustment = map(getTempF(), 40, 110, 100, 200);
   
   //webPrint("Watering time increased by a factor of %2d percent", temperature_adjustment);
   runtime1 = int((DURATION1 * temperature_adjustment) / 100);
   runtime2 = int((DURATION2 * temperature_adjustment) / 100);
   runtime3 = int((DURATION3 * temperature_adjustment) / 100);
   runtime4 = int((DURATION4 * temperature_adjustment) / 100);
   runtime5 = int((DURATION5 * temperature_adjustment) / 100);
   runtime6 = int((DURATION6 * temperature_adjustment) / 100);
   runtime7 = int((DURATION7 * temperature_adjustment) / 100);
   runtime8 = int((DURATION8 * temperature_adjustment) / 100);
  }
  
}

// ********** SETP AND LOOP *****************
void setup() {
   
  relayConfig();
  allOff();
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_OFF);
  pinMode(BLINK_LED, OUTPUT); // set LED pin to OUTPUT

  Serial.begin(115200);
  while (!Serial) {
    ;  // wait for Serial port to connect. Needed for native USB port only
  }
  Serial.println();
  Serial.println("started");

  WiFi.softAP(NAME);

  Serial.println("connected");

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request, IP address will be default (192.168.4.1)
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  if (!LittleFS.begin()) {
    Serial.println("Error mounting FS");
    return;
  }
  Serial.println("FS mounted");

  webServer.on("/style.css", handleStyle);
  webServer.on("/favicon.ico", handleFavicon);
  webServer.onNotFound(handleRoot); // needed to make auto sign in work
  webServer.begin();

  webSocket.onEvent(webSocketEvent);
  webSocket.begin();

  ArduinoOTA.setHostname(NAME);
  ArduinoOTA.begin();

  int countdownMS = Watchdog.enable(1000);

  // call the toggle_led function every 1000 millis (1 second)
  timer_led.every(1000, toggle_led);

  Serial.println("Starting server");
}


void loop() {
  webServer.handleClient();
  dnsServer.processNextRequest(); // captive portal support
  ArduinoOTA.handle();
  timer.tick();      // valve run & shut off
  timer_led.tick();  // blink led "heart beat"
  Watchdog.reset();
  webSocket.loop();
}
