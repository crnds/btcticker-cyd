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
#define PRICE_INTERVAL_MS  1000UL
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

// status-row diagnostics (CPU/RAM/FLASH) sample + repaint cadence: the
// values only meaningfully change at ~1 s scale, and ESP.getSketchSize()/
// getFreeSketchSpace() scan flash for the running partition's end (~300 ms)
// — paying that every 25 ms loop pass was itself the dominant CPU cost
#define STATUS_REFRESH_MS  1000UL

// touch poll cadence: the 3-sample bit-bang read costs ~1.5 ms, and 20 Hz
// is still far faster than any finger tap — no need to pay it every pass
#define TOUCH_POLL_MS      50UL

// NVS snapshot cadence — mirrors the web app's 5-min localStorage snapshots;
// keeps flash wear negligible while boot always has a recent value to paint
#define CACHE_SAVE_MS      (5 * 60000UL)

// ── Burn-in mitigation ────────────────────────────────────
// This is an always-on kiosk, so the whole UI periodically nudges by a few
// px in a clockwise cycle (center -> right -> down -> left -> up -> center)
// so no single physical pixel stays lit for hours. See setOrigin() use in
// screen.cpp.
#define PIXEL_SHIFT_PX          5
#define PIXEL_SHIFT_INTERVAL_MS (3UL * 60 * 1000)

// ── CYD pins (ESP32-2432S028R) ───────────────────────────
#define PIN_BACKLIGHT  21
#define PIN_LDR        34   // photoresistor, analog
// XPT2046 sits on its own VSPI bus (not the TFT HSPI) — classic single-USB CYD
#define PIN_TOUCH_IRQ  36   // PENIRQ, LOW while touched
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_LED_R      4    // RGB LED, active LOW
#define PIN_LED_G      16
#define PIN_LED_B      17

// ── Touch calibration (raw ADC → 320×240 landscape) ────────
// Idle pressure on this unit sits ~40–70; a firm tap is usually >200.
// Tune mins/maxes from the "touch dbg: raw=..." serial lines.
#define TOUCH_X_MIN    200
#define TOUCH_X_MAX    3700
#define TOUCH_Y_MIN    240
#define TOUCH_Y_MAX    3800
#define TOUCH_Z_MIN    95    // idle noise sits ~50–80; firm tap is usually 150+
#define TOUCH_TAP_MS   350   // debounce between taps
// ADC channel naming vs screen axes (see XPT2046 notes in main.cpp)
#define TOUCH_SWAP_XY  0
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0

// ── Backlight ────────────────────────────────────────────
#define BL_CHANNEL     0
#define BL_DUTY        230   // fixed brightness, ~90% of 255 (no tap/LDR control)
// LDR calibration: raw analogRead in a bright room vs in the dark.
// The CYD's divider reads LOWER in brighter light; tune on your unit.
#define LDR_BRIGHT_RAW 300
#define LDR_DARK_RAW   3800
