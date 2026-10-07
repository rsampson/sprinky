#pragma once

// Open-Meteo daily reference evapotranspiration (FAO-56 Penman-Monteith).
void updateWeather();          // once/second from loop(): fetch yesterday's ET0 when due
bool weatherEt0(float &mm);    // true (and mm set) only if the cached value is for yesterday
