# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Sprinky is an Arduino sketch (`sprinky.ino` + supporting `.cpp`/`.h` files, all
compiled together as one translation unit by the Arduino build system) that
turns an ESP32 or ESP8266 into a networked landscape sprinkler controller.
It drives up to 8 relay-switched solenoid valves on a schedule, adjusts run
time by outdoor temperature, serves a custom web dashboard (via
`ESPAsyncWebServer` + a JSON REST API, embedded HTML/CSS/JS — no ESPUI) for
control/config, and optionally bridges to Home Assistant over MQTT.

## Build

There is no Makefile/CMake — this is built via the Arduino IDE (2.x) or
`arduino-cli` against the sketch folder itself (`/home/richard/sprinky`).
`build/` contains per-board output and is gitignored.

Known-good FQBNs (from `build/*/build.options.json`):
- ESP32: `esp32:esp32:esp32:UploadSpeed=921600,CPUFreq=240,FlashFreq=80,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default`
- ESP8266: `esp8266:esp8266:nodemcuv2`

If using `arduino-cli`:
```
arduino-cli compile --fqbn <fqbn above> .
arduino-cli upload -p <port> --fqbn <fqbn above> .
```

Key library dependencies (versions matter, noted inline in source where
pinned): ESPAsyncWebServer (ESP32Async fork, v3.7.3 locally — serves the
dashboard and the JSON API; ElegantOTA is mounted on the same instance),
ArduinoJson 6.21.5 (status/action payloads), arduino-timer 3.0.1,
CircularBuffer 1.4.0, ElegantOTA (must be switched to async mode per its
docs — `ELEGANTOTA_USE_ASYNC_WEBSERVER=1`), NTPClient, Timezone, TimeLib,
DallasTemperature/OneWire (if `DS18B20` enabled), ArduinoHA (if
`USE_WITH_HA` enabled).

There is no test suite — validation is by compiling for both board targets
and exercising the device (real hardware or at minimum the web UI logic).

## Board/feature configuration (`config.h`)

Almost all per-device and per-hardware variation is compile-time, controlled
from `config.h`:
- `HOSTNAME` — device hostname, mDNS name, and AP SSID (used when no wifi
  credentials are stored yet). Distinct per physical controller.
- `RELAY8` — define for 8-relay boards, leave undefined for 4-relay boards.
  Gates `NUM_RELAYS`, the `relay[]` GPIO pin map, the extra watering-cycle
  stages in `controlRelays()`, and (if `USE_WITH_HA`) the extra HA switches.
- `relay[]` — GPIO pin assignment for each relay; must be set per physical
  relay board (Chinese-sourced ESP32/ESP8266 relay boards vary).
- `DS18B20` — use a DS18B20 digital sensor for outside temp instead of the
  analog diode-on-A0 fallback (see `getTempF()` in `sprinky.ino`).
- `USE_WITH_HA` — enable Home Assistant/MQTT integration (`BROKER_ADDR`,
  `BROKER_USERNAME`, `BROKER_PASSWORD` are also here — note these are
  plaintext in-repo credentials, be careful before committing real values).
- `ON`/`OFF` — relay active level; some relay boards are active-low.

ESP32 vs ESP8266 differences (temp sensor pin, `Serial` debug macro, mDNS
include) are handled with `#if defined(ESP32)` / `#else` guards, mostly at
the top of `sprinky.ino` and in `wifi_manager.cpp`.

## Architecture

All modules share global state through `sprinky.h`, which is included
everywhere and declares/externs the shared structs and objects. There is no
class hierarchy — this is C-style Arduino code organized by concern into
files, all sharing globals:

- **`sprinky.ino`** — `setup()`/`loop()` entry point, temperature reading
  (`getTempF()`), boot diagnostics, and (if `USE_WITH_HA`) all Home
  Assistant MQTT wiring/callbacks.
- **`sprinky.h`** — the shared state struct (`SprinklerState state`),
  shared globals (`preferences`, `timer`, `dayBuffer`, `circBuff`), and
  prototypes for nearly every function in the project. Almost every other
  file includes this.
- **`config.h`** — compile-time device/hardware configuration (see above).
  Included by `sprinky.h`.
- **`relay.cpp`** — valve/relay hardware control (`relayOn`, `allOff`,
  `relayConfig`) and the watering-schedule state machine
  (`controlRelays()`, `ComputeAveTemp()`). `controlRelays()` computes
  per-valve start times as a chain of offsets (`START1`..`START9` macros)
  from `state.start_time_ms`, each valve's `state.runtime[]` scaled by
  `state.temp_adjust` (a temperature-derived multiplier computed from the
  24-hour rolling average in `dayBuffer`, mapped 40-90°F onto a 0.3x-3x
  runtime multiplier). A safety timer (`timer`, from arduino-timer) always
  force-shuts-off valves after a bounded duration, both for scheduled
  cycles (80 min) and manual valve tests (60 sec).
