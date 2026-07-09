# BTC Ticker Firmware for CYD (Cheap Yellow Display) — Project Brief

## Goal

Port the four crucial metrics of the btcticker web app to a standalone firmware
for the **CYD ESP32 board** (ESP32-2432S028R: ESP32-WROOM-32, 2.8" ILI9341 TFT,
320×240, resistive touch, USB-powered). The device boots, joins Wi-Fi, and runs
24/7 as a desk ticker with no phone, browser, or server required.

The four metrics (mirroring `index.html` / `app.js`):

1. **BTC price + 24h % change**
2. **Mempool transaction fees** (4 priority tiers)
3. **Fear & Greed index**
4. **CDC Action Zone strip** (30-day EMA12/EMA26 bull/bear blocks)

## Data Sources

All fetched directly from the device over HTTPS. No key-gated APIs — the ESP32
cannot hold the CMC secret the web pipeline uses, so F&G switches provider.

| Metric | Source | Cadence | Notes |
|---|---|---|---|
| Price + 24h change | Binance REST `GET /api/v3/ticker/24hr?symbol=BTCUSDT` (fields `lastPrice`, `priceChangePercent`) | 5–10 s poll | WebSocket (`btcusdt@ticker`) optional v2 upgrade; REST polling is simpler and robust on ESP32. Fallback: Coinbase `GET /v2/prices/BTC-USD/spot` or Kraken `Ticker` REST. |
| Fees | mempool.space `GET /api/v1/fees/mempool-blocks` | 60 s | Same tier derivation as `app.js deriveTiers()`: high = block[0].medianFee, med = block[2], low = block[5], no-priority = last block (indices clamped to array length). Parse only `medianFee` per block — stream-parse or use a JSON filter, the full response is large. |
| Fear & Greed | alternative.me `GET https://api.alternative.me/fng/?limit=1` (no key) | 1 h | Replaces the CMC-key pipeline (`fetch_fng.py`). Returns `value` (0–100) + `value_classification`. |
| CDC strip | Kraken `GET /0/public/OHLC?pair=XBTUSD&interval=1440` | 1 h | Compute on-device, porting `calcEMA()` + block logic from `app.js:481-525`: last 100 daily closes → EMA12/EMA26 → last 30 days, `bull = ema12 > ema26`, bar height ∝ `|ema12 − ema26|` normalized over the 30-day window. Needs ≥56 closes (30 blocks + EMA26 warmup). |

## Hardware / Toolchain

- **Board:** ESP32-2432S028R "CYD" — ILI9341 320×240 SPI TFT, XPT2046 resistive
  touch (separate SPI bus), RGB LED, LDR on GPIO34, backlight on GPIO21.
- **Framework:** Arduino via **PlatformIO** (`board = esp32dev`).
- **Display lib:** LVGL 9 + TFT_eSPI (or `esp32-smartdisplay` which bundles the
  CYD pin map), landscape 320×240. Plain TFT_eSPI without LVGL is acceptable
  for v1 — the layout is static text + rectangles.
- **HTTP:** `HTTPClient` + `WiFiClientSecure` (use `setInsecure()` for v1;
  pin roots later). **JSON:** ArduinoJson 7 with input filters to keep RAM low.
- **Wi-Fi provisioning:** WiFiManager captive portal (no hardcoded credentials);
  store config in NVS.

## Screen Layout (320×240 landscape, mirrors the web page)

```
┌──────────────────────────────────────┐
│ ● live            Binance      ⚙     │  status row (8 px margin)
│                                      │
│        $ 1 1 8 , 4 2 3              │  price, largest element (~48 px font)
│              .55   28  +2%          │  decimals + F&G value + 24h chg
│                                      │
│  ▂▃▅▆▇▇▆▅▃▂▁▁▂▃▄▅▅▅▄▃▂▁▁▁▁▁▁▁▁      │  CDC strip: 30 bars, up=green/down=red
│                                      │
│  No 1 · Low 2 · Med 3 · High 5      │  fee bar (sat/vB, orange values)
└──────────────────────────────────────┘
```

