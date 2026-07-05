#pragma once

#include "config.h"
#include "wifi_manager.h"

#include "time_manager.h"
#include <Preferences.h>

#include <CircularBuffer.hpp>
//#include <umm_malloc/umm_heap_select.h>

// Forward decl of decltype timer
#include <arduino-timer.h>
using TimerType = decltype(timer_create_default());

inline constexpr size_t bufferSize = 400;

// --- UI Control Handles Struct ---
struct UIControls {
  uint16_t timeLabel;
  uint16_t tempLabel;
  uint16_t aveTempLabel;
  uint16_t signalLabel;
  uint16_t runtimeLabel;

  uint16_t mainSwitcher;
  uint16_t mainTime;
  uint16_t debugLabel;

  uint16_t wifiSSIDText;
  uint16_t wifiPassText;

  uint16_t hourNumber;
  uint16_t minuteNumber;
  uint16_t timeZoneLabel;

  // We group identical elements into arrays!
  uint16_t buttons[8];
  uint16_t valves[8];
  uint16_t sliders[8];
  uint16_t slideLabels[8];

#ifdef USE_WITH_HA
  uint16_t mqttUserText;
  uint16_t mqttPassText;
  uint16_t mqttBrokerText;
#endif
};

// --- Sprinkler Runtime State Struct ---
struct SprinklerState {
  bool disable;
  bool runCycle;
  uint16_t runHour;
  uint16_t runMinute;
  unsigned long runtime[8];

  uint32_t start_time_ms;
  uint32_t temp_adjust;
  float avg_temp;
};

// --- Global Struct Instances ---
extern UIControls ui;
extern SprinklerState state;

// --- Shared Utility Objects ---
extern Preferences preferences;
extern TimerType timer;
extern CircularBuffer<float, 24> dayBuffer;
extern CircularBuffer<char, (bufferSize - 4)> circBuff;
extern char charBuf[bufferSize];
extern char stylecol2[30];

extern String stored_hour;
extern String stored_minute;

// Timezone variables and Days array are declared in time_manager.h

// --- Function Prototypes ---
int getTempF();
void connectWifi();
void fetchDebugText();
void setUpUI();

// Callback Prototypes
struct Control; // Forward declare ESPUI Control struct
void generalCallback(Control *sender, int type);
void textCallback(Control *sender, int type);
void valveButtonCallback(Control *sender, int type);
void slideCallback(Control *sender, int type);
void hourCallback(Control *sender, int type);
void minuteCallback(Control *sender, int type);
void switchCallback(Control *sender, int type);
void SaveWifiDetailsCallback(Control *sender, int type);
void SaveScheduleCallback(Control *sender, int type);
void TZcallback(Control *sender, int type);
void RunCallback(Control *sender, int type);
void ESPReset(Control *sender, int type);

// Relay Action Prototypes
void ComputeAveTemp();
void controlRelays();
void relayConfig();
void relayOn(int relay_index);
void allOff();
void webPrint(const char *format, ...);
bool shutOff(void *);
// Time utility functions are declared in time_manager.h

#ifdef USE_WITH_HA
class HASwitch;
void onValveSwitchCommand(bool switchState, HASwitch *sender);
void onDisableSwitchCommand(bool switchState, HASwitch *sender);
#endif
