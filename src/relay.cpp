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

// --- Hargreaves ET0 (FAO-56 Irrigation & Drainage Paper 56, eqs. 21-25, 52) ---

// Extraterrestrial radiation Ra (MJ/m^2/day) for a latitude and day of year.
static float extraterrestrialRad(float latDeg, int doy) {
  const float phi = latDeg * (float)M_PI / 180.0f;
  const float dr = 1.0f + 0.033f * cosf(2.0f * (float)M_PI * doy / 365.0f);         // inverse Earth-Sun distance
  const float decl = 0.409f * sinf(2.0f * (float)M_PI * doy / 365.0f - 1.39f);    // solar declination
  float x = -tanf(phi) * tanf(decl);
  if (x > 1.0f) x = 1.0f;  // polar night / midnight sun guard
  if (x < -1.0f) x = -1.0f;
  const float ws = acosf(x);  // sunset hour angle
  return 37.586f * dr * (ws * sinf(phi) * sinf(decl) + cosf(phi) * cosf(decl) * sinf(ws));
}

// Reference evapotranspiration (mm/day) from mean temp and daily max-min swing, both F.
static float hargreavesET0(int doy, float meanF, float rangeF) {
  const float meanC = (meanF - 32.0f) / 1.8f;
  const float rangeC = rangeF / 1.8f;
  return 0.0023f * 0.408f * extraterrestrialRad(LATITUDE_DEG, doy) * (meanC + 17.8f) * sqrtf(rangeC);
}

static int dayOfYear(time_t t) {
  static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
  int y = year(t);
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return cum[month(t) - 1] + day(t) + ((leap && month(t) > 2) ? 1 : 0);
}

// Run-time factor = ET0 over the last 24 hourly samples / ET0 of the active
// season's typical day (ET_REFERENCE in config.h), clamped. Falls back to 1.0
// when the history is too short (just booted) or flat (dead sensor reads a
// constant 70F), since Hargreaves needs a real daily temperature swing.
static float computeEtScale() {
  const auto n = dayBuffer.size();
  if (n < 12) {
    webPrint("ET scaling: only %d h of temps, using 100%%\n", (int)n);
    return 1.0f;
  }
  float tMax = dayBuffer[0], tMin = dayBuffer[0], sum = 0;
  for (decltype(dayBuffer)::index_t i = 0; i < n; i++) {
    float v = dayBuffer[i];
    if (v > tMax) tMax = v;
    if (v < tMin) tMin = v;
    sum += v;
  }
  const float meanF = sum / n;
  const float rangeF = tMax - tMin;
  if (rangeF < 2.0f) {
    webPrint("ET scaling: temp swing %dF too flat (sensor?), using 100%%\n", (int)rangeF);
    return 1.0f;
  }

  const EtReference &ref = ET_REFERENCE[curSeason < 4 ? curSeason : 0];
  const float et0 = hargreavesET0(dayOfYear(now()), meanF, rangeF);
  const float etRef = hargreavesET0(ref.dayOfYear, ref.meanF, ref.rangeF);
  float scale = et0 / etRef;
  if (!isfinite(scale)) {  // NaN slips past the clamps below and would corrupt valve timing
    webPrint("ET scaling: invalid result, using 100%%\n");
    return 1.0f;
  }
  if (scale < ET_SCALE_MIN) scale = ET_SCALE_MIN;
  if (scale > ET_SCALE_MAX) scale = ET_SCALE_MAX;

  // integer tenths: avoids relying on %f support in webPrint's vsnprintf
  webPrint("ET0 %d.%dmm (ref %d.%d), Tmean %dF swing %dF -> %d%%\n",
           (int)(et0 * 10) / 10, (int)(et0 * 10) % 10, (int)(etRef * 10) / 10, (int)(etRef * 10) % 10,
           (int)meanF, (int)rangeF, (int)(scale * 100));
  return scale;
}

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

  // temp_adjust is the run-time factor x1000 (it doubles as the s->ms conversion)
  if (state.tempScaling) {
    state.temp_adjust = (uint32_t)(computeEtScale() * 1000.0f);
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
