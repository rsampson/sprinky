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

#define DS18B20  // compile in DS18B20 support; the A0 diode is always the fallback

// --- Silicon-diode temperature sensor on A0 (fallback when no DS18B20) ---
// Diode forward voltage at the A0 pin, calibrated in ice water and boiling
// water (1N914 biased at ~0.44 mA through 10k). Working in millivolts keeps
// one calibration valid on both ESP8266 and ESP32. Re-calibrate if the bias
// resistor or supply changes (e.g. 10k from 3.3V instead of 5V drops Vf ~12mV).
constexpr float DIODE_MV_AT_32F = 625.0f;
constexpr float DIODE_MV_AT_212F = 393.0f;
// A reading outside this window doesn't look like a forward-biased silicon
// diode (missing, open, shorted), so the reading defaults to 70F. Spans about
// -40F..160F on the calibration above.
constexpr float DIODE_MV_MIN = 460.0f;
constexpr float DIODE_MV_MAX = 718.0f;
// ESP8266 only: A0 full-scale voltage in mV. 1000 for a bare ESP-12E/F module;
// 3200 for NodeMCU/Wemos D1 boards with the on-board 220k/100k divider. (ESP32
// reads calibrated millivolts directly.)
constexpr float ESP8266_A0_FULL_SCALE_MV = 1000.0f;

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
