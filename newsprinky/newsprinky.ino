/**
 * Adapted from the brilliant ESPUI written by:
 * @author Ian Gray @iangray1000
 *
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an Access
 * Point wifi with the SSID HOSTNAME (defined below). You can then connect and use
 * the controls on the "Wifi Credentials" tab to store credentials into the EEPROM.
 *
 */

#include <Arduino.h>
//#include <EEPROM.h>
#include <ESPUI.h>

#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#else
// esp8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <umm_malloc/umm_heap_select.h>
#ifndef CORE_MOCK
#ifndef MMU_IRAM_HEAP
#warning Try MMU option '2nd heap shared' in 'tools' IDE menu (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#option-summary)
#warning use decorators: { HeapSelectIram doAllocationsInIRAM; ESPUI.addControl(...) ... } (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#how-to-select-heap)
#warning then check http://<ip>/heap
#endif // MMU_IRAM_HEAP
#ifndef DEBUG_ESP_OOM
#error on ESP8266 and ESPUI, you must define OOM debug option when developping
#endif
#endif
#endif

#include <NTPClient.h> // for ntp time client
#include <WiFiUdp.h>
WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP);
#include <Preferences.h>
Preferences preferences;

#include <arduino-timer.h>
auto timer = timer_create_default(); // create a timer for auto shut down of valves
#include <TimeLib.h>
//#include <ArduinoOTA.h>

//Settings
#define SLOW_BOOT 0
#define HOSTNAME "sprinky"
#define FORCE_USE_HOTSPOT 0

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
void paramCallback(Control* sender, int type, int param);
void slideCallback(Control *sender, int type);

//ESPUI=================================================================================================================
#include <ESPUI.h>

String stored_ssid, stored_pass, stored_hour, stored_minute;
//UI handles
// uint16_t statusLabelId, serialLabelId;
uint16_t wifi_ssid_text, wifi_pass_text;
uint16_t tempLabel, debugLabel, timeLabel, mainSwitcher, mainText, mainNumber, mainTime;
uint16_t hourNumber, minuteNumber;
uint16_t groupsliders;
#define slideID1 groupsliders
uint16_t slideID2, slideID3, slideID4, slideID5, slideID6;
// Input values
uint16_t runHour = 2;        // hour to start running
uint16_t runMinute = 10;     // minute to start running 
uint16_t slide1, slide2, slide3, slide4, slide5, slide6 ; // valve on durations
//ESPUI==================================================================================================================


const size_t bufferSize = 400; // debug buffer
char charBuf[bufferSize];
bool manualOp = false; // manual operations (valve test) flag
unsigned long runtime[8]; // valve on times in seconds

