#include <sys/types.h>
#pragma once

#include "config.h"
#include "wifi_manager.h"

#include "time_manager.h"
#include <Preferences.h>

#include <CircularBuffer.hpp>

// Forward decl of decltype timer
#include <arduino-timer.h>
using TimerType = decltype(timer_create_default());

inline constexpr size_t bufferSize = 1000;

// --- Sprinkler Runtime State Struct ---
struct SprinklerState {
  bool wateringDisabled;  // master off switch; when true, no valve ever opens
  bool runCycle;
  bool tempScaling;  // when true, scale valve run times by Hargreaves ET0 (default off); when false, run times as-is
  uint16_t runHour;
  uint16_t runMinute;
  unsigned long runtime[8];

  uint32_t start_time_ms;
  uint32_t temp_adjust;
  float cur_temp;
  float avg_temp;
  uint32_t lastRunMinutes;

  // Bitmask of days the watering sequence is allowed to run on.
  // Bit 0 = Sunday .. bit 6 = Saturday, matching TimeLib's weekday() (1-7, Sunday=1).
  uint8_t activeDays;

  // Bitmask of valves the watering sequence may use (bit i = valve i+1).
  // Global, not per-season. A cleared bit skips the valve in cycles
  // (the mask is frozen when a cycle starts). The server still accepts a manual
  // test of a disabled valve; only the dashboard greys its button.
  uint8_t valveEnabled;
};

// --- Global Struct Instances ---
extern SprinklerState state;

// --- Shared Utility Objects ---
extern Preferences preferences;
extern TimerType timer;
extern CircularBuffer<float, 24> dayBuffer;
extern char logBuf[bufferSize];  // recent webPrint() output, NUL-terminated (debug.cpp)
extern const char *etSource;  // ET0 source of the last cycle, owned by relay.cpp
extern bool tempFromWeather;  // last getTempF() reading came from Open-Meteo, not a sensor
extern uint8_t curSeason;  // active season profile (0=Summer..3=Spring), owned by web_server.cpp

// Timezone variables and Days array are declared in time_manager.h

// --- Function Prototypes ---
int getTempF();

// Relay Action Prototypes
void updateHourlyTempAverage();
void controlRelays();
void startCycle();
void resetAutoRunLatch();
void relayConfig();
void relayOn(int relay_index);
void allOff();
void webPrint(const char *format, ...);
bool shutOff(void *);
void valveWatchdog();
bool anyValveOpen();
void masterValveService();    // pulsed master-valve drive; a no-op unless MASTER_VALVE_PULSED
extern bool masterIsOpen;     // master valve held open (always false without MASTER_VALVE_PIN)
// Time utility functions are declared in time_manager.h
