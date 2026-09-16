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
#define START1 state.start_time_ms
#define START2 (START1 + state.runtime[0] * state.temp_adjust)
#define START3 (START2 + state.runtime[1] * state.temp_adjust)
#define START4 (START3 + state.runtime[2] * state.temp_adjust)
#define START5 (START4 + state.runtime[3] * state.temp_adjust)
#define START6 (START5 + state.runtime[4] * state.temp_adjust)
#define START7 (START6 + state.runtime[5] * state.temp_adjust)
#define START8 (START7 + state.runtime[6] * state.temp_adjust)
#define START9 (START8 + state.runtime[7] * state.temp_adjust)

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
    allOff();
    timer.cancel();  // cancel any manual operations
    state.start_time_ms = millis();

    //expand watering time .3 to 3x over a 40-90 average degree temp range, map it into milli seconds
    if (state.tempScaling)
      state.temp_adjust = map((int32_t)state.avg_temp, 40, 90, 300, 3000);
    else
      state.temp_adjust = 1000;  // 1.0x: run times used as-is

    timer.in(4800000, shutOff);  // safety: force all valves off 80 min after cycle start
    state.runCycle = true;
  }

  if (state.runCycle == true) {  // run watering cycle if is time

    if (millis() >= START1 && millis() < START2) relayOn(0);
    else if (millis() >= START2 && millis() < START3) relayOn(1);
    else if (millis() >= START3 && millis() < START4) relayOn(2);
    else if (millis() >= START4 && millis() < START5) relayOn(3);
#ifdef RELAY8
    else if (millis() >= START5 && millis() < START6) relayOn(4);
    else if (millis() >= START6 && millis() < START7) relayOn(5);
    else if (millis() >= START7 && millis() < START8) relayOn(6);
    else if (millis() >= START8 && millis() < START9) relayOn(7);
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