- **Price:** integer part dominant; decimals smaller/dimmer (match `style.css`
  visual hierarchy). Green/red flash on tick optional.
- **24h change:** `+N%` rounded to whole percent, green ≥ 0 / red < 0
  (same as `renderPriceDOM()`).
- **F&G:** value colored by quintile — reuse the web palette:
  `#ff1744, #ff6d00, #ffeb3b, #69f0ae, #00e676` (fear→greed, bands of 20).
- **CDC strip:** 30 slots; bull bars grow up from baseline (green), bear bars
  hang down (red), today's bar highlighted; heights 4–52 px normalized like
  `renderCDC()`.
- **Fees:** single row, 4 tiers, labels dim / values bold orange (`#f7931a`).

## Firmware Architecture

- **Tasks (FreeRTOS):** one network task doing scheduled fetches (price 5–10 s,
  fees 60 s, F&G + CDC 1 h) into a mutex-guarded state struct; UI task/loop
  renders dirty regions only — never rebuild the whole frame per tick (same
  principle as the web app's sub-element rendering).
- **Fail-soft everywhere:** any fetch error keeps the last value on screen and
  marks it stale (dim the value, or amber status dot) — mirror the web app's
  behavior of never blanking on error. HTTP timeout ~8 s.
- **Reconnect/backoff:** exponential backoff 1 s → 16 s cap on Wi-Fi/API
  failures; auto-rejoin Wi-Fi on drop.
- **Persistence (NVS):** last price/change, F&G, fee tiers, CDC blocks — paint
  instantly on boot before the first fetch (equivalent of the localStorage
  tiers in the web app).
- **Daily reset:** `esp_restart()` once daily at a quiet hour (04:00 via NTP
  time) — same rationale as the web app's 4 am scheduled reload.
- **NTP:** needed for the daily restart and F&G staleness check (>48 h = stale).

## Touch / Controls (v1 minimal)

- Tap anywhere: cycle brightness (backlight PWM on GPIO21), or use the LDR for
  auto-dim at night.
- Long-press: toggle metric visibility later (v2); v1 shows all four always.

## Milestones

1. **M0 – Bring-up:** PlatformIO project, display + touch verified, Wi-Fi
   portal, NTP.
2. **M1 – Price:** Binance REST poll, price + 24h change rendered with correct
   typography/colors.
3. **M2 – Fees:** mempool-blocks fetch, tier derivation, fee row.
4. **M3 – F&G + CDC:** alternative.me fetch; Kraken OHLC + on-device EMA; strip
   rendering.
5. **M4 – Hardening:** NVS cache, backoff, stale indicators, daily restart,
   auto-dim, 1-week soak test.

## Risks / Constraints

- **TLS RAM:** each `WiFiClientSecure` connection costs ~40 KB heap; do fetches
  sequentially on one client, never in parallel.
- **mempool-blocks payload** is large (can exceed 100 KB): use ArduinoJson
  filter documents or read incrementally; only `medianFee` of ≤8 blocks needed.
- **Kraken OHLC payload** (~100 daily candles) is fine, but slice to the last
  100 closes as the web app does.
- **API churn:** keep each source behind a small interface so a provider swap
  (e.g. Binance → Kraken) is a one-file change, like `EXCHANGES` in `app.js`.
- **Burn-in:** ILI9341 is IPS-less TFT (no OLED burn-in risk), but auto-dim
  still extends backlight life.

## Repo Plan

New sibling repo `btcticker-cyd` (PlatformIO layout):

```
btcticker-cyd/
├── platformio.ini
├── src/
│   ├── main.cpp          # setup, tasks, scheduler
│   ├── net/  (price.cpp, fees.cpp, fng.cpp, cdc.cpp, http.cpp)
│   ├── ui/   (screen.cpp, theme.h)      # colors from style.css
│   └── store/(nvs_cache.cpp)
├── include/config.h      # cadences, URLs, pins
└── README.md
```
