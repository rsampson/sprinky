#pragma once

#include <Arduino.h>
#include <NTPClient.h>
#include <Timezone.h>
#include <TimeLib.h>

// --- NTP Client ---
extern NTPClient timeClient;

// --- Timezone declarations ---
extern Timezone *tz;
extern Timezone ausET;
extern Timezone tzMSK;
extern Timezone CE;
extern Timezone UK;
extern Timezone UTC;
extern Timezone usET;
extern Timezone usCT;
extern Timezone usMT;
extern Timezone usAZ;
extern Timezone usPT;
extern Timezone tzIST;
extern Timezone tzJST;
extern Timezone tzCN;
extern Timezone tzGST;
extern Timezone tzSAST;
extern Timezone tzBRT;

extern String Days[];

// --- Function Prototypes ---
void printTZ();
Timezone *TZstringToPointer(String tzstring);
String tzName();
String tzCode();
time_t currentLocalTime();

// --- Clock source ---
// The clock runs from NTP when there is one. Until then it starts from the
// time last saved to flash (saved hourly, so after a power cut it is late by
// about the length of the outage), or from a browser that has the dashboard
// open. With none of these it is unset and the schedule doesn't run.
void initClock();            // setup(): NTP if already available, else the saved time
void clockService();         // once/second from loop(): adopt NTP / browser time, save hourly
const char *clockSource();   // "ntp", "browser", "restored" or "unset"
bool clockIsSet();
bool requestBrowserTime(uint32_t utcEpoch);  // from a web handler; false if implausible
