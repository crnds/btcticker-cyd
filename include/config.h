#pragma once

// ── Wi-Fi / time ─────────────────────────────────────────
#define AP_PORTAL_NAME     "BTC-Ticker"
#define NTP_SERVER         "pool.ntp.org"
// POSIX TZ string; "ICT-7" = Asia/Bangkok. Lookup table:
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TZ_INFO            "ICT-7"
// once-daily restart at a quiet hour resets the heap for free on a 24/7 device
#define DAILY_RESTART_HOUR 4

// ── Data sources ─────────────────────────────────────────
// Price + 24h change: Binance REST (the web app's default exchange).
#define PRICE_URL  "https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT"
// CDC closes: Binance daily klines instead of the web app's Kraken OHLC —
// same math, but a fixed limit=100 response that the streaming parser in
// net/cdc.cpp can read without buffering the whole payload.
#define KLINES_URL "https://api.binance.com/api/v3/klines?symbol=BTCUSDT&interval=1d&limit=100"
#define FEES_URL   "https://mempool.space/api/v1/fees/mempool-blocks"
// alternative.me instead of the web pipeline's CoinMarketCap: no API key,
// so the device can fetch it directly.
#define FNG_URL    "https://api.alternative.me/fng/?limit=1"

// ── Cadences / retry ─────────────────────────────────────
#define PRICE_INTERVAL_MS  10000UL
#define FEES_INTERVAL_MS   60000UL
#define FNG_INTERVAL_MS    3600000UL
#define CDC_INTERVAL_MS    3600000UL
#define RETRY_BASE_MS      1000UL
#define RETRY_MAX_MS       60000UL
#define HTTP_TIMEOUT_MS    8000

// a metric missing 3 consecutive refreshes renders dimmed instead of blanking
#define PRICE_STALE_MS     (3 * PRICE_INTERVAL_MS + 30000UL)
#define FEES_STALE_MS      (3 * FEES_INTERVAL_MS)
#define FNG_STALE_MS       (3 * FNG_INTERVAL_MS)

// NVS snapshot cadence — mirrors the web app's 5-min localStorage snapshots;
// keeps flash wear negligible while boot always has a recent value to paint
#define CACHE_SAVE_MS      (5 * 60000UL)

// ── CYD pins (ESP32-2432S028R) ───────────────────────────
#define PIN_BACKLIGHT  21
#define PIN_LDR        34   // photoresistor, analog
#define PIN_TOUCH_IRQ  36   // XPT2046 PENIRQ, LOW while touched
#define PIN_LED_R      4    // RGB LED, active LOW
#define PIN_LED_G      16
#define PIN_LED_B      17

// ── Backlight ────────────────────────────────────────────
#define BL_CHANNEL     0
#define BL_MIN_DUTY    30    // auto-dim floor (0-255)
// LDR calibration: raw analogRead in a bright room vs in the dark.
// The CYD's divider reads LOWER in brighter light; tune on your unit.
#define LDR_BRIGHT_RAW 300
#define LDR_DARK_RAW   3800
