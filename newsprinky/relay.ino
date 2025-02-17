
// ********** RELAY STUFF *****************
//  8 Relay board info can be found at:
//  https://www.amazon.com/Channel-Module-Supply-Stable-Development/dp/B0C4PGB5V1
//  https://www.aliexpress.us/item/3256805488891875.html?gatewayAdapt=glo2usa4itemAdapt
//  https://devices.esphome.io/devices/ESP32E-Relay-X8
//  https://www.reddit.com/r/esp32/comments/1czys44/unable_to_program_esp32wroom32e_relay_board/
// for 4 relay board: // https://xpart.org/how-to-use-the-dc-12v-esp8266-wifi-4-channel-relay-module-for-remote-control/
#define ON HIGH
#define OFF LOW

#ifdef  RELAY8     // if using a board with 8 relays
// esp 32 relay ---- GPIO32, GPIO33, GPIO25, GPIO26, GPIO27, GPIO14, GPIO12 and GPIO13
int relay[8] = {32, 33, 25, 26, 27, 14, 12, 13};
#else              // else using board with 4 relays, data at:
int relay[4] = {16, 14, 12, 13};
#endif

#ifdef RELAY8
#define NUM_RELAYS 8
#elif
#define NUM_RELAYS 4
#endif


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
  Serial.println("timer shut off ");
  return (false);
}

// turn a specific relay on, all others off
void relayOn(int relay_index) {

  if(relayEnabled[relay_index] == true) return; // only turn on if off

  for (int i = 0; i < NUM_RELAYS; i++)     { // make sure only one relay is on at a time
    digitalWrite(relay[i], (i == relay_index) ? ON: OFF);
    relayEnabled[i] = (i == relay_index);
  }
  
   webPrint("Valve %1d on %s @ %s \n", relay_index + 1,  Days[weekday()], timeClient.getFormattedTime());
}

uint32_t start_time_ms = 0;
uint32_t temp_adjust = 1000; // scaled ms in second

// runtimes are in seconds, start times are in ms
// temp_adjust has the sec to ms conversion factored in (temp adjust is in ms)
#define START1  start_time_ms
#define START2  (START1 + runtime[0] * temp_adjust)
#define START3  (START2 + runtime[1] * temp_adjust)
#define START4  (START3 + runtime[2] * temp_adjust)
#define START5  (START4 + runtime[3] * temp_adjust)
#define START6  (START5 + runtime[4] * temp_adjust)
#define START7  (START6 + runtime[5] * temp_adjust)
#define START8  (START7 + runtime[6] * temp_adjust)
#define START9  (START8 + runtime[7] * temp_adjust)


bool runCycle = false;

void controlRelays() {

  if (disable)
  {
    return;
  }

  time_t t = now(); // Store the current time atomically
  if (hour(t) == runHour && minute(t) == runMinute && second(t) == 0 && runCycle == false) {  // trigger start of cycle
    allOff();
    timer.cancel(); // cancel any manual operations
    start_time_ms = millis();
    runCycle = true;
  }

  if (runCycle == true) {      // run watering cycle if is time
    if      (millis() >= START1 && millis() < START2) relayOn(0);
    else if (millis() >= START2 && millis() < START3) relayOn(1);
    else if (millis() >= START3 && millis() < START4) relayOn(2);
    else if (millis() >= START4 && millis() < START5) relayOn(3);
#ifdef RELAY8
    else if (millis() >= START5 && millis() < START6) relayOn(4);
    else if (millis() >= START6 && millis() < START7) relayOn(5);
    else if (millis() >= START7 && millis() < START8) relayOn(6);
    else if (millis() >= START8 && millis() < START9) relayOn(7);
#endif
    else {         // terminate cycle
      allOff();
      // print statistics
      static String totalRunTime  = String (float(millis() - start_time_ms) / 60000);
      ESPUI.updateLabel(runtimeLabel, totalRunTime + " minutes");
      webPrint("Daily total run time is %s minutes\n", totalRunTime);
      runCycle = false;
    }
  }
}


// compute an adjustment to run time based on average temperature
void tempAdjRunTime(void) {
  static bool haveRun = false;
  
  time_t t = now(); // Store the current time atomically
  if (minute(t) == 0 && second(t) == 0 && haveRun == false) { // do once each hour
     haveRun = true;

    // samples temp and computes the average of the last 24 hours
    dayBuffer.push(getTempF());

    avg_temp = 0;
    // // the following ensures using the right type for the index variable
    using index_t = decltype(dayBuffer)::index_t;
    for (index_t i = 0; i < dayBuffer.size(); i++) {
      avg_temp += dayBuffer[i];
    }
    avg_temp = avg_temp / dayBuffer.size();
    
    //expand watering time 0 to 2x over a 35-110 degree temp range
    //map it into milli seconds
    temp_adjust = map((int32_t)avg_temp, 35, 110, 1, 2000);

  // report average temp and run time scaling adjustment
    ESPUI.updateLabel(aveTempLabel,  "24 hour average temperature: "  + String(avg_temp) + " F");
    webPrint("Average 24 hour temperature is %3.1f \n", avg_temp);
    webPrint("Run times scaled by %2d percent\n", temp_adjust / 10);
  }
  if (minute(t) == 0 && second(t) >= 1) haveRun = false;  // clear for run next hour
}
