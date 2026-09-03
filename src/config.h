#pragma once

// --- Device Configuration ---
constexpr const char *HOSTNAME = "sprinky2";

// this code should work with any esp32/esp8266 relay board that is available
// from the usual chinese sources such as Alibaba. You will have to set up
// the exact relay mapping for your board in the array below:

//#define RELAY8  // comment this out if using a board with only 4 relays
#ifdef RELAY8  // if using a board with 8 relays
static const uint8_t  relay[] = { 32, 33, 25, 26, 27, 14, 12, 13 };
#else  
static const uint8_t  relay[] = { 16, 14, 12, 13 };
#endif

static constexpr uint8_t NUM_RELAYS = sizeof(relay) / sizeof(relay[0]) ;

#define LED_BUILTIN 2 // For ESP32 dev module, change if using different board

#define DS18B20

// These may need redefining depending on the board you are using. 
// The default values below work for the board I have.
#define ON HIGH
#define OFF LOW

// --- Buffer Configs ---
constexpr size_t BOOT_REASON_MESSAGE_SIZE = 150;