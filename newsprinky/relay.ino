
// ********** RELAY STUFF *****************
//  8 Relay board info can be found at:
//  https://www.amazon.com/Channel-Module-Supply-Stable-Development/dp/B0C4PGB5V1
//  https://www.aliexpress.us/item/3256805488891875.html?gatewayAdapt=glo2usa4itemAdapt
//  https://devices.esphome.io/devices/ESP32E-Relay-X8
//  https://www.reddit.com/r/esp32/comments/1czys44/unable_to_program_esp32wroom32e_relay_board/

#define RELAY8
#ifdef  RELAY8     // if using a board with 8 relays
//int relay[8] = {16, 5, 4, 0, 2, 14, 12, 13}; // this is the output gpio pin ordering
// esp 32 relay ---- GPIO32, GPIO33, GPIO25, GPIO26, GPIO27, GPIO14, GPIO12 and GPIO13
int relay[8] = {32, 33, 25, 26, 27, 14, 12, 13};
#define ON HIGH
#define OFF LOW
#else              // else using board with 4 relays, data at:

// https://xpart.org/how-to-use-the-dc-12v-esp8266-wifi-4-channel-relay-module-for-remote-control/
int relay[4] = {16, 14, 12, 13};
#define ON HIGH
#define OFF LOW
#endif


void allOff() {
  int i;
  for (i = 0; i < sizeof relay / sizeof relay[0]; i++) {
    digitalWrite(relay[i], OFF);
  }
}

bool shutOff(void *) {  // make timer api happy
  allOff();
  timer.cancel();
  manualOp = false;
  Serial.println("timer shut off ");
  return (false);
}

void relayConfig( ) {
  int i;
  for (i = 0; i < sizeof relay / sizeof relay[0]; i++) {
    pinMode(relay[i], OUTPUT);
  }
}

unsigned long current_time_ms = 0;

#define START1  current_time_ms
#define START2  (START1 + runtime[0] * 1000)
#define START3  (START2 + runtime[1] * 1000)
#define START4  (START3 + runtime[2] * 1000)
#define START5  (START4 + runtime[3] * 1000)
#define START6  (START5 + runtime[4] * 1000)
#define START7  (START6 + runtime[5] * 1000)
#define START8  (START7 + runtime[6] * 1000)

// turn relay rl on, all others off
void relayOn(int rl) {
  if (digitalRead(relay[rl]) == OFF && !disable) {   // only turn ON  if it is currently OFF and not disabled

    webPrint("Valve %1d on %s @ %2d:%2d for %3d sec \n", rl + 1,  Days[weekday()], hour(), minute(), runtime[rl] );

    int i;
    for (i = 0; i < sizeof relay / sizeof relay[0]; i++) { // make sure only one relay is on at a time
      digitalWrite(relay[i], i == rl ? ON : OFF);
    }
  }
}

// do not modify these definitions
#define SUNDAY    1
#define MONDAY    2
#define TUESDAY   3
#define WEDNESDAY 4
#define THURSDAY  5
#define FRIDAY    6
#define SATURDAY  7

void controlRelays() {
  time_t t = now(); // Store the current time atomically
 
  if (!manualOp) {  // if not being operated manually
    if (hour(t) == runHour) {
      if (minute(t) < runMinute) {
        allOff();  // it's to soon in the hour !!!!!! fix reboot during the hour
        current_time_ms = millis();
      }
      else if (millis() > START1 && millis() < START2) relayOn(0);
      else if (millis() > START2 && millis() < START3) relayOn(1);
      else if (millis() > START3 && millis() < START4) relayOn(2);
      else if (millis() > START4 && millis() < START5) relayOn(3);
#ifdef RELAY8
      else if (millis() > START5 && millis() < START6) relayOn(4);
      else if (millis() > START6 && millis() < START7) relayOn(5);
      else if (millis() > START7 && millis() < START8) relayOn(6);
      else if (millis() > START8 && millis() < START8 + runtime[7]) relayOn(7);
#endif
      else { 
        allOff();
      }
    }  // skip to here if not run hour
  }  // skip to here if manual

  // measure temperature at 2 o'clock noon to adjust watering times
//  if (hour(t) == 14 && minute(t) == 10 && second() == 2) {
//    // expand watering time to 2x over a 40-110 degree temp range
//    unsigned long temperature_adjustment = map(getTempF(), 40, 110, 100, 200);
//
//    webPrint("Watering increased %2d percent", temperature_adjustment);
//    runtime[0] = (DURATION1 * temperature_adjustment) / 100;
//    runtime[1] = (DURATION2 * temperature_adjustment) / 100;
//    runtime[2] = (DURATION3 * temperature_adjustment) / 100;
//    runtime[3] = (DURATION4 * temperature_adjustment) / 100;
//    runtime[4] = (DURATION5 * temperature_adjustment) / 100;
//    runtime[5] = (DURATION6 * temperature_adjustment) / 100;
//    runtime[6] = (DURATION7 * temperature_adjustment) / 100;
//    runtime[7] = (DURATION8 * temperature_adjustment) / 100;
//  }

}