- **`wifi_manager.cpp`/`.h`** — Wi-Fi connect/reconnect as an explicit
  state machine (`WifiState`: DISCONNECTED → CONNECTING → CONNECTED, with
  periodic connectivity checks and automatic hard-reset-and-retry on
  failure). Falls back to AP mode (`ap_mode` global) at `192.168.4.1` if
  stored credentials fail, so the web UI can be used to enter new ones.
- **`time_manager.cpp`/`.h`** — NTP client instance, all supported
  `Timezone`/DST rule definitions, and conversion between the stored
  timezone string (in `Preferences`) and the active `Timezone *tz`.
- **`web_server.cpp`/`.h`** — owns the single `AsyncWebServer server`
  instance (port 80) and `setUpWebServer()`, which registers every route
  and is called from `setup()` in place of the old `setUpUI()`.
  `ElegantOTA.begin(&server)` is called right after, on the same server
  instance, so `/update` keeps working unchanged. Serves the dashboard's
  HTML/CSS/JS (from `web_assets.cpp`, embedded PROGMEM strings — no
  filesystem/LittleFS involved) plus a small JSON REST API:
  `GET /api/status` (polled by the browser every second: time, temp,
  24h avg temp, RSSI, per-valve name/runtime/on-state, disabled flag,
  last-run minutes, debug log tail, timezone, stored SSID), and POST
  actions `/api/valve/{n}`, `/api/watering`, `/api/run`, `/api/schedule`,
  `/api/wifi`, `/api/timezone`, `/api/reboot` — each mirrors what an
  ESPUI callback used to do (same `Preferences` keys, same
  `relayOn`/`shutOff`/`state` calls), just invoked over HTTP instead of a
  control callback.
- **`web_assets.cpp`/`.h`** — the dashboard itself as three PROGMEM string
  constants (`INDEX_HTML`, `STYLE_CSS`, `APP_JS`). Single-page app with
  CSS-only card layout (light/dark via `prefers-color-scheme`) and vanilla
  JS (no framework) that polls `/api/status` on a 1s interval and POSTs
  actions via `fetch()`. Valve rows/buttons are built client-side from
  `/api/status`'s `valves` array, so the same HTML works for both 4- and
  8-relay (`RELAY8`) builds without a template flag.
- **`debug.cpp`** — `webPrint()` (printf-style logging that goes to both
  `Serial` and a circular buffer) and `fetchDebugText()`, which drains
  that buffer into `charBuf`; `web_server.cpp`'s `/api/status` handler
  calls this each request to include the log tail in the JSON response.

### Persistence

All user-configurable state (Wi-Fi credentials, timezone, schedule
hour/minute, per-valve names and run times, watering-disabled flag) is
stored via the ESP `Preferences` library (NVS-backed key/value store) under
the `"Settings"` namespace. Keys are unchanged from before the web UI
rewrite (`ssid`, `pass`, `timezone`, `hour`, `minute`, `name1..N`,
`slide1..N`, `disable`) — existing values on deployed devices still load
correctly. Read back on boot in `setup()`; written by the corresponding
`/api/*` POST handler in `web_server.cpp`.

### Control flow per loop() iteration

`loop()` runs MQTT (if HA enabled), the Wi-Fi state machine, NTP update,
`controlRelays()` (schedule check + valve sequencing), OTA handling, and —
throttled to once/second — timer ticking, average-temp computation, and LED
heartbeat toggle. There's no server-push step: the browser pulls current
state itself via `/api/status` polling, so `loop()` doesn't need to know
anything about connected clients.

## Known in-progress/rough edges

- MQTT broker credentials in `config.h` are plaintext when `USE_WITH_HA` is
  enabled — treat as a placeholder to override locally, not something to
  commit with real values.
- The "buzz relay to clear jammed valve" logic in `relayOn()` (`relay.cpp`)
  is currently commented out — check its state before assuming it's active.
- The dashboard's MQTT user/password/broker fields (Setup tab, only shown
  when `USE_WITH_HA` is defined) are UI-only — saving them doesn't feed
  back into `USE_WITH_HA`'s hardcoded `BROKER_ADDR`/`BROKER_USERNAME`/
  `BROKER_PASSWORD` in `config.h`. Pre-existing gap from the ESPUI days,
  carried forward as-is.
- `sprinky.ino`'s `USE_WITH_HA` block references `switch5`..`switch9`
  unconditionally in `setup()`, but those globals are only declared when
  `RELAY8` is also defined — pre-existing bug, only surfaces if someone
  actually builds with `USE_WITH_HA` defined (it isn't, by default).
