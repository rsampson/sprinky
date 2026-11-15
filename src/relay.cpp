#include "sprinky.h"
#include "weather.h"
#include <TimeLib.h>


#ifdef MASTER_VALVE_PIN
// The master valve needs a relay of its own: a pin that is also in relay[]
// would be driven as a zone valve too.
// (Recursive: the ESP32 toolchain's C++ level has no loops in constexpr.)
constexpr bool masterPinIsZonePin(uint8_t i = 0) {
  return i < NUM_RELAYS && (relay[i] == MASTER_VALVE_PIN || masterPinIsZonePin(i + 1));
}
static_assert(!masterPinIsZonePin(), "MASTER_VALVE_PIN must not be one of the relay[] pins");
#endif

// True while the master valve is being held open (always false without
// MASTER_VALVE_PIN). Follows the zone valves: set by relayOn(), cleared by allOff().
bool masterIsOpen = false;

// Steady-level master output. In pulsed mode the pin is driven only by
// masterValveService(), from masterIsOpen.
static void masterWrite(bool open) {
  masterIsOpen = open;
#if defined(MASTER_VALVE_PIN) && !defined(MASTER_VALVE_PULSED)
  digitalWrite(MASTER_VALVE_PIN, open ? RELAY_ACTIVE : RELAY_INACTIVE);
#endif
}

void relayConfig() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(relay[i], OUTPUT);
  }
#ifdef MASTER_VALVE_PIN
  pinMode(MASTER_VALVE_PIN, OUTPUT);
#ifdef MASTER_VALVE_PULSED
  digitalWrite(MASTER_VALVE_PIN, LOW);
#endif
#endif
}

bool valveIsOpen[NUM_RELAYS];

// Valve watchdog bookkeeping: when the open valve was opened and how long it
// may stay open. Set by relayOn(), enforced by valveWatchdog(). volatile: on
// ESP32, relayOn() can run from the AsyncTCP task (manual valve test).
static volatile unsigned long valveOpenedMs = 0;
static volatile unsigned long valveMaxOpenMs = 0;
static const unsigned long MANUAL_VALVE_MS = 60000;  // manual valve-test duration

// Valve-open time used today against MAX_DAILY_SECONDS. Counted by
// valveWatchdog(), zeroed by controlRelays() when the date changes.
static unsigned long dailyOpenMs = 0;
static bool dailyLimitReached() {
  return dailyOpenMs >= MAX_DAILY_SECONDS * 1000UL;
}

static void zonesOff() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    digitalWrite(relay[i], RELAY_INACTIVE);
    valveIsOpen[i] = false;
  }
}

void allOff() {
  zonesOff();
  masterWrite(false);
}

#if defined(MASTER_VALVE_PIN) && defined(MASTER_VALVE_PULSED)
// Dead-man drive: toggle the master pin every millisecond while a valve is
// legitimately open. Called from loop() right after valveWatchdog(), and
// deliberately not from a hardware timer or PWM: the square wave has to stop
// when loop() stops. Logs the rate actually achieved each time the master
// closes (the charge pump needs roughly 40 Hz or more).
void masterValveService() {
  static bool level = false;
  static unsigned long lastToggleUs = 0, toggles = 0, sinceMs = 0;
  if (!masterIsOpen) {
    if (level) {
      level = false;
      digitalWrite(MASTER_VALVE_PIN, LOW);
    }
    if (toggles) {
      const unsigned long ms = millis() - sinceMs;
      if (ms >= 1000) webPrint("Master valve pulse averaged %lu Hz\n", 
                              (unsigned long)(toggles * 500ULL / ms)); // 64-bit: wraps after ~2.4 h in 32
      toggles = 0;
    }
    return;
  }
  if (toggles == 0) sinceMs = millis();
  if (micros() - lastToggleUs >= 1000UL) {
    lastToggleUs = micros();
    level = !level;
    digitalWrite(MASTER_VALVE_PIN, level ? HIGH : LOW);
    toggles++;
  }
}
#else
void masterValveService() {}
#endif

