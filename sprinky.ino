
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <arduino-timer.h>
#include <TimeLib.h>
#include <ArduinoOTA.h>
#include <BufferPrinter.h>  // https://github.com/bakercp/BufferUtils
#include <Adafruit_SleepyDog.h>  // watchdog

//#define RELAY8

#ifdef RELAY8
#define NAME "sprinky8"
#else
#define NAME "sprinky4"
#endif

//  ********** Set watering schedule here **********

#define RUN_HOUR 7       // hour to start running
#define RUN_MINUTE 10    // minute to start running 

#define DURATION1 1      // how long to run staton 1, etc
#define DURATION2 2
#define DURATION3 2
#define DURATION4 4
#define DURATION5 2
#define DURATION6 4
#define DURATION7 1
#define DURATION8 1

#define LED_ON LOW   // for working the built in LED
#define LED_OFF HIGH


#ifdef RELAY8
#define BLINK_LED 10  // 10 is a dummy LED for the 8 relay board, it does nothing
#else
#define BLINK_LED 5
#endif

const size_t bufferSize = 400;
uint8_t charBuf[bufferSize];
BufferPrinter printer(charBuf, bufferSize);


// ********** WEB SERVER STUFF *****************

// Captive portal code from: https://github.com/esp8266/Arduino/blob/master/libraries/DNSServer/examples/CaptivePortal/CaptivePortal.ino
const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer webServer(80);

void handleFile(const String& file, const String& contentType)
{
  File f = LittleFS.open(file, "r");
  if (!f) {
    Serial.println("***Error opening " + file + " ***");
    webServer.send(200, "text/plain", "error sending " + file);
  } else {   
    int file_size = f.size();
    int size_returned = webServer.streamFile(f, contentType);
    if ( size_returned != file_size) {
      Serial.println("Sent less data than expected for " + file);
      Serial.println("Got " + String(size_returned) + " expected " + String(file_size));
    }
  }
  f.close();
}
auto timer = timer_create_default(); // create a timer with default settings
auto timer_led = timer_create_default(); // create a timer to blink led

bool toggle_led(void *) {
  digitalWrite(BLINK_LED, !digitalRead(BLINK_LED)); // toggle the LED
  controlRelays();  // activate relay if correct time
  return true; // repeat? true
}

bool manualOp = false;

bool shutOff(void *) {  // make timer api happy
  allOff();
  timer.cancel();
  manualOp = false;
  Serial.print("timer shut off ");
  return (false);
}

// process the GET parameters sent from client
void handleParameters() {
  //   printer.print(webServer.args());
  //   printer.print(" args<br>");

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
  // printer.print(cBuf);

  // set internal clock time
  int m;
  int h;
  int d;

  if (webServer.hasArg("minute")) m = webServer.arg("minute").toInt();
  if (webServer.hasArg("hour"))   h = webServer.arg("hour").toInt();
  if (webServer.hasArg("day"))    d = webServer.arg("day").toInt();

  if (webServer.hasArg("minute") ||  webServer.hasArg("hour") || webServer.hasArg("day")) {
    setTime(h, m, 0, d, 1, 2022); // set time, only care about minute hour and day
    printer.print("<br>Time set ");
    printer.print(minute());
    printer.print(" min ");
    printer.print(hour());
    printer.print(" hr ");
    printer.print(day());
    printer.print(" day");
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

int last_m;  // time of last operations
int last_h;
int last_d;

void handleOperatonFeedback() { // embedded page that displays the recent operations
  printer.print("<br> last op "); // display time of last operation
  printer.print(last_m);
  printer.print(" min ");
  printer.print(last_h);
  printer.print(" hr ");
  printer.print(last_d);
  printer.print(" day");
  printer.write(0); // mark end of string

  char temp[500];
  snprintf(temp, sizeof(temp),

"<html>\
  <head>\
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\
    <title>Feedback</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; \
      font-size: 14px; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <p>%s</p>\
  </body>\
</html>",

           charBuf );

  //  Serial.write(temp);  // examine web page html
  //  Serial.println();
  webServer.send(200, "text/html", temp);
  printer.setOffset(0);  // reset printer buffer
  memset(charBuf, 0, sizeof(charBuf));
}

void handleName() {  // embedded page that displays the station name

  char _name[] = NAME;
  char temp[500];
  snprintf(temp, sizeof(temp),

"<html>\
  <head>\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
    <title>Name</title>\
    <style>\
      body { font-family: Arial, Helvetica, Sans-Serif; \
      font-size: 14px; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <p>%s</p>\
  </body>\
</html>",

           _name );  // put name in %s above

  //  Serial.write(temp);  // examine web page html for test
  //  Serial.println();
  webServer.send(200, "text/html", temp);
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
#define START2  START1 + DURATION1
#define START3  START2 + DURATION2
#define START4  START3 + DURATION3
#define START5  START4 + DURATION4
#define START6  START5 + DURATION5
#define START7  START6 + DURATION6
#define START8  START7 + DURATION7

#ifdef  RELAY8 
#define ON LOW
#define OFF HIGH
#else
#define ON HIGH
#define OFF LOW
#endif

// turn relay rl on, all others off
void relayOn(int rl) {
  if (digitalRead(relay[rl]) == OFF ) {   // only turn ON  if it is currently OFF
    Serial.print("relay ");
    Serial.println(rl);
    printer.print("<br>relay ");
    printer.print(rl);

    last_m = minute();    // record time of operation
    last_h = hour();
    last_d = day();
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
  if (!manualOp) {  // if not being operated manually
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
      else if (minute() >= START8 && minute() < START8 + DURATION8) {
        relayOn(7);
      }
#endif
      else {
        allOff();  // not the run minute
      }
    } else  allOff(); // not the run hour
  }
}

// ********** SETP AND LOOP *****************
void setup() {
  relayConfig();
  allOff();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_OFF);
  pinMode(BLINK_LED, OUTPUT); // set LED pin to OUTPUT
  // call the toggle_led function every 1000 millis (1 second)
  timer_led.every(1000, toggle_led);

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
  webServer.on("/operation.html", handleOperatonFeedback);
  webServer.on("/name.html", handleName);
  webServer.on("/style.css", handleStyle);
  webServer.on("/favicon.ico", handleFavicon);
  webServer.onNotFound(handleRoot); // needed to make auto sign in work
  webServer.begin();

  ArduinoOTA.setHostname(NAME);
  ArduinoOTA.begin();
  
  int countdownMS = Watchdog.enable(1000); 
   
  Serial.println("Starting server");
}


void loop() {
  webServer.handleClient();
  dnsServer.processNextRequest(); // captive portal support
  ArduinoOTA.handle();
  timer.tick();      // valve run & shut off
  timer_led.tick();  // blink led "heart beat"
  Watchdog.reset();
}
