# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Standalone BTC ticker **firmware** for the CYD "Cheap Yellow Display"
(ESP32-2432S028R: ESP32-WROOM-32, 2.8" 320×240 ILI9341 TFT, XPT2046 resistive
touch). It is a hardware port of four metrics from the sibling `btcticker` web
app: BTC price + 24h change, mempool fees, Fear & Greed index, and a CDC Action
Zone strip. The device boots, joins Wi-Fi, and runs 24/7 with no phone, browser,
or server. The full design rationale — including the exact web-app functions each
piece ports — lives in `docs/BRIEF.md`.

This is an Arduino/PlatformIO C++ project, **not** a static web project like the
other repos in this parent directory. The `~/CLAUDE.md` conventions (HTML/CSS/JS
projects) do not apply here.

## Build / flash / debug

Requires [PlatformIO](https://platformio.org) (`pipx install platformio`). With
the CYD on USB:

```sh
pio run              # compile only
pio run -t upload    # build + flash
pio device monitor   # serial log @ 115200
```

**Important**: Always ensure `upload_speed = 115200` in `platformio.ini` when flashing the firmware to avoid serial noise or corruption errors on the USB connection.

There is no test suite. **`simulator.html`** is the fast iteration path: open it
in a browser to preview the exact 320×240 layout and interactions without
hardware. It re-implements `src/ui/screen.cpp`'s render logic in JS against the
same live APIs, so it is where you validate layout/color/formatting changes
before flashing. Keep it in sync when you change rendering.

The custom price font is generated, not hand-written: `python3
scripts/gen_price_font.py` regenerates `src/ui/fonts/PriceFont.h` (needs Pillow +
a bold sans TTF; `FONT_PATH` defaults to a macOS Arial Bold path — change it on
other platforms).

## Architecture

Everything runs on the single Arduino `loop()` task — **no RTOS tasks, no
locking**. `main.cpp:loop()` calls, in order: `updateNetState` → `serviceJobs` →
`handleTouch` → `updateBacklight` → `maybeSaveCache` → `maybeDailyRestart` →
`screenRender`, then `delay(25)`.

**Shared state is one global `TickerState S`** (`include/state.h`). Fetch jobs
write it; the renderer reads it. Because it is all one task, plain reads/writes
are safe. Each metric carries a `*OkMs` timestamp (millis of last success, 0 =
never) that drives the "stale → render dimmed" behavior.

**Fetch jobs** (`main.cpp` `jobs[]` table). Four jobs — price/fees/fng/cdc —
each with an interval, a `run()` fn, a `next` due time, and a per-job `backoff`.
`serviceJobs()` runs **at most one due job per loop pass** so the single shared
TLS client stays sequential and the UI never blocks on more than one request.
Failures back off exponentially (`RETRY_BASE_MS` 1s → `RETRY_MAX_MS` 60s);
success resets backoff and reschedules at the normal interval.

**Networking** (`src/net/`). Fees/fng use one-shot `httpGet()` in `http.cpp`,
which opens a fresh `WiFiClientSecure` using `setInsecure()` (no cert pinning —
v1 tradeoff, see README roadmap) and `useHTTP10(true)` so responses can be
stream-parsed. `price.cpp` is the exception: it polls every second, so it
keeps its own persistent HTTP/1.1 keep-alive `WiFiClientSecure`/`HTTPClient`
session instead — a fresh TLS handshake every poll was the main-loop's biggest
CPU cost. Each metric has its own module:
- `price.cpp` — ArduinoJson with a **filter** parsed off the keep-alive
  session's `getString()` body (HTTP/1.1 may be chunked, so this can't stream
  straight off `getStream()` the way the one-shot fetchers do).
- `fees.cpp` / `fng.cpp` — ArduinoJson with a **filter** (or, for F&G, the
  whole small doc) parsed straight off `http.getStream()`.
- `cdc.cpp` — the klines payload is ~25 KB, too big to buffer, so
  `parseCloses()` is a hand-rolled bracket-depth scanner that pulls only field 4
  (close) off the TLS stream. `calcEMA()` is a direct port of the web app's EMA
  math; the last 30 days become bull/bear blocks with normalized bar heights.

**Rendering** (`src/ui/screen.cpp`) is **dirty-region based**. `screenRender()`
runs every loop pass but each `draw*()` fn compares its current values against
a last-drawn-state struct and returns early if unchanged — only changed
regions repaint (GFX free fonts don't paint their own background, so each dirty
region is `fillRect`-cleared first). `drawStatus()` additionally samples
`ESP.getSketchSize()`/`getFreeSketchSpace()` (a flash scan costing ~300 ms) at
most once per `STATUS_REFRESH_MS`, not every pass. Fixed layout bands are documented in the
comment block at the top of the file (status row, price row, delta-pill row,
CDC card, fee chips); change those `#define`s together. `theme.h` holds the
RGB565 palette ("instrument panel" dark theme), the F&G color bands ported
hex-for-hex from the web app, and `lerp565()`/`tint565()` blend helpers used
for pill fills and the CDC age fade (RGB565 has no alpha). An **offline screen**
(`drawOffline`) replaces the ticker whenever `netState == 2`.

**Persistence** (`src/store/cache.cpp`) mirrors the web app's localStorage
snapshots via ESP32 NVS `Preferences` (namespace `"ticker"`). `cacheSave()` runs
every `CACHE_SAVE_MS` (5 min) and writes **only metrics that have succeeded at
least once this boot**, so a failing session never clobbers good cached values.
`cacheLoad()` at boot paints last-known values immediately (dimmed) before the
first live fetch lands.

## Configuration

**`include/config.h` is the single tuning surface** — API URLs, poll cadences,
retry/timeout, timezone (`TZ_INFO`), daily-restart hour, all CYD pin
assignments, and LDR calibration (`LDR_BRIGHT_RAW` / `LDR_DARK_RAW`: log
`analogRead(34)` in a bright vs. dark room and set accordingly).

**`platformio.ini` `build_flags` configure TFT_eSPI** (this project uses build
flags instead of a `User_Setup.h`). The wiring there is specific to the
single-USB ESP32-2432S028R. Common per-unit fixes live in the README
troubleshooting table: swapped red/blue → `-D TFT_RGB_ORDER=TFT_BGR`; inverted
colors → `-D TFT_INVERSION_ON=1`; white screen usually means the dual-USB board
variant (needs `ST7789_DRIVER`).

## Conventions

- Comments explain **why**, often citing the web-app function being ported (e.g.
  `deriveTiers()`, `calcEMA()`, `renderCDC()`). Preserve those cross-references.
- Metrics fail soft: never blank a value on a failed fetch — keep the last one
  and dim it once stale (3 missed refreshes). Match this pattern for any new
  metric.
- Keep fetches non-blocking to the UI: one request per loop pass, sequential on
  the shared TLS client.
