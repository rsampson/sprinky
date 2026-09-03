#pragma once

// --- Device Configuration ---
constexpr const char *HOSTNAME = "sprinky2";
#define LED_BUILTIN 2 // For ESP32 dev module, change if using different board

// --- Hardware Profile Flags ---
//#define RELAY8

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

// #define ON LOW
// #define OFF HIGH

#define ON HIGH
#define OFF LOW

// --- Buffer Configs ---
constexpr size_t BOOT_REASON_MESSAGE_SIZE = 150;