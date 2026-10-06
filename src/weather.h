#pragma once

// Open-Meteo daily reference evapotranspiration (FAO-56 Penman-Monteith).
void updateWeather();          // once/second from loop(): fetch yesterday's ET0 when due
bool weatherEt0(float &mm);    // true (and mm set) only if the cached value is for yesterday
bool recentRainSkip();         // blocking: true if recent rain meets a RAIN_SKIP tier

// Site location (degrees, north/east positive): entered on the Setup page, else
// looked up from the public IP address, else LATITUDE_DEG/LONGITUDE_DEG.
extern float siteLat;
extern float siteLon;
const char *siteLocSource();   // "manual", "auto" or "default"
void loadSiteLocation();       // from Preferences, at boot
// From web handlers: queued, then applied by updateWeather() in loop().
void requestManualLocation(float lat, float lon);
void requestAutoLocation();
