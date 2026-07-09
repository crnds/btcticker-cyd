#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
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
// TLS client, and the UI never stalls for more than one request
static void serviceJobs() {
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  for (Job& j : jobs) {
    if (now < j.next) continue;
    if (j.run()) {
      j.backoff = RETRY_BASE_MS;
      j.next = now + j.interval;
    } else {
      Serial.printf("[%s] fetch failed, retry in %lus\n", j.name,
                    (unsigned long)(j.backoff / 1000));
      j.next = now + j.backoff;
      j.backoff = j.backoff * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : j.backoff * 2;
    }
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

// ── backlight: tap to cycle Auto -> 100% -> 55% -> 25% ────
static uint8_t blMode = 0;   // 0 = auto (LDR), 1..3 = fixed

static void handleTouch() {
  static uint32_t lastTap = 0;
  // XPT2046 PENIRQ sits LOW while the panel is pressed — good enough for a
  // tap detector without running the touch controller's SPI bus
  if (digitalRead(PIN_TOUCH_IRQ) == LOW && millis() - lastTap > 400) {
    lastTap = millis();
    blMode = (blMode + 1) % 4;
    Serial.printf("backlight mode %u\n", blMode);
  }
}

static void updateBacklight() {
  static uint32_t lastMs = 0;
  static float    smooth = 255;
  if (millis() - lastMs < 500) return;
  lastMs = millis();

  int duty;
  if (blMode == 0) {
    int raw = analogRead(PIN_LDR);   // lower raw = brighter room on the CYD
    float dark = (raw - LDR_BRIGHT_RAW) / (float)(LDR_DARK_RAW - LDR_BRIGHT_RAW);
    dark = dark < 0 ? 0 : dark > 1 ? 1 : dark;
    duty = BL_MIN_DUTY + (int)((1.0f - dark) * (255 - BL_MIN_DUTY));
  } else {
    duty = blMode == 1 ? 255 : blMode == 2 ? 140 : 64;
  }
  smooth += (duty - smooth) * 0.3f;   // ease changes so dimming never pops
  ledcWrite(BL_CHANNEL, (int)smooth);
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

  screenInit();
  // TFT_eSPI's init drove the backlight pin digitally; take it over with PWM
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 255);

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
  updateNetState();
  serviceJobs();
  handleTouch();
  updateBacklight();
  maybeSaveCache();
  maybeDailyRestart();
  screenRender();
  delay(25);
}