// Closes every valve and ends any watering cycle. Clearing runCycle matters:
// otherwise controlRelays() reopens the current valve on the next loop().
bool shutOff(void*) {  // bool return and void* makes timer api happy
  allOff();
  timer.cancel();
  state.runCycle = false;
  Serial.println("timer shut off ");
  return (false);
}

// Last-resort guard, called every loop() and independent of arduino-timer and
// the cycle sequencing: if a valve has been open longer than relayOn() allowed,
// close everything. Bounds any valve to its window plus a minute even if the
// timer, the cycle logic or the stored run times go wrong.
bool anyValveOpen() {
  for (int i = 0; i < NUM_RELAYS; i++)
    if (valveIsOpen[i]) return true;
  return false;
}

void valveWatchdog() {
  static unsigned long lastMs = 0;
  const unsigned long nowMs = millis();
  const unsigned long sinceLast = nowMs - lastMs;
  lastMs = nowMs;
  if (!anyValveOpen()) return;

  dailyOpenMs += sinceLast;
  if (dailyLimitReached()) {
    shutOff(nullptr);
    webPrint("SAFETY: valves open %lu min today, the daily limit -- all valves closed until tomorrow\n",
             dailyOpenMs / 60000);
    return;
  }

  unsigned long openFor = nowMs - valveOpenedMs;
  if (openFor > valveMaxOpenMs) {
    void* garb = nullptr;
    shutOff(garb);
    webPrint("SAFETY: valve open %lus, over its %lus limit -- all valves closed\n",
             openFor / 1000, valveMaxOpenMs / 1000);
  }
}

// Enabled-valve mask frozen at cycle start, so unticking a valve and saving
// mid-cycle can't shift the window offsets under the valve that is open.
static uint8_t cycleMask = 0xFF;

// Run time (seconds) a valve gets in a watering cycle: 0 if it is disabled.
static unsigned long cycleRuntime(int i) {
  return (cycleMask & (1 << i)) ? state.runtime[i] : 0;
}

// turn a specific relay on, all others off
// be aware that this function gets called multiple times, but should only run once per session
void relayOn(int relay_index) {

  if (valveIsOpen[relay_index] == true) return;  // only turn on if off
  if (dailyLimitReached()) {  // a cycle never gets here (startCycle() refuses); a manual test does
    webPrint("Daily watering limit reached: valve %d not opened\n", relay_index + 1);
    return;
  }
  zonesOff();  // not allOff(): the master valve stays open across a zone change

  // Arm the valve watchdog before opening: a cycle valve may stay open for its
  // scaled run time, a manual test for MANUAL_VALVE_MS; plus a minute's slack.
  // Never more than the hard cap, whatever the run time and scale say.
  unsigned long maxOpenMs = (state.runCycle ? cycleRuntime(relay_index) * state.temp_adjust : MANUAL_VALVE_MS) + 60000UL;
  if (maxOpenMs > MAX_VALVE_SECONDS * 1000UL) maxOpenMs = MAX_VALVE_SECONDS * 1000UL;
  valveMaxOpenMs = maxOpenMs;
  valveOpenedMs = millis();

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
  masterWrite(true);

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
#define OFFSET2 (OFFSET1 + cycleRuntime(0) * state.temp_adjust)
#define OFFSET3 (OFFSET2 + cycleRuntime(1) * state.temp_adjust)
#define OFFSET4 (OFFSET3 + cycleRuntime(2) * state.temp_adjust)
#define OFFSET5 (OFFSET4 + cycleRuntime(3) * state.temp_adjust)
#define OFFSET6 (OFFSET5 + cycleRuntime(4) * state.temp_adjust)
#define OFFSET7 (OFFSET6 + cycleRuntime(5) * state.temp_adjust)
#define OFFSET8 (OFFSET7 + cycleRuntime(6) * state.temp_adjust)
#define OFFSET9 (OFFSET8 + cycleRuntime(7) * state.temp_adjust)

// Calendar-day key (YYYYMMDD) of the last automatic cycle start, so a scheduled
// run fires at most once per day; it self-clears when the date rolls over.
// A latch plus the catch-up window (CATCHUP_MINUTES in config.h) replaces an
// exact-time match, so a run that can't start on time (reboot, power cut, the
// hour skipped when daylight saving starts) still starts when it can. The
// window ends at midnight: a run is only caught up on its own calendar day.
// Kept in flash ("lastRun") so a reboot inside the window doesn't water twice.
static long lastAutoRunDayKey = -1;
static bool latchLoaded = false;

static void setAutoRunLatch(long dayKey) {
  lastAutoRunDayKey = dayKey;
  latchLoaded = true;
  preferences.putLong("lastRun", dayKey);
}

// Called when the schedule is saved with a different start time. A new time
// still ahead today is allowed to fire today. One that has already passed
// waits for tomorrow: otherwise the catch-up window would start watering the
// moment the schedule was saved.
void resetAutoRunLatch() {
  time_t t = now();
  const bool stillAhead = state.runHour * 60 + state.runMinute > hour(t) * 60 + minute(t);
  setAutoRunLatch(stillAhead ? -1 : (long)year(t) * 10000 + month(t) * 100 + day(t));
}

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
  return 0.0023f * 0.408f * extraterrestrialRad(siteLat, doy) * (meanC + 17.8f) * sqrtf(rangeC);
}

