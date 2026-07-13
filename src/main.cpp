#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <XPT2046_Bitbang.h>
#include <time.h>

#include "config.h"
#include "state.h"
#include "ui/screen.h"
#include "net/price.h"
#include "net/fees.h"
#include "net/fng.h"
#include "net/cdc.h"
#include "store/cache.h"

TickerState S;

// Software-SPI XPT2046 on the dedicated CYD touch pins (MOSI/MISO/CLK/CS).
// Avoids fighting TFT_eSPI's hardware SPI / former TOUCH_CS ownership.
static XPT2046_Bitbang ts(PIN_TOUCH_MOSI, PIN_TOUCH_MISO, PIN_TOUCH_CLK,
                          PIN_TOUCH_CS, 320, 240);

// Low-level bitbang (same protocol as the lib) so we can log pressure even
// when it's below the library's touch threshold.
static void xptWrite(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_TOUCH_MOSI, (cmd >> i) & 1);
    digitalWrite(PIN_TOUCH_CLK, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    delayMicroseconds(5);
  }
  digitalWrite(PIN_TOUCH_MOSI, LOW);
  digitalWrite(PIN_TOUCH_CLK, LOW);
}
static uint16_t xptRead(uint8_t cmd) {
  xptWrite(cmd);
  uint16_t result = 0;
  for (int i = 15; i >= 0; i--) {
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_TOUCH_CLK, LOW);
    delayMicroseconds(5);
    result |= (uint16_t)digitalRead(PIN_TOUCH_MISO) << i;
  }
  return result >> 4;
}
static uint16_t xptRawZ() {
  digitalWrite(PIN_TOUCH_CS, LOW);
  uint16_t z1 = xptRead(0xB1);
  uint16_t z  = z1 + 4095;
  uint16_t z2 = xptRead(0xC1);
  z -= z2;
  // power-down
  xptRead(0xD0);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  return z > 4095 ? 0 : z;
}

// ── fetch jobs ────────────────────────────────────────────
static bool jobPrice() {
  float p, c;
  if (!fetchPrice(p, c)) return false;
  S.price = p;
  if (!isnan(c)) S.changePct = c;
  S.priceOkMs = millis();
  return true;
}

static bool jobFees() {
  FeeTiers t;
  if (!fetchFees(t)) return false;
  S.fees = t;
  S.feesOkMs = millis();
  return true;
}

static bool jobFng() {
  int v;
  if (!fetchFng(v)) return false;
  S.fng = v;
  S.fngOkMs = millis();
  return true;
}

static bool jobCdc() {
  CdcBlock b[30];
  if (!fetchCDC(b)) return false;
  memcpy(S.cdc, b, sizeof(b));
  S.cdcValid = true;
  S.cdcVersion++;
  S.cdcOkMs = millis();
  return true;
}

struct Job {
  const char* name;
  uint32_t    interval;
  bool (*run)();
  uint32_t next;      // millis() of next attempt
  uint32_t backoff;   // current retry delay
};

static Job jobs[] = {
  {"price", PRICE_INTERVAL_MS, jobPrice, 0, RETRY_BASE_MS},
  {"fees",  FEES_INTERVAL_MS,  jobFees,  0, RETRY_BASE_MS},
  {"fng",   FNG_INTERVAL_MS,   jobFng,   0, RETRY_BASE_MS},
  {"cdc",   CDC_INTERVAL_MS,   jobCdc,   0, RETRY_BASE_MS},
};

// runs at most ONE due job per pass: fetches stay sequential on the single
// TLS client, and the UI never stalls for more than one request.
// Round-robin: after running job[i], next scan starts at job[i+1] so a
// high-frequency job like price cannot starve slower ones.
static void serviceJobs() {
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  static int rrStart = 0;
  const int N = sizeof(jobs) / sizeof(jobs[0]);
  for (int i = 0; i < N; i++) {
    int idx = (rrStart + i) % N;
    Job& j = jobs[idx];
    if ((int32_t)(now - j.next) < 0) continue;
    bool ok = j.run();
    // schedule from completion, not scan start: a fetch that runs longer
    // than its interval (e.g. a slow TLS handshake) must not be instantly
    // due again, or every slow poll turns into back-to-back requests
    uint32_t done = millis();
    if (ok) {
      j.backoff = RETRY_BASE_MS;
      j.next = done + j.interval;
    } else {
      Serial.printf("[%s] fetch failed, retry in %lus\n", j.name,
                    (unsigned long)(j.backoff / 1000));
      j.next = done + j.backoff;
      j.backoff = j.backoff * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : j.backoff * 2;
    }
    rrStart = (idx + 1) % N;
    break;
  }
}

// ── connection status / RGB LED ───────────────────────────
static void updateNetState() {
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (!wifi)                 S.netState = 2;                          // reconnecting
  else if (S.priceOkMs && millis() - S.priceOkMs < PRICE_STALE_MS)
                             S.netState = 1;                          // live
  else                       S.netState = 0;                          // connecting
  // the case LED is bright — only use it as an offline beacon (active LOW)
  digitalWrite(PIN_LED_R, S.netState == 2 ? LOW : HIGH);
}

