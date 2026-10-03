# Open-Meteo ET0 for run-time scaling — design

Date: 2026-10-03
Status: approved design, awaiting implementation plan

## Context

Run-time scaling today (`computeEtScale()` in `src/relay.cpp`) estimates FAO-56
Hargreaves ET0 from the last 24 hourly local temperature samples, divided by the
Hargreaves ET0 of the active season's typical day (`ET_REFERENCE` in
`src/config.h`). Hargreaves uses temperature only, so it misjudges humid,
overcast or windy days. It also falls back to 100% on boards without a
working sensor.

Open-Meteo publishes a daily FAO-56 **Penman-Monteith** ET0
(`et0_fao_evapotranspiration`) for any coordinate, free and with no API key.
Penman-Monteith accounts for radiation, humidity and wind as well as
temperature.

**Goal:** make run-time scaling more accurate by using Open-Meteo ET0 when
it is available, keeping the existing sensor path as a fallback.

## Decisions

| Topic | Decision |
|---|---|
| Source priority | Open-Meteo, then local-sensor Hargreaves (existing code, unchanged), then 100%. The source used is logged on every run. |
| ET0 window | Yesterday's daily ET0, replacing what was lost on the last full day. Request: `past_days=1&forecast_days=0`. |
| Location | New `LONGITUDE_DEG` compile-time constant in `config.h`, next to `LATITUDE_DEG`. |
| Fetch mechanism | Synchronous `HTTPClient` over **plain HTTP**, 4 s timeout, scheduled outside watering cycles. |
| Reference denominator | New per-season `et0mm` (mm/day) field in `ET_REFERENCE`. The web source divides Penman-Monteith by a Penman-Monteith reference, so the method bias between Hargreaves and Penman-Monteith (often 10–25%) does not enter the ratio. |
| Persistence | RAM only. The device fetches again after a reboot. |
| On/off | The existing `tempScaling` toggle still gates all scaling. Off means 1.0×, as today. |
| UI / API | `etSource` added to `GET /api/status`. No dashboard change. |

## Verified facts (2026-10-03)

- `http://api.open-meteo.com/v1/forecast?...` answers **200 over plain HTTP with
  no redirect**, so TLS is not needed on the ESP8266.
- The response uses `Transfer-Encoding: chunked` and is about 350 bytes. Read it with
  `HTTPClient::getString()`, which de-chunks it, rather than parsing the raw stream.
- Response shape:
  ```json
  {"latitude":32.99596,"longitude":-116.992935, ...,
   "daily_units":{"time":"iso8601","et0_fao_evapotranspiration":"mm"},
   "daily":{"time":["2026-10-02"],"et0_fao_evapotranspiration":[8.91]}}
  ```
  With `timezone=auto`, `daily.time[0]` is the local calendar date of the
  value.

## Design

### New module: `src/weather.cpp` / `src/weather.h`

Interface:

- `void updateWeather()` is called from the once-per-second housekeeping in
  `loop()` (`src/sprinky.cpp`), next to `updateAutoSeason()`. It decides
  whether a fetch is due and performs it.
- `bool weatherEt0(float &mm)` returns `true` and sets `mm` only when the
  cached value is dated exactly **yesterday** (local date). Otherwise it returns
  `false`.

`updateWeather()` fetches only when **all** of these hold:

1. `WiFi.status() == WL_CONNECTED` and `!ap_mode`.
2. `year(now()) >= 2024`, the same NTP-synced test `updateAutoSeason()` uses.
3. `!state.runCycle` and no valve is open, so a blocking fetch can never delay valve sequencing.
4. The cached value is not already dated yesterday.
5. Local hour ≥ 1, or no attempt has been made since boot. The 01:00 rule gives the previous
   day's value time to settle.
6. At least 1 h since the last attempt (retry spacing).

Fetch:

```
http://api.open-meteo.com/v1/forecast?latitude=<LATITUDE_DEG>&longitude=<LONGITUDE_DEG>
  &daily=et0_fao_evapotranspiration&past_days=1&forecast_days=0&timezone=auto
```

- Use `HTTPClient` (`ESP8266HTTPClient` on ESP8266 with a `WiFiClient`, `HTTPClient` on
  ESP32) with `setTimeout(4000)`, and read the body with `getString()`.
- Parse with ArduinoJson 6 (already a dependency), using a filter document that keeps only
  `daily.time[0]` and `daily.et0_fao_evapotranspiration[0]`.
- Convert `"YYYY-MM-DD"` to a `long` YYYYMMDD key, the same style as
  `lastAutoRunDayKey` in `relay.cpp`. Compare it with yesterday's key computed from
  `now() - 86400`.
- Accept the value only if it is finite and 0 ≤ mm ≤ 20.

Logging uses `webPrint`, with integer tenths rather than `%f`, as elsewhere:

- Success: `Weather: ET0 8.9mm for 2026-10-02`
- Failure: `Weather: fetch failed (HTTP <code>), retry in 1h`, or
  `Weather: bad response (<reason>), retry in 1h`

### `src/relay.cpp` — `computeEtScale()`

- If `weatherEt0(mm)` succeeds: `scale = mm / ET_REFERENCE[season].et0mm`,
  followed by the existing `isfinite` guard and the `ET_SCALE_MIN`/`ET_SCALE_MAX` clamps.
  Log `ET0 8.9mm [open-meteo] (ref 3.4) -> 200%`. The source is `"open-meteo"`.
- Otherwise the existing Hargreaves path runs unchanged, except that its log
  line gets a `[sensor]` tag. The source is `"sensor"`. Its existing 100% fallbacks
  (fewer than 12 samples, swing under 2°F, non-finite result) set the source to `"none"`.
- When `tempScaling` is off, the source is `"off"`.
- The source is held in a variable that the `/api/status` handler reads. It is set at each
  cycle start, so it describes the last run.

### `src/config.h`

- `constexpr float LONGITUDE_DEG = -117.0f;` is a placeholder matching the existing
  `LATITUDE_DEG = 33.0f`, with a comment telling the user to set both for their
  site.
- Add `float et0mm;` as a fourth field of `EtReference`. Starting values are the 2025
  Open-Meteo monthly means at 33 N, 117 W for each season's mid-month:

  | Season | Month | `et0mm` |
  |---|---|---|
  | Summer | Jul | 5.7 |
  | Fall | Oct | 3.4 |
  | Winter | Jan | 3.0 |
  | Spring | Apr | 4.0 |

  The comment should tell users to replace these with their own site's
  monthly means. They can get those from the Open-Meteo archive API
  (`archive-api.open-meteo.com/v1/archive?...&daily=et0_fao_evapotranspiration`)
  or by tuning from the logged `ET0 ... [open-meteo]` lines.

### `src/web_server.cpp`

- Add `doc["etSource"]` to the `/api/status` document. Its value is one of `"open-meteo"`, `"sensor"`,
  `"none"` or `"off"`.

## Timing and failure handling

- `loop()` can block for at most about 4 s, and only when no cycle is running and no valve
  is open, so `valveWatchdog()` and the safety timer are unaffected. HTTPClient
  yields while waiting, which keeps the ESP8266 watchdog fed. If a fetch overlaps
  the scheduled start minute, the existing 5-minute catch-up window still
  starts the cycle.
- A non-200 status, a JSON parse error, a missing or null field, or an out-of-range value is
  logged and treated as a miss. The next attempt comes 1 h later. A day uses at most
  about 24 calls, far under Open-Meteo's free-tier limit.
- A cached value is used only if it is dated yesterday, so a fetch that keeps failing
  for days can never keep scaling by an old value. Each such run falls back to the
  sensor path.
- If the run time is before 01:00, that day's run uses the sensor fallback, because yesterday's value
  isn't fetched until 01:00. The log makes this visible.
- Extreme days are bounded by the existing clamps. For example, 8.9 mm against a 3.4 mm Fall reference
  is clamped to 2.0×.

## Out of scope

- Rain or precipitation-based skipping.
- Forecast-based (look-ahead) scaling.
- Configuring the location at runtime or from the dashboard.
- HTTPS/TLS.
- Dashboard UI changes beyond what `/api/status` already feeds the log.

## Files

| File | Change |
|---|---|
| `src/weather.cpp` (new) | Fetch, parse, cache, scheduling |
| `src/weather.h` (new) | `updateWeather()`, `weatherEt0()` |
| `src/sprinky.cpp` | Call `updateWeather()` in the 1 s housekeeping |
| `src/relay.cpp` | `computeEtScale()` prefers Open-Meteo and records and tags the source |
| `src/config.h` | `LONGITUDE_DEG`, `EtReference::et0mm` and its values |
| `src/web_server.cpp` | `etSource` in `/api/status` |
| `src/CLAUDE.md` | Document the module, the config fields and the API field |
| `README.md` | **OPTIONAL**: a note in "Temperature-adjusted watering" |
| `platformio.ini` | No change expected; the HTTP client ships with both cores |

## Verification

There is no unit-test suite. Verification is:

1. `pio run -e esp12e` and `pio run -e esp32dev` both succeed.
2. Flash the esp12e test board. The Status log shows `Weather: ET0 …mm for <yesterday>`.
   **Run Now** shows an `ET0 … [open-meteo]` line, and `/api/status` has
   `"etSource":"open-meteo"`.
3. Fallback check: temporarily point the host at an unreachable name, rebuild, flash,
   and Run Now. Expect a fetch-failed line, then a `[sensor]` attempt, then 100% (the test board
   reads a flat 70°F) with `"etSource":"none"`. Then revert.
4. Log free heap before and after a fetch on the ESP8266. It must return to its
   baseline, with no lasting loss.
