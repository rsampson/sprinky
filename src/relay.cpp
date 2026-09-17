#include "sprinky.h"
#include <TimeLib.h>


void relayConfig() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(relay[i], OUTPUT);
  }
}

bool valveIsOpen[NUM_RELAYS];

void allOff() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    digitalWrite(relay[i], RELAY_INACTIVE);
    valveIsOpen[i] = false;
  }
}

bool shutOff(void*) {  // bool return and void* makes timer api happy
  allOff();
  timer.cancel();
  Serial.println("timer shut off ");
  return (false);
}

// turn a specific relay on, all others off
// be aware that this function gets called multiple times, but should only run once per session
void relayOn(int relay_index) {

  if (valveIsOpen[relay_index] == true) return;  // only turn on if off
  allOff();

  // "buzz" relay to clear jammed valve
  // for (int j = 0; j < 3; j++) {
  //   digitalWrite(relay[relay_index], RELAY_ACTIVE);
  //   delay(20);
  //   digitalWrite(relay[relay_index], RELAY_INACTIVE);
  //   delay(20);
  // }

  for (int i = 0; i < NUM_RELAYS; i++) {  // make sure only one relay is on at a time
    // turn relay on, all others off
    digitalWrite(relay[i], (i == relay_index) ? RELAY_ACTIVE : RELAY_INACTIVE);
    valveIsOpen[i] = (i == relay_index);
  }
  // turn on last valve as a master safety valve
  //digitalWrite(relay[NUM_RELAYS - 1], RELAY_ACTIVE);

  time_t t = now();
  webPrint("Valve %1d on %s @ %02d:%02d:%02d %02d/%02d \n",  relay_index + 1, Days[weekday()], hour(t), minute(t), second(t),  month(t), day(t));
}

// runtimes are in seconds, start times are in ms
// temp_adjust has the sec to ms conversion factored in (temp adjust is in ms)
// Offsets are relative to state.start_time_ms (OFFSET1 = 0), not absolute
// millis() values -- controlRelays() compares them against (millis() -
// state.start_time_ms), which wraps correctly across a millis() rollover.
// Comparing absolute millis() against an absolute START value doesn't: if
// start_time_ms is close enough to UINT32_MAX that a later threshold
// overflows past it, the wrapped threshold becomes smaller than an earlier
// one and every window comparison fails, silently ending the cycle with no
// valve ever opened.
#define OFFSET1 0UL
#define OFFSET2 (OFFSET1 + state.runtime[0] * state.temp_adjust)
#define OFFSET3 (OFFSET2 + state.runtime[1] * state.temp_adjust)
#define OFFSET4 (OFFSET3 + state.runtime[2] * state.temp_adjust)
#define OFFSET5 (OFFSET4 + state.runtime[3] * state.temp_adjust)
#define OFFSET6 (OFFSET5 + state.runtime[4] * state.temp_adjust)
#define OFFSET7 (OFFSET6 + state.runtime[5] * state.temp_adjust)
#define OFFSET8 (OFFSET7 + state.runtime[6] * state.temp_adjust)
#define OFFSET9 (OFFSET8 + state.runtime[7] * state.temp_adjust)

// Calendar-day key (YYYYMMDD) of the last automatic cycle start, so a scheduled
// run fires at most once per day; it self-clears when the date rolls over.
// A latch plus a short catch-up window (below) replaces the old exact
// second(t)==0 match, so a loop that runs a little late (e.g. briefly stalled
// during a WiFi outage) still starts the cycle instead of skipping the
// one-second trigger window entirely.
static long lastAutoRunDayKey = -1;

// Called when the schedule is saved, so a deliberately-changed run time can
// still fire today instead of waiting for the once-per-day latch to clear
// tomorrow.
void resetAutoRunLatch() {
  lastAutoRunDayKey = -1;
}

// How long after the scheduled minute we'll still start a missed cycle. Covers
// a sluggish loop / short stall, but not "device was off for hours" (we don't
// want it watering at noon because it booted after a morning slot).
static const int CATCHUP_MINUTES = 5;

