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

extern String Days[];

// --- Function Prototypes ---
void printTZ();
Timezone *TZstringToPointer(String tzstring);
String tzName();
String tzCode();
time_t getNtpTime();
