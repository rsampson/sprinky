#include "time_manager.h"
#include "sprinky.h"
#include <WiFiUdp.h>
#include <ESPUI.h>

// --- UDP and NTP Client instances ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// --- Timezone Rules and Definitions ---

// Australia Eastern Time Zone (Sydney, Melbourne)
TimeChangeRule aEDT = {"AEDT", First, Sun, Oct, 2, 660}; // UTC + 11 hours
TimeChangeRule aEST = {"AEST", First, Sun, Apr, 3, 600}; // UTC + 10 hours
Timezone ausET(aEDT, aEST);

// Moscow Standard Time (MSK, does not observe DST)
TimeChangeRule msk = {"MSK", Last, Sun, Mar, 1, 180};
Timezone tzMSK(msk);

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};  // Central European Standard Time
Timezone CE(CEST, CET);

// United Kingdom (London, Belfast)
TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60}; // British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};  // Standard Time
Timezone UK(BST, GMT);

// UTC
TimeChangeRule utcRule = {"UTC", Last, Sun, Mar, 1, 0}; // UTC
Timezone UTC(utcRule);

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240}; // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};  // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

// US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};
TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};
Timezone usCT(usCDT, usCST);

// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};
TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};
Timezone usMT(usMDT, usMST);

// Arizona is US Mountain Time Zone but does not use DST
Timezone usAZ(usMST);

// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};
Timezone usPT(usPDT, usPST);

Timezone *tz = &UTC;

// --- Global Days array ---
String Days[] = {"Undefined", "Sunday", "Monday", "Tuesday",
                 "Wednesday", "Thursday", "Friday", "Saturday"};

// --- Helper functions ---

// Return time zone and DST adjusted time from server
time_t getNtpTime(void) {
  time_t serv_time = tz->toLocal(timeClient.getEpochTime());
  return (serv_time);
}

void printTZ(Timezone *tzone) {
  String TZS;

  if (tzone == &ausET)
    TZS = "Australia Eastern";
  else if (tzone == &tzMSK)
    TZS = "Moscow";
  else if (tzone == &CE)
    TZS = "Central European";
  else if (tzone == &UK)
    TZS = "British Standard";
  else if (tzone == &UTC)
    TZS = "Universal";
  else if (tzone == &usET)
    TZS = "Eastern Standard";
  else if (tzone == &usCT)
    TZS = "Central Standard";
  else if (tzone == &usMT)
    TZS = "Mountain Standard";
  else if (tzone == &usAZ)
    TZS = "Arizona";
  else if (tzone == &usPT)
    TZS = "Pacific Standard";
  else
    TZS = "Unknown TZ";

  TZS = TZS + " Time";
  Serial.println(TZS);
  ESPUI.updateControlValue(ui.timeZoneLabel, TZS);
}

Timezone *TZstringToPointer(String tzstring) {
  if (tzstring == "AEST")
    return (&ausET);
  else if (tzstring == "MSK")
    return (&tzMSK);
  else if (tzstring == "CE")
    return (&CE);
  else if (tzstring == "GMT")
    return (&UK);
  else if (tzstring == "UTC")
    return (&UTC);
  else if (tzstring == "EST")
    return (&usET);
  else if (tzstring == "CST")
    return (&usCT);
  else if (tzstring == "MST")
    return (&usMT);
  else if (tzstring == "AZT")
    return (&usAZ);
  else if (tzstring == "PST")
    return (&usPT);
  else {
    Serial.println("Bad TZ selection");
    return (&UTC);
  }
}

void displayTime(void) {
  char buf1[20];
  time_t t = now();
  sprintf(buf1, "%02d:%02d:%02d %02d/%02d", hour(t), minute(t), second(t),
          month(t), day(t));
  ESPUI.updateLabel(ui.timeLabel, buf1);
}
