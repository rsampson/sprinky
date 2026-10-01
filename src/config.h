#pragma once

// --- Device Configuration ---
constexpr const char *HOSTNAME = "sprinky";

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

// --- Temperature scaling (FAO-56 Hargreaves reference evapotranspiration) ---
// Run times are scaled by ET0(last 24h) / ET0(active season's reference day),
// so each season profile's configured minutes mean "a typical day in that
// season". Latitude sets the solar-radiation term (north positive).
constexpr float LATITUDE_DEG = 33.0f;

// Typical day per season profile, in profile order Summer/Fall/Winter/Spring:
// day of year (mid-season), 24h mean temp (F), daily max-min swing (F).
// Rough Southern California values -- tune them from the "ET0 ... Tmean/swing"
// lines logged on the Status page each run.
struct EtReference {
  int dayOfYear;
  float meanF;
  float rangeF;
};
constexpr EtReference ET_REFERENCE[] = {
  { 196, 74.0f, 22.0f },  // Summer (mid-July)
  { 288, 67.0f, 22.0f },  // Fall   (mid-October)
  { 15, 56.0f, 20.0f },   // Winter (mid-January)
  { 105, 62.0f, 20.0f },  // Spring (mid-April)
};

// Safety clamp on the computed run-time factor.
constexpr float ET_SCALE_MIN = 0.25f;
constexpr float ET_SCALE_MAX = 2.0f;

// Relay active level. Some relay boards are active-low (a LOW signal closes the
// relay contact / opens the valve). Swap these if your valves energize backwards.
#define RELAY_ACTIVE HIGH
#define RELAY_INACTIVE LOW