// Multi-sample bit-bang read. Mirrors the Paul Stoffregen XPT2046 sample
// order: discard the first noisy X, average the best pair of X/Y reads.
// On this controller the 0xD1 (Y-cmd) samples feed the screen X axis and
// 0x91 (X-cmd) samples feed screen Y — same as TFT_eSPI/XPT2046_Touchscreen.
static bool readTouch(int16_t& sx, int16_t& sy,
                      uint16_t* rawX = nullptr, uint16_t* rawY = nullptr,
                      uint16_t* rawZ = nullptr) {
  uint16_t bestZ = 0, bestSx = 0, bestSy = 0;

  for (int sample = 0; sample < 3; sample++) {
    digitalWrite(PIN_TOUCH_CS, LOW);
    uint16_t z1 = xptRead(0xB1);
    uint16_t z  = z1 + 4095;
    uint16_t z2 = xptRead(0xC1);
    z -= z2;

    uint16_t d0 = 0, d1 = 0, d2 = 0, d3 = 0, d4 = 0, d5 = 0;
    if (z >= TOUCH_Z_MIN) {
      xptRead(0x91);          // dummy X
      d0 = xptRead(0xD1);     // Y
      d1 = xptRead(0x91);     // X
      d2 = xptRead(0xD1);     // Y
      d3 = xptRead(0x91);     // X
      d4 = xptRead(0xD0);     // Y + power down
      d5 = xptRead(0x00);
    } else {
      xptRead(0xD0);
    }
    digitalWrite(PIN_TOUCH_CS, HIGH);

    if (z > bestZ) {
      bestZ = z;
      // median-of-closest-pair style average (same idea as besttwoavg)
      auto avg2 = [](uint16_t a, uint16_t b, uint16_t c) -> uint16_t {
        uint16_t ab = a > b ? a - b : b - a;
        uint16_t ac = a > c ? a - c : c - a;
        uint16_t bc = b > c ? b - c : c - b;
        if (ab <= ac && ab <= bc) return (a + b) / 2;
        if (ac <= ab && ac <= bc) return (a + c) / 2;
        return (b + c) / 2;
      };
      // screen-X from Y-cmd samples, screen-Y from X-cmd samples
      bestSx = avg2(d0, d2, d4);
      bestSy = avg2(d1, d3, d5);
    }
    delayMicroseconds(100);
  }

  if (rawX) *rawX = bestSx;
  if (rawY) *rawY = bestSy;
  if (rawZ) *rawZ = bestZ;
  if (bestZ < TOUCH_Z_MIN || (bestSx == 0 && bestSy == 0)) return false;

  uint16_t ax = bestSx, ay = bestSy;
#if TOUCH_SWAP_XY
  uint16_t tmp = ax; ax = ay; ay = tmp;
#endif
  int16_t x = map((int)ax, TOUCH_X_MIN, TOUCH_X_MAX, 0, 319);
  int16_t y = map((int)ay, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 239);
#if TOUCH_INVERT_X
  x = 319 - x;
#endif
#if TOUCH_INVERT_Y
  y = 239 - y;
#endif
  if (screenIsFlipped()) {
    x = 319 - x;
    y = 239 - y;
  }
  if (x < 0) x = 0; else if (x > 319) x = 319;
  if (y < 0) y = 0; else if (y > 239) y = 239;
  sx = x;
  sy = y;
  return true;
}

