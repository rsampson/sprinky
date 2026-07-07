#include "sprinky.h"
#include <ESPUI.h>
#include <TimeLib.h>


void relayConfig() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(relay[i], OUTPUT);
  }
}

bool relayEnabled[NUM_RELAYS];

void allOff() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    digitalWrite(relay[i], OFF);
    relayEnabled[i] = false;
  }
}

bool shutOff(void*) {  // bool return and void* makes timer api happy
  allOff();
  timer.cancel();
  // reset button color to indicate inactivity
  sprintf(stylecol2, "background-color: silver;");
  for (int i = 0; i < NUM_RELAYS; i++) {
    ESPUI.setElementStyle(ui.buttons[i], stylecol2);
  }
  Serial.println("timer shut off ");
  return (false);
}

// turn a specific relay on, all others off
// be aware that this function gets called multiple times, but should only run once per session
void relayOn(int relay_index) {

  if (relayEnabled[relay_index] == true) return;  // only turn on if off
  allOff();

  // "buzz" relay to clear jammed valve
  for (int j = 0; j < 3; j++) {
    digitalWrite(relay[relay_index], ON);
    delay(20);
    digitalWrite(relay[relay_index], OFF);
    delay(20);
  }

  for (int i = 0; i < NUM_RELAYS; i++) {  // make sure only one relay is on at a time
    // turn relay on, all others off
    digitalWrite(relay[i], (i == relay_index) ? ON : OFF);
    sprintf(stylecol2, (i == relay_index) ? "background-color: lime;" :  "background-color: silver;" );
    ESPUI.setElementStyle(ui.buttons[i], stylecol2); // animate button
    relayEnabled[i] = (i == relay_index);
  }
  // turn on last valve as a master safety valve
  //digitalWrite(relay[NUM_RELAYS - 1], ON);
    
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

void controlRelays() {

  if (state.disable) {
    return;
  }

  time_t t = now();                                                                           // Store the current time atomically
  if (hour(t) == state.runHour && minute(t) == state.runMinute && second(t) == 0 && state.runCycle == false) {  // trigger start of cycle
    allOff();
    timer.cancel();  // cancel any manual operations
    state.start_time_ms = millis();

    //expand watering time .3 to 3x over a 40-90 average degree temp range, map it into milli seconds
    state.temp_adjust = map((int32_t)state.avg_temp, 40, 90, 300, 3000);
    state.runCycle = true;
  }

  if (state.runCycle == true) {  // run watering cycle if is time

    timer.in(4800000, shutOff);  // for safety, turn off automatically after 80 min
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
      String totalRunTime = String((millis() - state.start_time_ms) /60000);
      ESPUI.updateLabel(ui.runtimeLabel, totalRunTime + " minutes");
      webPrint("Run times scaled by %2d percent\n", state.temp_adjust / 10);
      state.runCycle = false;
    }
  }
}

bool haveRan = false;
// compute average temperature
void ComputeAveTemp(void) {

  time_t t = now();                                            // Store the current time atomically
  if (minute(t) == 0 && second(t) == 0 && haveRan == false) {  // do once each hour
    haveRan = true;

    // samples temp and computes the average of the last 24 hours
    dayBuffer.push(getTempF());

    state.avg_temp = 0;
    // // the following ensures using the right type for the index variable
    using index_t = decltype(dayBuffer)::index_t;

    for (index_t i = 0; i < dayBuffer.size(); i++) {  // compute 24 hour temp
      state.avg_temp += dayBuffer[i];
    }
    state.avg_temp = state.avg_temp / dayBuffer.size();

    // report average temp and run time scaling adjustment
    ESPUI.updateLabel(ui.aveTempLabel, "24 hour average temperature: " + String(state.avg_temp) + " F");
    

  } else if (minute(t) == 0 && second(t) > 0) haveRan = false;  // clear for run next hour
}