// Starts a watering cycle right now: computes temp_adjust from the current
// average temperature (or pins it to 1.0x if scaling is off), arms the
// cycle's 80-minute safety timer, and marks the cycle as running. Shared by
// the scheduled auto-trigger and the manual "Run Now" API so a manual run
// always gets a fresh scaling factor instead of reusing whatever a prior
// scheduled cycle last computed.
void startCycle() {
  allOff();
  timer.cancel();  // cancel any manual operations
  state.start_time_ms = millis();

  // expand watering time .3 to 3x over a 40-90 average degree temp range, map it into milli seconds
  if (state.tempScaling) {
    long adjust = map((int32_t)state.avg_temp, 40, 90, 300, 3000);
    // map() extrapolates past its output range for an avg_temp outside
    // 40-90F (a failed sensor's sentinel reading, or genuinely extreme
    // weather) -- clamp before use, since temp_adjust is unsigned and a
    // negative value would wrap to a huge one, corrupting every valve's
    // on-time for the whole cycle.
    if (adjust < 300) adjust = 300;
    if (adjust > 3000) adjust = 3000;
    state.temp_adjust = (uint32_t)adjust;
  } else {
    state.temp_adjust = 1000;  // 1.0x: run times used as-is
  }

  timer.in(4800000, shutOff);  // safety: force all valves off 80 min after cycle start
  state.runCycle = true;
}

void controlRelays() {

  if (state.wateringDisabled) {
    return;
  }

  time_t t = now();                                                                           // Store the current time atomically
  long dayKey = (long)year(t) * 10000 + month(t) * 100 + day(t);  // unique per calendar day
  int nowMins = hour(t) * 60 + minute(t);
  int schedMins = state.runHour * 60 + state.runMinute;
  bool todayActive = state.activeDays & (1 << (weekday() - 1));  // weekday() is 1-7, Sunday=1

  // Trigger if it's an active day, we're within [schedule, schedule+catchup),
  // and we haven't already run today.
  bool inWindow = (nowMins >= schedMins) && (nowMins < schedMins + CATCHUP_MINUTES);
  if (todayActive && inWindow && dayKey != lastAutoRunDayKey && state.runCycle == false) {  // trigger start of cycle
    lastAutoRunDayKey = dayKey;
    startCycle();
  }

  if (state.runCycle == true) {  // run watering cycle if is time
    // Rollover-safe: elapsed wraps correctly even if millis() itself has
    // rolled over since start_time_ms, unlike comparing absolute millis()
    // against an absolute threshold (see the OFFSET macros above).
    unsigned long elapsed = millis() - state.start_time_ms;

    if (elapsed >= OFFSET1 && elapsed < OFFSET2) relayOn(0);
    else if (elapsed >= OFFSET2 && elapsed < OFFSET3) relayOn(1);
    else if (elapsed >= OFFSET3 && elapsed < OFFSET4) relayOn(2);
    else if (elapsed >= OFFSET4 && elapsed < OFFSET5) relayOn(3);
#ifdef RELAY8
    else if (elapsed >= OFFSET5 && elapsed < OFFSET6) relayOn(4);
    else if (elapsed >= OFFSET6 && elapsed < OFFSET7) relayOn(5);
    else if (elapsed >= OFFSET7 && elapsed < OFFSET8) relayOn(6);
    else if (elapsed >= OFFSET8 && elapsed < OFFSET9) relayOn(7);
#endif
    else {  // terminate cycle
      void* garb;  // make call happy
      shutOff(garb);
      // print statistics
      state.lastRunMinutes = (millis() - state.start_time_ms) / 60000;
      webPrint("Run times scaled by %2d percent\n", state.temp_adjust / 10);
      state.runCycle = false;
    }
  }
}

// True once the hourly temperature sample has been taken for the current hour;
// cleared a second later so it re-arms for the next hour.
bool hourlySampleTaken = false;

// Sample the current temperature into the 24-hour history and recompute the
// rolling average that drives temperature-based run-time scaling. Runs once
// per hour.
void updateHourlyTempAverage(void) {

  time_t t = now();                                                      // Store the current time atomically
  if (minute(t) == 0 && second(t) == 0 && hourlySampleTaken == false) {  // do once each hour
    hourlySampleTaken = true;

    // samples temp and computes the average of the last 24 hours
    dayBuffer.push(state.cur_temp);

    state.avg_temp = 0;
    // // the following ensures using the right type for the index variable
    using index_t = decltype(dayBuffer)::index_t;

    for (index_t i = 0; i < dayBuffer.size(); i++) {  // compute 24 hour temp
      state.avg_temp += dayBuffer[i];
    }
    state.avg_temp = state.avg_temp / dayBuffer.size();

  } else if (minute(t) == 0 && second(t) > 0) hourlySampleTaken = false;  // clear for run next hour
}