static int dayOfYear(time_t t) {
  static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
  int y = year(t);
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return cum[month(t) - 1] + day(t) + ((leap && month(t) > 2) ? 1 : 0);
}

// Bit i set = the i-th newest dayBuffer sample came from Open-Meteo, not a
// sensor (both hold the last 24 hours). computeEtScale() won't run Hargreaves
// on those.
static uint32_t weatherSamples = 0;

// Which ET0 source scaled the last cycle: "open-meteo", "sensor", "none"
// (fell back to 100%) or "off" (scaling disabled). Reported in /api/status.
const char *etSource = "none";

// Run-time factor = yesterday's Open-Meteo Penman-Monteith ET0 / the active
// season's typical-day et0mm, when a fetch for yesterday succeeded. Otherwise
// Hargreaves ET0 over the last 24 hourly samples / ET0 of the season's typical
// day (ET_REFERENCE in config.h). Clamped either way. The sensor path falls
// back to 1.0 when the history is too short (just booted) or flat (dead sensor
// reads a constant 70F), since Hargreaves needs a real daily temperature swing.
static float clampScale(float scale) {
  if (scale < ET_SCALE_MIN) return ET_SCALE_MIN;
  if (scale > ET_SCALE_MAX) return ET_SCALE_MAX;
  return scale;
}

static float computeEtScale() {
  etSource = "none";  // until a source produces a factor
  const EtReference &ref = ET_REFERENCE[curSeason < 4 ? curSeason : 0];
  float mm;
  if (weatherEt0(mm)) {  // mm is range-checked and et0mm > 0, so the ratio is finite
    const float scale = clampScale(mm / ref.et0mm);
    webPrint("ET0 %d.%dmm [Open-Meteo Penman-Monteith] (ref %d.%d) -> %d%%\n", (int)(mm * 10) / 10, (int)(mm * 10) % 10,
             (int)(ref.et0mm * 10) / 10, (int)(ref.et0mm * 10) % 10, (int)(scale * 100));
    etSource = "open-meteo";
    return scale;
  }

  // Hargreaves is the sensor method: model temperatures don't count.
  if (tempFromWeather || weatherSamples != 0) {
    webPrint("ET scaling: temps from Open-Meteo, not a sensor, using 100%%\n");
    return 1.0f;
  }
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

  const float et0 = hargreavesET0(dayOfYear(now()), meanF, rangeF);
  const float etRef = hargreavesET0(ref.dayOfYear, ref.meanF, ref.rangeF);
  float scale = et0 / etRef;
  if (!isfinite(scale)) {  // NaN slips past the clamps below and would corrupt valve timing
    webPrint("ET scaling: invalid result, using 100%%\n");
    return 1.0f;
  }
  scale = clampScale(scale);

  // integer tenths: avoids relying on %f support in webPrint's vsnprintf
  webPrint("ET0 %d.%dmm [sensor Hargreaves] (ref %d.%d), Tmean %dF swing %dF -> %d%%\n",
           (int)(et0 * 10) / 10, (int)(et0 * 10) % 10, (int)(etRef * 10) / 10, (int)(etRef * 10) % 10,
           (int)meanF, (int)rangeF, (int)(scale * 100));
  etSource = "sensor";
  return scale;
}

