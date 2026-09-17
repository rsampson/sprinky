#include "wifi_manager.h"
#include "config.h"

#if defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#else
// esp8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#endif

WifiState currentWifiState = WIFI_STATE_DISCONNECTED;
unsigned long wifiStateTimer = 0;
const unsigned long WIFI_CONNECT_TIMEOUT =
    15000; // 15 seconds connection timeout
const unsigned long WIFI_CHECK_INTERVAL =
    10000; // Check connection integrity every 10 seconds

char ipStr[] = "xxx.xxx.xxx.xxx"; // local IP as text, filled in on connect

String stored_ssid;
String stored_pass;
bool ap_mode = true;

// Track link up/down transitions so the Status-page log can show how long the
// link had been up when it dropped, and how long it was down when it recovered.
static unsigned long wifiConnectedSince = 0; // millis() when link last came up
static unsigned long wifiDownSince = 0;      // millis() when link last dropped

// Render a millis() delta compactly for a log line: "45s", "6m", "3h12m".
static String humanDur(unsigned long ms) {
  unsigned long s = ms / 1000;
  if (s < 60)
    return String(s) + "s";
  unsigned long m = s / 60;
  if (m < 60)
    return String(m) + "m";
  return String(m / 60) + "h" + String(m % 60) + "m";
}

#include <Preferences.h>
extern Preferences preferences;
extern void webPrint(const char *format, ...);

void setupWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

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

  // Try to connect with stored credentials, fire up an access point if they
  // don't work.
  Serial.println("Connecting to : " + stored_ssid);
  WiFi.mode(WIFI_STA);
#if defined(ESP32)
  WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());
#else
  WiFi.begin(stored_ssid, stored_pass);
#endif
  connect_timeout = 28; // 7 seconds
  while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) {
    delay(250);
    Serial.print(".");
    connect_timeout--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    currentWifiState = WIFI_STATE_CONNECTED;
    ap_mode = false;
    IPAddress ip = WiFi.localIP(); // display ip address
    ip.toString().toCharArray(ipStr, 16);
    webPrint("Wifi up, IP address = %s \n", ipStr);
    webPrint("WiFi RSSI %d dBm\n", WiFi.RSSI());
    wifiConnectedSince = millis();
    Serial.print(WiFi.RSSI());
    Serial.println(" dbm");
    // Disable auto-reconnect as we are managing it with our custom, robust
    // state machine
    WiFi.setAutoReconnect(false);
    WiFi.persistent(true);

  
    if (!MDNS.begin(HOSTNAME)) {
      Serial.println("Error setting up MDNS responder!");
    }

    MDNS.addService("http", "tcp", 80);
 
  } else {
    ap_mode = true;
    Serial.println("\nCreating access point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(HOSTNAME);
  }

#if defined(ESP32)
  WiFi.setSleep(false); // For the ESP32: turn off sleeping to increase UI
                        // responsivness (at the cost of power use)
#endif
 
}

void handleWiFi() {
  if (ap_mode == true) {
    // In AP mode, we don't need to manage connection states, so we can skip the
    // state machine
    return;
  }

  unsigned long currentMillis = millis();

  switch (currentWifiState) {
  case WIFI_STATE_DISCONNECTED: {
    Serial.println("WiFi: Disconnected. Initiating connection...");

    // Completely reset Wi-Fi hardware before initiating a new connection to
    // clear any stuck states
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);

    Serial.printf("WiFi: Connecting to SSID '%s'...\n", stored_ssid.c_str());
#if defined(ESP32)
    WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());
#else
    WiFi.begin(stored_ssid, stored_pass);
#endif
    wifiStateTimer = currentMillis;
    currentWifiState = WIFI_STATE_CONNECTING;
    break;
  }

  case WIFI_STATE_CONNECTING: {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi: Connection established successfully!");
      Serial.print("WiFi: IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("WiFi: RSSI (Signal Strength): ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");

      if (wifiDownSince == 0)
        webPrint("WiFi back: %d dBm (initial)\n", WiFi.RSSI());
      else
        webPrint("WiFi back: %d dBm (down %s)\n", WiFi.RSSI(),
                 humanDur(currentMillis - wifiDownSince).c_str());
      wifiConnectedSince = currentMillis;

      // Re-arm mDNS against the fresh connection -- setupWiFi() only starts
      // it once at boot, but the DISCONNECTED state above tears down and
      // rebuilds the WiFi interface on every reconnect, so <hostname>.local
      // stops resolving after any drop unless we start it again here.
      if (!MDNS.begin(HOSTNAME)) {
        Serial.println("Error setting up MDNS responder!");
      }
      MDNS.addService("http", "tcp", 80);

      wifiStateTimer = currentMillis; // reset timer for periodic checks
      currentWifiState = WIFI_STATE_CONNECTED;
    } else if (currentMillis - wifiStateTimer >= WIFI_CONNECT_TIMEOUT) {
      Serial.println("WiFi: Connection timeout reached. Retrying...");
      webPrint("WiFi connect timed out, retrying\n");
      currentWifiState = WIFI_STATE_DISCONNECTED;
    }
    break;
  }

  case WIFI_STATE_CONNECTED: {
    // Periodically check the connection integrity
    if (currentMillis - wifiStateTimer >= WIFI_CHECK_INTERVAL) {
      wifiStateTimer = currentMillis; // reset check timer

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi: Connection lost! Triggering reconnection...");
        webPrint("WiFi lost: %d dBm, up %s\n", WiFi.RSSI(),
                 humanDur(currentMillis - wifiConnectedSince).c_str());
        wifiDownSince = currentMillis;

        currentWifiState = WIFI_STATE_DISCONNECTED;
      }
    }
    break;
  }
  }
}
