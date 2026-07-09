# btcticker-cyd

Standalone BTC ticker firmware for the **CYD "Cheap Yellow Display"**
(ESP32-2432S028R: ESP32-WROOM-32, 2.8" ILI9341 320×240 TFT, resistive touch).
A hardware port of the four crucial metrics from the
[btcticker](../btcticker) web app:

1. **BTC price + 24h % change** — Binance REST, 10 s poll
2. **Mempool fees** (No / Low / Med / High priority, sat/vB) — mempool.space, 60 s
3. **Fear & Greed index** — alternative.me (keyless), hourly
4. **CDC Action Zone strip** — 30 days of EMA12/EMA26 bull/bear blocks
   computed on-device from Binance daily klines, hourly

The device boots, joins Wi-Fi, and runs 24/7 with no phone, browser, or
server. Full design brief in [docs/BRIEF.md](docs/BRIEF.md).

## Build & flash

Requires [PlatformIO](https://platformio.org) (`pipx install platformio` or
the VS Code extension). Then, with the CYD on USB:

```sh
pio run -t upload        # build + flash
pio device monitor       # serial log at 115200
```

## First boot

With no stored credentials the device opens a captive portal — join the
Wi-Fi AP **`BTC-Ticker`** from your phone and enter your network details.
Credentials persist in NVS; later boots connect automatically.

## Behavior

- **Instant paint:** last-known values are cached in NVS every 5 min and
  drawn (dimmed) at boot before the first fetch lands.
- **Fail-soft:** a failed fetch keeps the last value on screen and dims it
  once it goes stale (3 missed refreshes); retries back off 1 s → 60 s.
- **Status row:** green dot = live, orange = connecting, red = reconnecting.
  The case's red LED also lights while Wi-Fi is down.
- **Backlight:** auto-dims via the onboard LDR; tap the screen to cycle
  Auto → 100% → 55% → 25%.
- **Daily restart** at 04:00 (NTP, `TZ_INFO` in `include/config.h`) for a
  fresh heap — same trick as the web app's 4 am reload.

## Configuration

Everything tunable lives in `include/config.h`: URLs, poll cadences,
timezone, restart hour, LDR calibration (`LDR_BRIGHT_RAW` / `LDR_DARK_RAW` —
log `analogRead(34)` in a bright and a dark room and set accordingly).

## Troubleshooting

| Symptom | Fix |
|---|---|
| Red/blue swapped | add `-D TFT_RGB_ORDER=TFT_BGR` to `build_flags` |
| Colors inverted / washed out | add `-D TFT_INVERSION_ON=1` (some CYD batches) |
| White screen | check the board is the single-USB ESP32-2432S028R; the dual-USB variant uses `ST7789_DRIVER` |
| Binance blocked in your region | swap `PRICE_URL`/`KLINES_URL` hosts in `config.h` (e.g. Kraken — see BRIEF.md) |

## Roadmap (v2)

- WebSocket price stream instead of REST polling
- TLS certificate pinning (v1 uses `setInsecure()`)
- Metric show/hide via long-press, like the web app's settings menu
- Bigger price digits via a custom smooth font
