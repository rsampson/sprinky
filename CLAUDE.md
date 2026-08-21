# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Sprinky is an Arduino sketch (`sprinky.ino` + supporting `.cpp`/`.h` files, all
compiled together as one translation unit by the Arduino build system) that
turns an ESP32 or ESP8266 into a networked landscape sprinkler controller.
It drives up to 8 relay-switched solenoid valves on a schedule, adjusts run
time by outdoor temperature, serves a web UI (ESPUI) for control/config, and
optionally bridges to Home Assistant over MQTT.

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
pinned): ESPUI 2.2.4 (pulls in ESPAsyncWebServer, AsyncTCP, WebSockets,
ArduinoJson 6.21.5), arduino-timer 3.0.1, CircularBuffer 1.4.0, ElegantOTA
(must be switched to async mode per its docs), NTPClient, Timezone,
TimeLib, DallasTemperature/OneWire (if `DS18B20` enabled), ArduinoHA (if
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

ESP32 vs ESP8266 differences (temp sensor pin, `Serial` debug macro,
`umm_malloc` heap-select requirement for ESPUI on 8266, mDNS include) are
handled with `#if defined(ESP32)` / `#else` guards, mostly at the top of
`sprinky.ino` and in `wifi_manager.cpp`.

## Architecture

All modules share global state through `sprinky.h`, which is included
everywhere and declares/externs the shared structs and objects. There is no
class hierarchy — this is C-style Arduino code organized by concern into
files, all sharing globals:

- **`sprinky.ino`** — `setup()`/`loop()` entry point, temperature reading
  (`getTempF()`), boot diagnostics, and (if `USE_WITH_HA`) all Home
  Assistant MQTT wiring/callbacks.
- **`sprinky.h`** — the shared state structs (`UIControls ui`,
  `SprinklerState state`), shared globals (`preferences`, `timer`,
  `dayBuffer`, `circBuff`), and prototypes for nearly every function in
  the project. Almost every other file includes this.
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
- **`gui.cpp`** — top-level `setUpUI()` that assembles the three ESPUI
  tabs from the `gui_*` files below.
- **`gui_system_status.cpp`/`.h`** — "System Status" tab: time/temp/signal
  labels, the watering on/off switcher, debug log label.
- **`gui_valve_controls.cpp`/`.h`** — "Valve Controls" tab: per-valve test
  buttons, name fields, run-time sliders, and schedule (hour/minute)
  controls. Also owns `RunCallback`/`SaveScheduleCallback` and the valve
  index lookup pattern used by several callbacks (linear scan matching
  `sender->id` against `ui.buttons[]`/`ui.sliders[]`).
- **`gui_setup_maintenance.cpp`/`.h`** — "Setup and Maintenance" tab:
  Wi-Fi/MQTT credential entry, timezone selector, OTA update link, reboot
  button.
- **`debug.cpp`** — `webPrint()` (printf-style logging that goes to both
  `Serial` and a circular buffer) and `fetchDebugText()`, which drains
  that buffer into `charBuf` for display in the web UI's debug label.

### Persistence

All user-configurable state (Wi-Fi credentials, timezone, schedule
hour/minute, per-valve names and run times, watering-disabled flag) is
stored via the ESP `Preferences` library (NVS-backed key/value store) under
the `"Settings"` namespace, read back on boot in the relevant `setUp*Tab()`
function or `setup()`.

### Control flow per loop() iteration

`loop()` runs MQTT (if HA enabled), the Wi-Fi state machine, NTP update,
`controlRelays()` (schedule check + valve sequencing), OTA handling, and —
throttled to once/second — timer ticking, average-temp computation, LED
heartbeat toggle, and pushing fresh values into the ESPUI labels.

## Known in-progress/rough edges

- MQTT broker credentials in `config.h` are plaintext when `USE_WITH_HA` is
  enabled — treat as a placeholder to override locally, not something to
  commit with real values.
- The "buzz relay to clear jammed valve" logic in `relayOn()` (`relay.cpp`)
  is currently commented out — check its state before assuming it's active.
