#include "time_manager.h"
#include "sprinky.h"
#include <WiFiUdp.h>

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

// India Standard Time (Kolkata, Mumbai, Delhi) - no DST, UTC + 5:30
TimeChangeRule istRule = {"IST", Last, Sun, Mar, 1, 330};
Timezone tzIST(istRule);

// Japan Standard Time (Tokyo) - no DST, UTC + 9
TimeChangeRule jstRule = {"JST", Last, Sun, Mar, 1, 540};
Timezone tzJST(jstRule);

// China Standard Time (Beijing, Shanghai) - no DST, UTC + 8
TimeChangeRule cnstRule = {"CNST", Last, Sun, Mar, 1, 480};
Timezone tzCN(cnstRule);

// Gulf Standard Time (Dubai, Abu Dhabi) - no DST, UTC + 4
TimeChangeRule gstRule = {"GST", Last, Sun, Mar, 1, 240};
Timezone tzGST(gstRule);

// South Africa Standard Time (Johannesburg) - no DST, UTC + 2
TimeChangeRule sastRule = {"SAST", Last, Sun, Mar, 1, 120};
Timezone tzSAST(sastRule);

// Brazil (Sao Paulo) - no DST since 2019, UTC - 3
TimeChangeRule brtRule = {"BRT", Last, Sun, Mar, 1, -180};
Timezone tzBRT(brtRule);

Timezone *tz = &UTC;

// --- Global Days array ---
String Days[] = {"Undefined", "Sunday", "Monday", "Tuesday",
                 "Wednesday", "Thursday", "Friday", "Saturday"};

// --- Helper functions ---

// Return time zone and DST adjusted time from server
time_t currentLocalTime(void) {
  time_t serv_time = tz->toLocal(timeClient.getEpochTime());
  return (serv_time);
}

String tzName() {
  if (tz == &ausET)
    return "Australia Eastern Time";
  else if (tz == &tzMSK)
    return "Moscow Time";
  else if (tz == &CE)
    return "Central European Time";
  else if (tz == &UK)
    return "British Standard Time";
  else if (tz == &UTC)
    return "Universal Time";
  else if (tz == &usET)
    return "Eastern Standard Time";
  else if (tz == &usCT)
    return "Central Standard Time";
  else if (tz == &usMT)
    return "Mountain Standard Time";
  else if (tz == &usAZ)
    return "Arizona Time";
  else if (tz == &usPT)
    return "Pacific Standard Time";
  else if (tz == &tzIST)
    return "India Standard Time";
  else if (tz == &tzJST)
    return "Japan Standard Time";
  else if (tz == &tzCN)
    return "China Standard Time";
  else if (tz == &tzGST)
    return "Gulf Standard Time";
  else if (tz == &tzSAST)
    return "South Africa Standard Time";
  else if (tz == &tzBRT)
    return "Brasilia Time";
  else
    return "Unknown TZ Time";
}

String tzCode() {
  if (tz == &ausET) return "AEST";
  else if (tz == &tzMSK) return "MSK";
  else if (tz == &CE) return "CE";
  else if (tz == &UK) return "GMT";
  else if (tz == &UTC) return "UTC";
  else if (tz == &usET) return "EST";
  else if (tz == &usCT) return "CST";
  else if (tz == &usMT) return "MST";
  else if (tz == &usAZ) return "AZT";
  else if (tz == &usPT) return "PST";
  else if (tz == &tzIST) return "IST";
  else if (tz == &tzJST) return "JST";
  else if (tz == &tzCN) return "CNST";
  else if (tz == &tzGST) return "GST";
  else if (tz == &tzSAST) return "SAST";
  else if (tz == &tzBRT) return "BRT";
  else return "UTC";
}

void printTZ() {
  Serial.println(tzName());
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
  else if (tzstring == "IST")
    return (&tzIST);
  else if (tzstring == "JST")
    return (&tzJST);
  else if (tzstring == "CNST")
    return (&tzCN);
  else if (tzstring == "GST")
    return (&tzGST);
  else if (tzstring == "SAST")
    return (&tzSAST);
  else if (tzstring == "BRT")
    return (&tzBRT);
  else {
    Serial.println("Bad TZ selection");
    return (&UTC);
  }
}
