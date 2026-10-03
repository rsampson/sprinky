# Open-Meteo ET0 Scaling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scale valve run times by yesterday's Open-Meteo Penman-Monteith ET0. If that value is unavailable, fall back to the existing local-sensor Hargreaves path, and then to 100%.

**Architecture:** A new `weather.cpp` module fetches and caches one daily ET0 value. It uses a blocking `HTTPClient` over plain HTTP, called from `loop()`'s 1 s housekeeping, and fetches only when no valve is open. `computeEtScale()` in `relay.cpp` uses that value when it is dated yesterday; otherwise it runs the unchanged Hargreaves code. The source used is exposed as `etSource` in `/api/status`.

**Tech Stack:** PlatformIO, Arduino cores for ESP8266 (`esp12e`) and ESP32 (`esp32dev`), `ESP8266HTTPClient` / `HTTPClient` (bundled with the cores), ArduinoJson 6.21.5 (already pinned), TimeLib.

**Spec:** `docs/superpowers/specs/2026-10-03-open-meteo-et0-design.md`

## Global Constraints

- Both `pio run -e esp12e` and `pio run -e esp32dev` must succeed after every task. There is no unit-test suite; verification means compiling both targets and observing the device (see `src/CLAUDE.md`).
- Plain HTTP only. Do not add TLS or BearSSL.
- Request URL: `http://api.open-meteo.com/v1/forecast?latitude=<LATITUDE_DEG>&longitude=<LONGITUDE_DEG>&daily=et0_fao_evapotranspiration&past_days=1&forecast_days=0&timezone=auto`
- HTTP timeout is 4000 ms. Read the body with `getString()`, because the response is chunked.
- Accept a value only if it is present (not JSON `null`), finite, and 0 ≤ mm ≤ 20.
- Retry spacing is 1 h. Daily fetches start at local hour ≥ 1, except for the first attempt after boot.
- Fetch only when WiFi is `WL_CONNECTED`, `!ap_mode`, `year(now()) >= 2024`, `!state.runCycle`, and no entry of `valveIsOpen[]` is true.
- The cache lives in RAM only. Add no NVS keys.
- `webPrint` logging uses integer tenths, never `%f`.
- `etSource` values are exactly `"open-meteo"`, `"sensor"`, `"none"` or `"off"`.
- `et0mm` values are Summer 5.7, Fall 3.4, Winter 3.0, Spring 4.0. `LONGITUDE_DEG = -117.0f`.
- Match the existing style: C-style free functions, shared globals through `sprinky.h`, comment density like `relay.cpp`. Make surgical changes only.

## Review Focus

1. **JSON `null` ET0.** ArduinoJson reads `null` as `0.0f`, which would clamp the run to 25% and badly underwater. Expected behavior: `isNull()` is checked first, the response is logged as `bad response (null)`, and the device falls back to the sensor path. *(Task 1, Step 4 inspection.)*
2. **Cache goes stale at local midnight.** Between 00:00 and the next successful fetch, `weatherEt0()` must return false, because the cached value is now two days old. A run then uses `[sensor]` and must not reuse the old value. *(Task 2, Step 5.)*
3. **Timezone mismatch.** `timezone=auto` dates the value in the coordinates' local zone, while the device compares dates using its own configured zone. If those disagree, the value never matches and every run falls back. Expected behavior: every successful fetch logs the value's date, so the mismatch shows up in the log. Do not try to correct it. *(Task 1, Step 6 log check.)*
4. **Network failure while a cycle is due.** A fetch must never start while a cycle runs or a valve is open, and a failed fetch must not delay the cycle start beyond the 5-minute catch-up window. *(Task 3, Step 2.)*
5. **`millis()` rollover and the first attempt after boot.** The 1 h spacing uses `millis() - lastAttemptMs` (unsigned subtraction) together with a separate `attempted` flag, so the device fetches immediately at boot and never stalls after about 49 days of uptime. *(Task 1, Step 4 inspection.)*

---

### Task 1: Weather module that fetches and caches yesterday's ET0

**Files:**
- Modify: `src/config.h:44` (add `LONGITUDE_DEG` after `LATITUDE_DEG`) and `src/config.h:50-60` (`EtReference` gains `float et0mm`; add the value to each row and extend the comment)
- Create: `src/weather.h`, `src/weather.cpp`
- Modify: `src/sprinky.cpp` (add `#include "weather.h"`; call `updateWeather();` on the line after `updateAutoSeason();` in the 1 s housekeeping block)