// Safety timer callback: the cycle outran its computed length plus margin.
static bool cycleTimeout(void*) {
  shutOff(nullptr);
  webPrint("SAFETY: watering cycle overran, all valves closed\n");
  return false;
}

// Starts a watering cycle right now: computes temp_adjust from the current
// average temperature (or pins it to 1.0x if scaling is off), arms the
// cycle's safety timer (computed length + 5 min), and marks the cycle as running. Shared by
// the scheduled auto-trigger and the manual "Run Now" API so a manual run
// always gets a fresh scaling factor instead of reusing whatever a prior
// scheduled cycle last computed.
void startCycle() {
  if (state.valveEnabled == 0) {  // nothing to water; don't pretend a cycle ran
    webPrint("No valves enabled: watering cycle not started\n");
    return;
  }
  if (dailyLimitReached()) {
    webPrint("Daily watering limit reached: watering cycle not started\n");
    return;
  }
  cycleMask = state.valveEnabled;
  allOff();
  timer.cancel();  // cancel any manual operations
  state.start_time_ms = millis();

  // temp_adjust is the run-time factor x1000 (it doubles as the s->ms conversion)
  if (state.tempScaling) {
    state.temp_adjust = (uint32_t)(computeEtScale() * 1000.0f);
  } else {
    state.temp_adjust = 1000;  // 1.0x: run times used as-is
    etSource = "off";
    webPrint("Temperature scaling off: run times as set\n");
  }

  // Safety: force all valves off 5 min after the cycle should have ended.
  unsigned long cycleMs = 0;
  for (int i = 0; i < NUM_RELAYS; i++) cycleMs += cycleRuntime(i) * state.temp_adjust;
  timer.in(cycleMs + 300000UL, cycleTimeout);
  state.runCycle = true;
}

void controlRelays() {

  time_t t = now();                                                                           // Store the current time atomically
  long dayKey = (long)year(t) * 10000 + month(t) * 100 + day(t);  // unique per calendar day

  // New day: start the MAX_DAILY_SECONDS count again.
  static long dailyLimitDayKey = -1;
  if (dayKey != dailyLimitDayKey) {
    dailyLimitDayKey = dayKey;
    dailyOpenMs = 0;
  }

  if (state.wateringDisabled) {
    return;
  }

  if (!latchLoaded) {
    lastAutoRunDayKey = preferences.getLong("lastRun", -1);
    latchLoaded = true;
  }

  int nowMins = hour(t) * 60 + minute(t);
  int schedMins = state.runHour * 60 + state.runMinute;
  bool todayActive = state.activeDays & (1 << (weekday() - 1));  // weekday() is 1-7, Sunday=1

  // Trigger if it's an active day, we're within [schedule, schedule+catchup),
  // and we haven't already run today.
  bool inWindow = (nowMins >= schedMins) && (nowMins < schedMins + CATCHUP_MINUTES);
  // Not on a clock that has never been set (it would count from 1970).
  if (clockIsSet() && todayActive && inWindow && dayKey != lastAutoRunDayKey && state.runCycle == false) {  // trigger start of cycle
    setAutoRunLatch(dayKey);  // set first: a rain-skipped day isn't retried
    if (!recentRainSkip()) startCycle();
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
    weatherSamples = ((weatherSamples << 1) | (tempFromWeather ? 1 : 0)) & 0xFFFFFFUL;

    state.avg_temp = 0;
    // // the following ensures using the right type for the index variable
    using index_t = decltype(dayBuffer)::index_t;

    for (index_t i = 0; i < dayBuffer.size(); i++) {  // compute 24 hour temp
      state.avg_temp += dayBuffer[i];
    }
    state.avg_temp = state.avg_temp / dayBuffer.size();

  } else if (minute(t) == 0 && second(t) > 0) hourlySampleTaken = false;  // clear for run next hour
}