static void handleTouch() {
  static uint32_t lastTap = 0;
  static uint32_t lastDbg = 0;
  static uint32_t lastPoll = 0;
  static bool     wasDown = false;

  // a finger tap holds contact far longer than TOUCH_POLL_MS, so sampling
  // at 20 Hz instead of every 25 ms pass halves the bit-bang cost for free
  uint32_t pollNow = millis();
  if (pollNow - lastPoll < TOUCH_POLL_MS) return;
  lastPoll = pollNow;

  int16_t x = -1, y = -1;
  uint16_t rx = 0, ry = 0, rz = 0;
  bool ok = readTouch(x, y, &rx, &ry, &rz);
  // IRQ is unreliable on many CYD boards (stays high) — pressure is source of truth
  bool irqLow = digitalRead(PIN_TOUCH_IRQ) == LOW;

  uint32_t now = millis();
  if (ok && now - lastDbg > 800) {
    lastDbg = now;
    Serial.printf("touch dbg: z=%u raw=%u,%u -> %d,%d irq=%d\n",
                  (unsigned)rz, (unsigned)rx, (unsigned)ry,
                  (int)x, (int)y, irqLow ? 1 : 0);
  } else if (now - lastDbg > 4000) {
    lastDbg = now;
    Serial.printf("touch idle: irq=%d rawZ=%u\n", irqLow ? 1 : 0, (unsigned)rz);
  }

  // edge-trigger on press so a hold doesn't spam actions
  if (!ok) {
    wasDown = false;
    return;
  }
  if (wasDown) return;
  if (now - lastTap < TOUCH_TAP_MS) return;
  wasDown = true;
  lastTap = now;

  // [SETTINGS] button — navigates to settings page. Skip the hit test while
  // already on settings: the button's tap zone overlaps the "< BACK" label
  // there, and taking this branch would swallow the back tap.
  if (!screenIsOnSettings() && screenHitSettingsButton(x, y)) {
    screenShowSettings();
    Serial.println("-> settings page");
    return;
  }

  // Route touches to settings page handler
  if (screenIsOnSettings()) {
    int result = screenSettingsHandleTouch(x, y);
    if (result >= 0) {
      // Brightness step selected — apply immediately
      ledcWrite(BL_CHANNEL, screenGetBlDuty());
      Serial.printf("settings: brightness step %d -> duty %d\n",
                    result, (int)screenGetBlDuty());
    } else if (result == -1) {
      // Flip button tapped
      bool flipped = screenToggleFlip();
      Serial.printf("settings: flip -> %s\n", flipped ? "180" : "0");
      // Stay on settings page, redraw to show new selection
      screenShowSettings();
    } else if (result == -2) {
      // Back button
      screenShowTicker();
      Serial.println("<- ticker page");
    }
    // result == -3: dead zone, do nothing
    return;
  }

  Serial.printf("tap ignored (%d,%d z=%u raw=%u,%u)\n",
                (int)x, (int)y, (unsigned)rz,
                (unsigned)rx, (unsigned)ry);
}

// ── housekeeping ──────────────────────────────────────────
static void maybeSaveCache() {
  static uint32_t last = 0;
  if (millis() - last < CACHE_SAVE_MS) return;
  last = millis();
  cacheSave(S);
}

// same rationale as the web app's 4am location.reload(): a 24/7 appliance
// gets a fresh heap once a day. Uptime guard keeps it to one restart per day.
static void maybeDailyRestart() {
  if (millis() < 3600000UL) return;
  struct tm t;
  if (getLocalTime(&t, 0) && t.tm_hour == DAILY_RESTART_HOUR) {
    Serial.println("daily restart");
    esp_restart();
  }
}

// approximates CPU load as the fraction of each loop() spent doing work vs.
// blocked in delay() — not a true kernel/idle-task CPU metric (FreeRTOS
// runtime stats aren't enabled), but a reasonable "is the board busy" proxy.
// Note a blocking HTTP fetch will show as high "CPU" too, since the loop
// can't do anything else while waiting on the network.
static void updateCpuLoad(uint32_t loopStartUs, uint32_t loopDelayMs) {
  static float smooth = 0;
  uint32_t workUs = micros() - loopStartUs;
  float busyPct = 100.0f * workUs / (workUs + loopDelayMs * 1000UL);
  smooth += (busyPct - smooth) * 0.1f;
  S.cpuPct = (uint8_t)(smooth + 0.5f);
}

// ── setup / loop ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // RGB LED off (active LOW, and surprisingly bright in a dark room)
  const int leds[3] = {PIN_LED_R, PIN_LED_G, PIN_LED_B};
  for (int pin : leds) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  pinMode(PIN_TOUCH_IRQ, INPUT);
  ts.begin();
  ts.setCalibration(TOUCH_X_MIN, TOUCH_X_MAX, TOUCH_Y_MIN, TOUCH_Y_MAX);
  Serial.printf("touch: bitbang mosi=%d miso=%d clk=%d cs=%d irq=%d\n",
                PIN_TOUCH_MOSI, PIN_TOUCH_MISO, PIN_TOUCH_CLK,
                PIN_TOUCH_CS, PIN_TOUCH_IRQ);

  screenInit();
  // TFT_eSPI's init drove the backlight pin digitally; take it over with PWM.
  // Brightness is persisted in NVS via screenGetBlDuty().
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, screenGetBlDuty());

  // paint last-known values immediately (dim until live data arrives)
  cacheLoad(S);
  screenRender();

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setAPCallback([](WiFiManager*) {
    screenMessage("Wi-Fi setup", "Join AP \"" AP_PORTAL_NAME "\" to configure");
  });
  if (!wm.autoConnect(AP_PORTAL_NAME)) esp_restart();  // portal timed out — try again
  WiFi.setAutoReconnect(true);

  configTzTime(TZ_INFO, NTP_SERVER);   // for the daily-restart clock
  screenInvalidate();                  // wipe any portal message
}

void loop() {
  uint32_t t0 = micros();
  updateNetState();
  serviceJobs();
  handleTouch();
  maybeSaveCache();
  maybeDailyRestart();
  screenRender();
  const uint32_t LOOP_DELAY_MS = 25;
  updateCpuLoad(t0, LOOP_DELAY_MS);
  delay(LOOP_DELAY_MS);
}