**Interfaces:**
- Consumes: `state.runCycle`, `valveIsOpen[NUM_RELAYS]` (declared non-static in `relay.cpp:11`; add `extern bool valveIsOpen[NUM_RELAYS];` to `weather.cpp`), `ap_mode` (`wifi_manager.h`), `webPrint()`, TimeLib `now()`/`year()`/`month()`/`day()`/`hour()` (the device's local time), `LATITUDE_DEG`, `LONGITUDE_DEG`.
- Produces (in `weather.h`):
  - `void updateWeather();` decides whether a fetch is due and runs it.
  - `bool weatherEt0(float &mm);` returns true and sets `mm` only when the cached value's YYYYMMDD key equals the key of `now() - SECS_PER_DAY`.

- [ ] **Step 1: Update `config.h`**

Add `constexpr float LONGITUDE_DEG = -117.0f;` with a comment saying to set it, together with `LATITUDE_DEG`, for your site (east positive). Add `float et0mm;  // typical-day Penman-Monteith ET0 (mm/day), reference for the Open-Meteo source` as the last field of `EtReference`, and append `5.7f`, `3.4f`, `3.0f` and `4.0f` to the Summer/Fall/Winter/Spring rows in that order. Extend the comment above the struct to say that `et0mm` came from 2025 Open-Meteo monthly means at 33 N, 117 W, and can be replaced with your own site's values from `archive-api.open-meteo.com/v1/archive?...&daily=et0_fao_evapotranspiration` or tuned from the `ET0 ... [open-meteo]` log lines.

- [ ] **Step 2: Build both targets**

Run: `pio run -e esp12e && pio run -e esp32dev`
Expected: both report `SUCCESS`. Existing aggregate initializers stay valid because the new field is last.

- [ ] **Step 3: Write `weather.h` / `weather.cpp`**

Use `#if defined(ESP32)` → `<HTTPClient.h>` + `<WiFi.h>`, `#else` → `<ESP8266HTTPClient.h>` + `<ESP8266WiFi.h>`, which is the guard style `sprinky.cpp` uses. Use one `WiFiClient` and one `HTTPClient` local to the fetch function. Module state is all `static`:
- `float cachedMm`
- `long cachedKey = -1`
- `unsigned long lastAttemptMs`
- `bool attempted = false`

`updateWeather()` returns early unless every fetch condition in Global Constraints holds and all of the following are true:
- `cachedKey != yesterdayKey`
- either `!attempted`, or (`hour(now()) >= 1` and `millis() - lastAttemptMs >= 3600000UL`)

Set `attempted = true; lastAttemptMs = millis();` **before** the fetch.

Build the URL with `String(LATITUDE_DEG, 4)` / `String(LONGITUDE_DEG, 4)`; do not use `%f`. Use `http.setTimeout(4000)`. If `GET()` returns anything other than 200, log `Weather: fetch failed (HTTP %d), retry in 1h\n` and return. Otherwise parse `getString()` with `deserializeJson` and a filter document:

```cpp
StaticJsonDocument<64> filter;
filter["daily"]["time"][0] = true;
filter["daily"]["et0_fao_evapotranspiration"][0] = true;
```

Reject the response in this order. Each rejection logs `Weather: bad response (<reason>), retry in 1h\n` and leaves the cache untouched.
1. A deserialize error. Reason: `json`.
2. `daily.time[0]` missing, or not a 10-character `YYYY-MM-DD` string. Reason: `date`.
3. `daily.et0_fao_evapotranspiration[0].isNull()`. Reason: `null`.
4. A value that is not finite or is outside 0–20. Reason: `range`.

On success, set `cachedKey` to the date parsed as `YYYY*10000+MM*100+DD` (with `atoi` on the substrings) and set `cachedMm`. Log `Weather: ET0 %d.%dmm for %s\n` in integer tenths, with the date string, plus ` (heap %u->%u)`, where the two figures are `ESP.getFreeHeap()` captured before the `HTTPClient` is created and after the parse. Task 3 uses these for the heap check.

Write a small `static long dayKey(time_t t)` helper and use it for both yesterday's key and in `weatherEt0()`.

- [ ] **Step 4: Inspect against the Review Focus list**

Re-read `weather.cpp` and confirm three things. The `isNull()` check comes before the float read (item 1). The spacing uses unsigned `millis() -` subtraction, and the boot attempt is gated by `attempted` (item 5). Every early return after `lastAttemptMs` is set still logs a line.

- [ ] **Step 5: Build both targets**

Run: `pio run -e esp12e && pio run -e esp32dev`
Expected: both `SUCCESS`.

- [ ] **Step 6: Flash esp12e and observe the fetch**

Flash using the `flash` skill (serial or OTA). Open `http://sprinky.local/api/status` or the Status page.
Expected: within a minute of NTP sync, the log contains `Weather: ET0 X.Ymm for <yesterday's date> (heap A->B)`. Yesterday's date must match the device's local date minus one day (Review Focus item 3).

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/weather.h src/weather.cpp src/sprinky.cpp
git commit -m "Fetch yesterday's Open-Meteo ET0 once a day"
```

---

### Task 2: Scale runs by Open-Meteo ET0 and expose `etSource`

**Files:**
- Modify: `src/relay.cpp:154-217` (`computeEtScale()` and the `state.tempScaling` branch of `startCycle()`)
- Modify: `src/sprinky.h` (add `extern const char *etSource;`)
- Modify: `src/web_server.cpp:69` (add `doc["etSource"] = etSource;` after the `tempScaling` line)

**Interfaces:**
- Consumes: `bool weatherEt0(float &mm)` (Task 1), `ET_REFERENCE[i].et0mm` (Task 1).
- Produces: `const char *etSource`, defined in `relay.cpp` with the initial value `"none"`. It is set at every cycle start to one of the four constants.

- [ ] **Step 1: Add the Open-Meteo branch to `computeEtScale()`**

At the top of the function, read `float mm; if (weatherEt0(mm)) { ... }`. In that branch:
- compute `scale = mm / ref.et0mm` (hoist the `ref` lookup above the branch)
- apply the existing `isfinite` guard (on failure set `etSource = "none"` and return 1.0)
- apply the existing clamps
- log `ET0 %d.%dmm [open-meteo] (ref %d.%d) -> %d%%\n` in integer tenths
- set `etSource = "open-meteo"` and return

- [ ] **Step 2: Tag the Hargreaves path**

On the existing path, change the final log format to `ET0 %d.%dmm [sensor] (ref %d.%d), Tmean %dF swing %dF -> %d%%\n` and set `etSource = "sensor"` before returning. Each existing early `return 1.0f` (fewer than 12 samples, flat swing, non-finite) sets `etSource = "none"`. In `startCycle()`, the `else` branch (scaling off) sets `etSource = "off"`.

- [ ] **Step 3: Expose the source in `/api/status`**

Add the `extern` declaration to `sprinky.h` and the `doc["etSource"]` line to `web_server.cpp`.

- [ ] **Step 4: Build both targets**

Run: `pio run -e esp12e && pio run -e esp32dev`
Expected: both `SUCCESS`.

- [ ] **Step 5: Flash and check each source**

Flash esp12e, wait for the `Weather: ET0` line, enable temperature scaling on the Setup page, then press **Run Now**. Let the cycle finish, or stop it from the dashboard.
Expected: the log shows `ET0 X.Ymm [open-meteo] (ref 3.4) -> N%`, where the reference is 3.4 because October falls in the Fall season. Also, `curl -s http://sprinky.local/api/status | grep -o '"etSource":"[a-z-]*"'` prints `"etSource":"open-meteo"`.
Then turn scaling off and press Run Now again. Expected: `"etSource":"off"`.

For Review Focus item 2, inspect `weatherEt0()` and confirm it compares against the key of `now() - SECS_PER_DAY`, so a value two days old returns false.

- [ ] **Step 6: Commit**

```bash
git add src/relay.cpp src/sprinky.h src/web_server.cpp
git commit -m "Scale run times by Open-Meteo ET0, falling back to the sensor"
```

---

### Task 3: Fallback verification and docs

**Files:**
- Modify (temporarily, then revert): `src/weather.cpp` host string
- Modify: `src/CLAUDE.md`. Add a `weather.cpp / .h` entry to the project layout and architecture, and an `updateWeather()` mention in the `loop()` description. Update the `computeEtScale()` description to cover the Open-Meteo-first order and `[open-meteo]`/`[sensor]` tags. Add `LONGITUDE_DEG` and `et0mm` to the config section, and `etSource` to the `/api/status` field list.
- Modify (**OPTIONAL**, ask the user first): `README.md`, two or three sentences in "Temperature-adjusted watering" about the Open-Meteo source and setting `LATITUDE_DEG`/`LONGITUDE_DEG`.

**Interfaces:**
- Consumes: everything from Tasks 1–2. Produces nothing new.

- [ ] **Step 1: Point the fetch at an unreachable host**

Temporarily change the host in the URL to `api.open-meteo.invalid`. Build esp12e and flash it.
Expected: the log shows `Weather: fetch failed (HTTP -1), retry in 1h`, with negative HTTPClient error codes accepted.

- [ ] **Step 2: Run Now with no web value**

With scaling on, press **Run Now**.
Expected: the cycle starts without delay. The log shows the `ET scaling: ...` fallback line (this board has fewer than 12 hourly samples just after boot, or a flat 70 °F swing), and `etSource` is `"none"`. Before the cycle starts, the log shows no fetch attempt while valves are open (Review Focus item 4). Let the cycle finish, or stop it.

- [ ] **Step 3: Revert the host and confirm heap**

Restore `api.open-meteo.com`, then build both targets and flash esp12e.
Expected: both builds `SUCCESS`. The `Weather: ET0 ... (heap A->B)` line appears, with B within about 1 KB of A, which means the fetch leaves no lasting heap loss.

Run `git diff src/weather.cpp` and confirm it shows no change.

- [ ] **Step 4: Update `src/CLAUDE.md`** as listed under Files.

- [ ] **Step 5: Commit**

```bash
git add src/CLAUDE.md
git commit -m "Document the Open-Meteo ET0 source"
```
