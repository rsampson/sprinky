# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working style (applies to this repo)

1. **Think before coding.** Don't assume; surface tradeoffs. State assumptions
   explicitly and ask when uncertain. If multiple interpretations exist, present
   them rather than silently picking one. If a simpler approach exists, say so.
2. **Simplicity first.** Minimum code that solves the problem, nothing
   speculative. No abstractions for single-use code, no configurability that
   wasn't asked for, no error handling for impossible scenarios.
3. **Surgical changes.** Touch only what the request requires. Don't "improve"
   adjacent code, comments, or formatting; match existing style. Remove
   imports/variables your change orphaned, but leave pre-existing dead code
   alone (mention it instead).
4. **Goal-driven.** Turn the task into a verifiable check and loop until it
   passes — for firmware here that means *compiles for both board targets* and,
   where practical, *observed working on hardware or at least in the web UI
   logic*.

## What this is

Sprinky turns an ESP32 or ESP8266 into a networked landscape sprinkler
controller. It drives up to 8 relay-switched solenoid valves on a daily
schedule (with day-of-week selection and per-season profiles), optionally
scales run time by outdoor temperature, and serves a self-contained web
dashboard (custom `ESPAsyncWebServer` + JSON REST API, embedded HTML/CSS/JS —
**no ESPUI, no Home Assistant/MQTT**) for control and configuration.
`ElegantOTA` is mounted on the same web server for firmware updates.

`README.md` (at the project root, so it renders on the GitHub project page)
is the user-facing setup guide — hardware requirements, flashing, first-boot
Wi-Fi provisioning, dashboard walkthrough, screenshots under `images/`. Read
it for onboarding-flow context; this file focuses on architecture for making
code changes.

## Project layout