// This is the main function which builds our GUI
void setUpUI() {

#ifdef ESP8266
    { HeapSelectIram doAllocationsInIRAM;
#endif

	//Turn off verbose debugging
	ESPUI.setVerbosity(Verbosity::Quiet);

	//Make sliders continually report their position as they are being dragged.
	ESPUI.sliderContinuous = true;

	/*
	 * Tab: Basic Controls
	 * This tab contains all the basic ESPUI controls, and shows how to read and update them at runtime.
	 *-----------------------------------------------------------------------------------------------------------*/
	auto maintab = ESPUI.addControl(Tab, "", "System Controls");
  ESPUI.addControl(Separator, "status", "", None, maintab);
  //ESPUI.separator("status");
	debugLabel = ESPUI.addControl(Label, "Debug", "some message", Wetasphalt, maintab, generalCallback);
  timeLabel = ESPUI.addControl(Label, "Current Time", "12:00", Wetasphalt, maintab, generalCallback);
	tempLabel = ESPUI.addControl(Label, "Outside Temperature", "70 degrees F", Wetasphalt, maintab, generalCallback);
  
  ESPUI.addControl(Separator, "controls", "", None, maintab);
  mainSwitcher = ESPUI.addControl(Switcher, "Watering Disable", "", Wetasphalt, maintab, generalCallback);

	//These are the values for the selector's options. (Note that they *must* be declared static
	//so that the storage is allocated in global memory and not just on the stack of this function.)
	static String optionValues[] {"Winter", "Spring", "Summer", "Fall"};
	auto mainselector = ESPUI.addControl(Select, "Set Season Timing Defaults", "Set Defaults", Wetasphalt, maintab, generalCallback);
	for(auto const& v : optionValues) {
		ESPUI.addControl(Option, v.c_str(), v, None, mainselector);
	}

	mainTime = ESPUI.addControl(Time, "", "", None, 0,
    [](Control *sender, int type) {
      if(type == TM_VALUE) { 
        ESPUI.updateLabel(timeLabel, timeClient.getFormattedTime());
      }
    });

//	ESPUI.addControl(Button, "Time Update", "Get Time", Wetasphalt, maintab,
//		[](Control *sender, int type) {
//			if(type == B_UP) {
//				ESPUI.updateTime(mainTime);
//			}
//		});


	/*
	 * Tab: Valve controls
	 *-----------------------------------------------------------------------------------------------------------*/
	auto grouptab = ESPUI.addControl(Tab, "", "Valve Controls");
  ESPUI.addControl(Separator, "valve control", "", None, grouptab);
	//The parent of this button is a tab, so it will create a new panel with one control.
	auto groupbutton = ESPUI.addControl(Button, "Test Valve", "valve 1", Wetasphalt, grouptab, valveButtonCallback);
	//However the parent of this button is another control, so therefore no new panel is
	//created and the button is added to the existing panel.
	ESPUI.addControl(Button, "", "valve 2", Wetasphalt, groupbutton, valveButtonCallback);
	ESPUI.addControl(Button, "", "valve 3", Wetasphalt, groupbutton, valveButtonCallback);
	ESPUI.addControl(Button, "", "valve 4", Wetasphalt, groupbutton, valveButtonCallback);
	ESPUI.addControl(Button, "", "valve 5", Wetasphalt, groupbutton, valveButtonCallback);
	ESPUI.addControl(Button, "", "valve 6", Wetasphalt, groupbutton, valveButtonCallback);
	
  ESPUI.addControl(Separator, "Run Time Settings", "", None, grouptab);

  //Number inputs also accept Min and Max components, but you should still validate the values.
  hourNumber = ESPUI.addControl(Number, "Run Hour", "12", Wetasphalt, grouptab, hourCallback);
  ESPUI.addControl(Min, "", "0", None, hourNumber);
  ESPUI.addControl(Max, "", "23", None, hourNumber);
  //Number inputs also accept Min and Max components, but you should still validate the values.
  minuteNumber = ESPUI.addControl(Number, "Run Minute", "0", Wetasphalt, grouptab, minuteCallback);
  ESPUI.addControl(Min, "", "0", None, minuteNumber);
  ESPUI.addControl(Max, "", "60", None, minuteNumber);  
  
  //Sliders can be grouped as well 
	//To label each slider in the group, we are going add additional labels and give them custom CSS styles
	//We need this CSS style rule, which will remove the label's background and ensure that it takes up the entire width of the panel
	String clearLabelStyle = "background-color: unset; width: 75%;";
	//First we add the main slider to create a panel
	groupsliders = ESPUI.addControl(Slider, "Run Time (in seconds)", "600", Wetasphalt, grouptab, slideCallback);
	//Then we add a label and set its style to the clearLabelStyle. Here we've just given it the name "A"
	ESPUI.setElementStyle(ESPUI.addControl(Label, "", "valve 1", None, groupsliders), clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, groupsliders);
  ESPUI.addControl(Max, "", "600", None, groupsliders);
  
	//We can now continue to add additional sliders and labels
	slideID2 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
	ESPUI.setElementStyle(ESPUI.addControl(Label, "", "valve 2", None, groupsliders), clearLabelStyle);
	ESPUI.addControl(Min, "", "1", None, slideID2);
  ESPUI.addControl(Max, "", "600", None, slideID2);
  
	slideID3 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
	ESPUI.setElementStyle(ESPUI.addControl(Label, "", "valve 3", None, groupsliders), clearLabelStyle);
	ESPUI.addControl(Min, "", "1", None, slideID3);
  ESPUI.addControl(Max, "", "600", None, slideID3);
  
	slideID4 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
	ESPUI.setElementStyle(ESPUI.addControl(Label, "", "valve 4", None, groupsliders), clearLabelStyle);
	ESPUI.addControl(Min, "", "1", None, slideID4);
  ESPUI.addControl(Max, "", "600", None, slideID4);
  
	slideID5 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
	ESPUI.setElementStyle(ESPUI.addControl(Label, "", "valve 5", None, groupsliders), clearLabelStyle);
	ESPUI.addControl(Min, "", "1", None, slideID5);
  ESPUI.addControl(Max, "", "600", None, slideID5);
  
	slideID6 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
	ESPUI.setElementStyle(ESPUI.addControl(Label, "", "valve 6", None, groupsliders), clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID6);
  ESPUI.addControl(Max, "", "600", None, slideID6);

  ESPUI.addControl(Button, "Save Schedule", "Save", Wetasphalt, grouptab, SaveScheduleCallback);
  // retrieve settings and initialize sliders
  runtime[0] = preferences.getString("slide1", "300").toInt(); 
  runtime[1] = preferences.getString("slide2", "300").toInt(); 
  runtime[2] = preferences.getString("slide3", "300").toInt(); 
  runtime[3] = preferences.getString("slide4", "300").toInt(); 
  runtime[4] = preferences.getString("slide5", "300").toInt(); 
  runtime[5] = preferences.getString("slide6", "300").toInt(); 
  ESPUI.updateSlider(slideID1, runtime[0]); 
  ESPUI.updateSlider(slideID2, runtime[1]); 
  ESPUI.updateSlider(slideID3, runtime[2]); 
  ESPUI.updateSlider(slideID4, runtime[3]); 
  ESPUI.updateSlider(slideID5, runtime[4]); 
  ESPUI.updateSlider(slideID6, runtime[5]); 

	/*
	 * Tab: WiFi Credentials
	 * You use this tab to enter the SSID and password of a wifi network to autoconnect to.
	 *-----------------------------------------------------------------------------------------------------------*/
	auto wifitab = ESPUI.addControl(Tab, "", "WiFi Credentials");
	wifi_ssid_text = ESPUI.addControl(Text, "SSID", "", Wetasphalt, wifitab, textCallback);
	//Note that adding a "Max" control to a text control sets the max length
	ESPUI.addControl(Max, "", "32", None, wifi_ssid_text);
	wifi_pass_text = ESPUI.addControl(Text, "Password", "", Wetasphalt, wifitab, textCallback);
	ESPUI.addControl(Max, "", "64", None, wifi_pass_text);
	ESPUI.addControl(Button, "Save", "Save", Wetasphalt, wifitab, SaveWifiDetailsCallback
 
//	    [](Control *sender, int type) {
//			if(type == B_UP) {
//				Serial.println("Saving credentials to EPROM...");
//				Serial.println(ESPUI.getControl(wifi_ssid_text)->value);
//				Serial.println(ESPUI.getControl(wifi_pass_text)->value);
//
//        
//				unsigned int i;
//				EEPROM.begin(100);
//				for(i = 0; i < ESPUI.getControl(wifi_ssid_text)->value.length(); i++) {
//					EEPROM.write(i, ESPUI.getControl(wifi_ssid_text)->value.charAt(i));
//					if(i==30) break; //Even though we provided a max length, user input should never be trusted
//				}
//				EEPROM.write(i, '\0');
//
//				for(i = 0; i < ESPUI.getControl(wifi_pass_text)->value.length(); i++) {
//					EEPROM.write(i + 32, ESPUI.getControl(wifi_pass_text)->value.charAt(i));
//					if(i==94) break; //Even though we provided a max length, user input should never be trusted
//				}
//				EEPROM.write(i + 32, '\0');
//				EEPROM.end();
//			}
//	    }

);

	//Finally, start up the UI.
	//This should only be called once we are connected to WiFi.
	ESPUI.begin("Garden Watering System");

#ifdef ESP8266
    } // HeapSelectIram
#endif

}

