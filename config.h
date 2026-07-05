#pragma once
// extern "C" {
//   #include "umm_malloc/umm_malloc.h"
// }
#include <Arduino.h>
// --- Device Configuration ---
constexpr const char *HOSTNAME = "testsprinky";
#define LED_BUILTIN 2 // For ESP32 dev module, change if using different board

// --- Hardware Profile Flags ---
#define RELAY8

#ifdef RELAY8
static constexpr uint8_t NUM_RELAYS = 8;
#else
static constexpr uint8_t NUM_RELAYS = 4;
#endif

// this code should work with any esp32/esp8266 relay board that is available
// from the usual chinese sources such as Alibaba. You will have to set up
// the exact relay mapping for your board in the array below:

#ifdef RELAY8  // if using a board with 8 relays
static const uint8_t  relay[NUM_RELAYS] = { 32, 33, 25, 26, 27, 14, 12, 13 };
#else  
static const uint8_t  relay[NUM_RELAYS] = { 16, 14, 12, 13 };
#endif

#define DS18B20

// #define USE_WITH_HA // Add feature to be controlled by Home Assistant

#define ON LOW
#define OFF HIGH

// #define ON HIGH
// #define OFF LOW

// --- Buffer Configs ---
constexpr size_t BOOT_REASON_MESSAGE_SIZE = 150;

// --- Home Assistant Configs ---
#ifdef USE_WITH_HA
#define BROKER_ADDR IPAddress(192, 168, 0, 223)
#define BROKER_USERNAME "mqtt_user"
#define BROKER_PASSWORD "squid6562"
#endif