This is a **PlatformIO** project — there is **no `.ino` sketch**. The firmware
is the `.cpp`/`.h` files, and (because PlatformIO's default `src_dir` is `src/`)
they live in `src/` alongside `config.h`, `LICENSE`, and this file
(`README.md` lives at the project root, not in `src/`):

```
platformio.ini          project + env + dependency definitions
src/
  sprinky.cpp / .h       setup()/loop(), getTempF(), boot diagnostics, shared state
  config.h               compile-time device/hardware configuration
  relay.cpp              valve control + watering-schedule state machine
  wifi_manager.cpp / .h  Wi-Fi connect/reconnect state machine, AP fallback
  time_manager.cpp / .h  NTP client, all Timezone/DST rule definitions
  web_server.cpp / .h    the AsyncWebServer instance + all routes + persistence
  web_assets.cpp / .h    the dashboard (INDEX_HTML / STYLE_CSS / APP_JS PROGMEM)
  debug.cpp              webPrint() logging to Serial + a circular buffer
```

`src/build/` is stale `arduino-cli` output from before the PlatformIO
conversion; it's gitignored and unused. Ignore it.

## Build

Two environments in `platformio.ini`: **`esp12e`** (ESP8266) and **`esp32dev`**
(ESP32). Both must compile for any firmware change.

```
pio run -e esp12e                       # build ESP8266
pio run -e esp32dev                     # build ESP32
pio run -e esp12e -t upload             # build + flash over USB
pio device monitor                      # serial, 115200 baud
```

If `pio` isn't on PATH, try `~/.platformio/penv/bin/pio`. If no toolchain is
available in the sandbox, say so explicitly and mark the change **UNVERIFIED**
in your summary — never claim a build passed without running it.

There is no unit-test suite. Validation is: compile both targets, then exercise
the device (real hardware, or at minimum the dashboard's JS logic and the JSON
API with `curl`).

### Dependencies / library resolution

All library versions are **pinned** in `platformio.ini` (`lib_deps`). Shared
deps are in `[env]`; each board env adds its own (`esp12e`:
`vshymanskyy/Preferences` shim + `esp32async/ESPAsyncTCP`; `esp32dev`:
`esp32async/AsyncTCP`, needs an Arduino-ESP32 3.x core — `platform =
espressif32 @ ^7.0.0` ships it).

Two non-obvious `platformio.ini` settings exist because ESPAsyncWebServer's
`AsyncJson.cpp` gates its ArduinoJson support behind
`__has_include(<ArduinoJson.h>)` and PlatformIO compiles each library in
isolation:

- `lib_ldf_mode = deep+`
- `build_flags += -I "$PROJECT_LIBDEPS_DIR/$PIOENV/ArduinoJson/src"`

Without these, the `AsyncCallbackJsonWebHandler` symbols `web_server.cpp` needs
are never emitted and the link fails. Don't remove them.

`build_flags` also sets **`-D ELEGANTOTA_USE_ASYNC_WEBSERVER=1`** (required so
ElegantOTA runs against the async server rather than pulling in a second one).

Globally-installed copies in `~/Arduino/libraries` (AsyncTCP, ESPAsyncWebServer,
Preferences, ESPUI) can shadow the pinned ones and cause resolution failures —
check for those first if a build breaks on a library it shouldn't.

## Board / feature configuration (`config.h`)

Almost all per-device and per-hardware variation is compile-time:

- **`HOSTNAME`** — device hostname, mDNS name, and AP-mode SSID. Unique per
  physical controller (currently `"sprinky2"`).
- **`RELAY8`** — define for 8-relay boards, leave commented for 4-relay. Gates
  the `relay[]` pin map length (hence `NUM_RELAYS`, computed as
  `sizeof(relay)/sizeof(relay[0])`) and the extra watering-cycle stages in
  `controlRelays()`.
- **`relay[]`** — GPIO pin per relay; **must** be set per physical board.
  Defaults: 4-relay `{16, 14, 12, 13}`, 8-relay `{32, 33, 25, 26, 27, 14, 12, 13}`.
- **`ON` / `OFF`** — relay active level (`HIGH`/`LOW`); some boards are
  active-low.
- **`DS18B20`** — define to read outside temp from a DS18B20 on `TEMP_PIN`
  (GPIO21 on ESP32, `D1` on ESP8266); leave undefined to read an analog diode on
  `A0`. Currently **defined** in this checkout. Both paths fall back to a fixed
  ~70°F when no sensor is detected: the DS18B20 path checks
  `sensors.getDeviceCount()`, the analog path treats a raw ADC reading < 100 as
  a floating/disconnected pin.
- **`LED_BUILTIN`**, buffer-size constants — rarely touched.

ESP32-vs-ESP8266 differences (temp-sensor pin, `Serial` debug macro, mDNS
include and `MDNS.update()` in `loop()`, Wi-Fi API details) are handled with
`#if defined(ESP32)` / `#else` guards, concentrated at the top of `sprinky.cpp`
and throughout `wifi_manager.cpp`.

## Architecture

C-style Arduino code organized by concern into files that all share globals
through `sprinky.h` (included nearly everywhere; declares the shared state
struct and externs). No class hierarchy.

- **`sprinky.cpp`** — `setup()` / `loop()`, temperature reading (`getTempF()`),
  boot diagnostics, and the `SprinklerState state` definition.
  `loop()` each iteration: `handleWiFi()` (Wi-Fi state machine),
  `timeClient.update()` (NTP), `controlRelays()`, `ElegantOTA.loop()`, and —
  throttled to once/second — `timer.tick()`, `state.cur_temp = getTempF()`,
  `ComputeAveTemp()`, LED heartbeat. On ESP8266 it also calls `MDNS.update()`.
  There is **no server-push step**: the browser polls `/api/status`, so `loop()`
  knows nothing about connected clients.
- **`sprinky.h`** — `struct SprinklerState` (`disable`, `runCycle`,
  `tempScaling`, `runHour`, `runMinute`, `runtime[8]`, `start_time_ms`,
  `temp_adjust`, `cur_temp`, `avg_temp`, `lastRunMinutes`, `activeDays`),
  `extern` globals (`preferences`, `timer`, `dayBuffer`, `circBuff`, `charBuf`),
  and prototypes for most functions in the project.
- **`config.h`** — see above. Included by `sprinky.h`.
- **`relay.cpp`** — valve/relay hardware (`relayOn`, `allOff`, `relayConfig`)
  and the watering state machine:
  - `controlRelays()` — checks `state.activeDays` (bitmask, bit 0 = Sunday …
    bit 6 = Saturday, matching TimeLib `weekday()` which is 1-7 Sunday=1) to see
    if today is enabled, then fires at `state.runHour:state.runMinute:00` if
    `runCycle` is false. At cycle start it computes `state.temp_adjust`: if
    `state.tempScaling` is on, `map((int32_t)state.avg_temp, 40, 90, 300, 3000)`
    (0.3×–3.0× run-time multiplier, with a seconds→ms factor folded in);
    otherwise it's pinned to `1000` (1.0×, run times used exactly as entered).
    Per-valve start times chain as `START1..START9` macro offsets from
    `state.start_time_ms`, each `state.runtime[i] * state.temp_adjust`.
  - **Safety timer** (`timer`, arduino-timer): force-shuts all valves after
    4 800 000 ms (80 min) for a scheduled cycle, and after 60 s for a manual
    valve test.
  - `ComputeAveTemp()` — once/hour, pushes `state.cur_temp` into the 24-slot
    `dayBuffer` and recomputes `state.avg_temp`.
  - The "buzz relay to clear a jammed valve" loop in `relayOn()` is **commented
    out** — check before assuming it runs.
  - `/api/run` ("Run Watering Sequence Now") bypasses both the `activeDays`
    check and the daily start time.
- **`wifi_manager.cpp` / `.h`** — Wi-Fi as an explicit state machine
  (`DISCONNECTED → CONNECTING → CONNECTED`, periodic connectivity checks,
  hard-reset-and-retry on failure). Falls back to AP mode (`ap_mode` global) at
  `192.168.4.1` when stored credentials fail, so the dashboard can be used to
  enter new ones. Owns `MDNS.begin(HOSTNAME)`.
- **`time_manager.cpp` / `.h`** — the NTP client instance, every supported
  `Timezone`/DST rule, and conversion between the stored timezone string (in
  `Preferences`) and the active `Timezone *tz`. Adding a zone means: a
  `TimeChangeRule` + `Timezone`, an `extern` in the header, and a branch in each
  of `tzName()`, `tzCode()`, `TZstringToPointer()` — plus an `<option>` in
  `web_assets.cpp`. The selector value string must be ≤15 chars (ESP32 NVS key
  limit is not the constraint here, but keep codes short/distinct; note China
  uses `CNST`, not `CST`, to avoid colliding with US Central).
- **`web_server.cpp` / `.h`** — owns the single `AsyncWebServer server` (port
  80) and `setUpWebServer()`, called from `setup()`. `ElegantOTA.begin(&server)`
  runs right after on the same instance, so `/update` works unchanged. Serves
  the dashboard assets from `web_assets.cpp` (PROGMEM — no LittleFS) plus the
  JSON REST API (see below). Also holds the per-season persistence layer.
- **`web_assets.cpp` / `.h`** — the dashboard as three PROGMEM strings
  (`INDEX_HTML`, `STYLE_CSS`, `APP_JS`). Single-page, three tabs (Status /
  Valves / Setup), CSS-only card layout with light/dark via
  `prefers-color-scheme`, vanilla JS (no framework) that polls `/api/status`
  every 1 s and POSTs actions with `fetch()`. Valve rows, day-of-week toggles,
  and per-valve test buttons are all built client-side from `/api/status`, so
  the same HTML serves both 4- and 8-relay builds with no template flag.
- **`debug.cpp`** — `webPrint()` (printf-style, goes to both `Serial` and a
  circular buffer) and `fetchDebugText()`, which drains the buffer into
  `charBuf`; the `/api/status` handler calls it each request to include the log
  tail.

### Web API

`GET /api/status` (polled every 1 s) returns: `hostname`, `time`, `timezone`,
`timezoneCode`, `tempF`, `avgTempF`, `rssi`, `lastRunMinutes`, `disabled`,
`tempScaling`, `runHour`, `runMinute`, `activeDays`, `season`, `ssid`,
`apMode`, `log`, and a `valves[]` array (`name`, `runtime`, `on`).

POST actions (JSON body unless noted), each mirroring what an old ESPUI
callback did — same `Preferences` keys, same `relayOn`/`shutOff`/`state` calls,
just over HTTP:

| Route | Body | Effect |
|---|---|---|
| `/api/valve/{n}` | `{"on":bool}` | Manually open/close valve n; auto-off after 60 s |
| `/api/watering` | `{"disable":bool}` | Master watering enable/disable (`disable` key) |
| `/api/tempscaling` | `{"enabled":bool}` | Toggle temperature run-time scaling (`tempScale` key) |
| `/api/run` | *(none)* | Start the full watering sequence now, ignoring day/time |
| `/api/schedule` | `{hour, minute, activeDays, valves:[{name,runtime}], season?}` | Save hour/minute/days/valve names+times to the current (or given) season |
| `/api/season` | `{"season":0-3}` | Switch active season profile: load its stored schedule into `state` and apply immediately |
| `/api/wifi` | `{ssid, pass}` | Store Wi-Fi credentials |
| `/api/timezone` | `{"tz":"<code>"}` | Set timezone; re-syncs NTP |
| `/api/reboot` | *(none)* | Restart after ~0.5 s |

### Persistence (`Preferences`, NVS-backed, namespace `"Settings"`)

`preferences.begin("Settings")` is called in `setup()` (`sprinky.cpp`).

**Global keys** (not per-season): `ssid`, `pass`, `timezone`, `disable`,
`tempScale` (bool, default `true`), `curSeason` (`uint8`, 0-3, default `0` =
Summer).

**Per-season schedule keys**, prefixed `s<0-3>_`: `s<n>_hour`, `s<n>_minute`,
`s<n>_activeDays` (`uint8`, longest key at 13 chars — stay ≤15 for the ESP32
NVS limit), `s<n>_name1..8`, `s<n>_slide1..8` (run time in **seconds**).

Season index → name: `0`=Summer, `1`=Fall, `2`=Winter, `3`=Spring. Only one
profile is loaded into `state` at a time; `controlRelays()` is season-agnostic
and just reads `state.*`. There is **no** date-based auto-switching — the user
picks the season.

**Legacy migration**: `seedSeasonFromLegacy()` (called once from
`setUpWebServer()`) copies the pre-seasonal flat keys (`hour`, `minute`,
`activeDays`, `name1..8`, `slide1..8`) into the Summer (`s0_*`) slot the first
time the firmware boots with no season data, so deployed devices keep their
schedule. Legacy keys are then left in place, unread.

Boot read path: `setUpWebServer()` → `curSeason = getUChar("curSeason", 0)` →
`seedSeasonFromLegacy()` → `loadSeason(curSeason)`. `disable` is read directly
in `setup()`.

### Adding a persisted setting — checklist

1. Field in `struct SprinklerState` (`sprinky.h`) if it needs to be live in
   `loop()`.
2. Read it in `loadSeason()` (per-season) or the relevant boot path (global),
   with a sensible default.
3. Write it in `handleSchedule()` (per-season) or a dedicated `handle*()` +
   route (global), using `seasonKey()` for per-season keys.
4. Add it to the `/api/status` document.
5. Wire the control in `web_assets.cpp`: HTML in `INDEX_HTML`, styling in
   `STYLE_CSS`, `fetch()` + `applyStatus()` handling in `APP_JS`.
6. `pio run` for **both** envs.

### Editing `web_assets.cpp`

The three assets are raw-string literals (`R"HTML(...)HTML"` etc.). A stray
`)HTML"` / `)CSS"` / `)JS"` sequence inside the content, or an unbalanced
quote, breaks the build — that's the usual failure mode after an edit here.
The valve config rows are created once (a `configBuilt` latch); anything that
must re-populate them later (e.g. a season switch) has to call the shared
`fillValveFields()` helper rather than relying on `applyStatus()`.

## Verifying changes

- **Always** `pio run -e esp12e` **and** `pio run -e esp32dev` after any
  firmware edit. ESP32 is the target that enforces the 15-char NVS key limit.
- For dashboard/API changes, hit the running device with `curl`
  (`/api/status`, POST the relevant route) and confirm the effect, and check
  the served `/style.css` / `/app.js` actually contain the change.
- OTA-flashing a networked device: this firmware uses **ElegantOTA v3**, whose
  real upload endpoints are `GET /ota/start?mode=fr&hash=<md5>` then
  `POST /ota/upload` (multipart, field `file`) — `/update` is only the UI page.
  After upload the device reboots; poll `GET /` for a 200, then re-check
  `/api/status`. (The user's `flash` skill automates this.)

## Known rough edges

- `src/build/` — stale pre-PlatformIO `arduino-cli` output, gitignored, ignore.
- The jammed-valve "buzz" loop in `relayOn()` is commented out.
- `tempScaling` is a single global, deliberately not per-season (it's a
  sensor-behavior switch, not a schedule value).