// temperature measuring stuff
#define DS18B20
#ifdef DS18B20
// sensor libraries
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(21);  // sensor hooked to gpio21
DallasTemperature sensors(&oneWire);
#endif

int getTempF() {
  int tempF;
#ifdef DS18B20
  if (sensors.getDeviceCount() != 0) {
    sensors.requestTemperatures(); // Send the command to get temperatures
    tempF = sensors.getTempFByIndex(0);
  } else {
    tempF = 70; // sensor failed, fake it
  }
#else
  int sensorValue = analogRead(A0);  // read diode voltage attached to A0 pin
  // map diode voltage to temperature F  ( diode mv values recorded from freezing and boiling water)
  // int tempF = map(sensorValue, 640, 402, 32, 212); // 1n914 diode @ .44 ma (10k / 5v)
  // Wemos mini devides by  .3125
  tempF = map(sensorValue, 200, 126, 32, 212); // 1n914 diode @ .44 ma (10k / 5v)
#endif
  return (tempF);
}


#define LED_ON LOW   // for working the built in LED
#define LED_OFF HIGH
#ifdef RELAY8
// #define BLINK_LED 10  // 10 is a dummy LED for the 8 relay board, it does nothing
//#define BLINK_LED 23
#define BLINK_LED LED_BUILTIN // for DOIT devkit v1 (esp wroom-esp s)
#else
#define BLINK_LED 5
#endif


