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

inline constexpr size_t bufferSize = 400;

// --- Sprinkler Runtime State Struct ---
struct SprinklerState {
  bool wateringDisabled;  // master off switch; when true, no valve ever opens
  bool runCycle;
  bool tempScaling;  // when true, scale valve run times by 24h avg temp; when false, run times as-is
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
};

// --- Global Struct Instances ---
extern SprinklerState state;

// --- Shared Utility Objects ---
extern Preferences preferences;
extern TimerType timer;
extern CircularBuffer<float, 24> dayBuffer;
extern CircularBuffer<char, (bufferSize - 4)> circBuff;
extern char charBuf[bufferSize];

// Timezone variables and Days array are declared in time_manager.h

// --- Function Prototypes ---
int getTempF();
void fetchDebugText();

// Relay Action Prototypes
void updateHourlyTempAverage();
void controlRelays();
void resetAutoRunLatch();
void relayConfig();
void relayOn(int relay_index);
void allOff();
void webPrint(const char *format, ...);
bool shutOff(void *);
// Time utility functions are declared in time_manager.h
