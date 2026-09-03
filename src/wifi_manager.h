#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// Wi-Fi State Machine states
enum WifiState {
  WIFI_STATE_DISCONNECTED,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED
};

// --- WiFi Global Interfaces ---
extern String stored_ssid;
extern String stored_pass;
extern bool ap_mode;
extern WifiState currentWifiState;

// --- WiFi Actions ---
void setupWiFi();
void handleWiFi();

#endif // WIFI_MANAGER_H