void setup() {

  relayConfig();
  allOff();
  pinMode(LED_BUILTIN, OUTPUT); // set heartbeat LED pin to OUTPUT
  
	Serial.begin(115200);
	while(!Serial);
  if(SLOW_BOOT) delay(5000); //Delay booting to give time to connect a serial monitor
   
 #ifdef DS18B20 // temp sensor
  sensors.begin();
  if (sensors.getDeviceCount() != 0) {
    Serial.println("temp sensor configured");
  } else {
    Serial.println("!!temp sensor configuration failed!!");    
  }
#endif

  if(!preferences.begin("Settings")) {
    Serial.println("Failed to open preferences.");
    ESP.restart();
  } else {
	  connectWifi();
 
	  setUpUI();
    stored_hour = preferences.getString("hour", "2");
    stored_minute = preferences.getString("minute", "0");
    ESPUI.updateNumber(hourNumber, stored_hour.toInt());
    ESPUI.updateNumber(minuteNumber, stored_minute.toInt());   
  }
    
  timeClient.begin();
  timeClient.setTimeOffset(-28800); // UTC to pacific standard time
  
  Serial.println("We Are Go!");
}

void loop() {
	static long unsigned halfSecondTimer = 0;
  static long unsigned twoSecondTimer = 0;
  
	if(millis() > halfSecondTimer + 500) {
   
		ESPUI.updateLabel(tempLabel, String( getTempF()) + " deg F");
    
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // toggle the LED
    
    controlRelays();  // activate relay if correct time
    
		halfSecondTimer = millis();
	}

  // update debug information
  if(millis() > twoSecondTimer + 2000) {
    fetchDebugText();
    String debugString = (char*)charBuf;
    
    ESPUI.updateLabel(debugLabel, debugString);
    ESPUI.updateTime(mainTime);
    
    setTime(timeClient.getHours(),timeClient.getMinutes(), timeClient.getSeconds(), timeClient.getDay(), 1, 2025); // set time, only care about second minute hour and day    
//    webPrint( "Current Time: %2d:%2d \n", hour(), minute());
//    webPrint( "Run Time: %2d:%2d \n", runHour, runMinute);
    
    twoSecondTimer = millis();
  }
  

	//Simple debug UART interface
	if(Serial.available()) {
		switch(Serial.read()) {
			case 'w': //Print IP details
				Serial.println(WiFi.localIP());
				break;
			case 'W': //Reconnect wifi
				connectWifi();
				break;
			case 'C': //Force a crash (for testing exception decoder)
				#if !defined(ESP32)
					((void (*)())0xf00fdead)();
				#endif
				break;
			default:
				Serial.print('#');
				break;
		}
	}

  timeClient.update();      // run ntp time client  
  timer.tick();             // tick the timer (to shut down valve tests after two minutes)

	#if !defined(ESP32)
		//We don't need to call this explicitly on ESP32 but we do on 8266
		MDNS.update();
	#endif

}


//Utilities
//------------------------------------------------------------------------------------------------
//void readStringFromEEPROM(String& buf, int baseaddress, int size) {
//	buf.reserve(size);
//	for (int i = baseaddress; i < baseaddress+size; i++) {
//		char c = EEPROM.read(i);
//		buf += c;
//		if(!c) break;
//	}
//}

void connectWifi() {
	int connect_timeout;

#if defined(ESP32)
	WiFi.setHostname(HOSTNAME);
#else
	WiFi.hostname(HOSTNAME);
#endif
	Serial.println("Begin wifi...");

	//Load credentials from EEPROM
	if(!(FORCE_USE_HOTSPOT)) {
		yield();
    //******************************************************
    stored_ssid = preferences.getString("ssid", "SSID");
    stored_pass = preferences.getString("pass", "PASSWORD");
    //*******************************************************
//		EEPROM.begin(100);
//		String stored_ssid, stored_pass;
//		readStringFromEEPROM(stored_ssid, 0, 32);
//		readStringFromEEPROM(stored_pass, 32, 96);
//		EEPROM.end();

		//Try to connect with stored credentials, fire up an access point if they don't work.
     Serial.println("Connecting to : " + stored_ssid);
     //Serial.println("With Password : " + stored_pass);
		#if defined(ESP32)
			WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());
		#else
			WiFi.begin(stored_ssid, stored_pass);
		#endif
		connect_timeout = 28; //7 seconds
		while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) {
			delay(250);
			Serial.print(".");
			connect_timeout--;
		}
	}

	if (WiFi.status() == WL_CONNECTED) {
		Serial.println(WiFi.localIP());
		Serial.println("Wifi started");

		if (!MDNS.begin(HOSTNAME)) {
			Serial.println("Error setting up MDNS responder!");
		}
	} else {
		Serial.println("\nCreating access point...");
		WiFi.mode(WIFI_AP);
		WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
		WiFi.softAP(HOSTNAME);

		connect_timeout = 20;
		do {
			delay(250);
			Serial.print(",");
			connect_timeout--;
		} while(connect_timeout);
	}
  #if defined(ESP32)
    WiFi.setSleep(false); //For the ESP32: turn off sleeping to increase UI responsivness (at the cost of power use)
  #endif
}
