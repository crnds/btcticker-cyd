#include "screen.h"
#include "theme.h"
#include "state.h"
#include "config.h"
#include <TFT_eSPI.h>
#include "fonts/PriceFont.h"

TFT_eSPI tft;

// ── layout (320x240 landscape) ────────────────────────────
//   0.. 23  status row: connection dot + label, exchange name
//  26..113  price row: big price (custom PriceFont) on the left, F&G
//           stacked above 24h change in a right-hand column
// 116..196  CDC strip: 30 bars around a midline at y=156
// 204..236  fee row: No/Low/Med/High
#define PRICE_Y_TOP  26
#define PRICE_Y_H    88
#define PRICE_LEFT_PAD 6
#define SIDE_COL_X0  260   // left edge of the reserved F&G/change column
#define SIDE_COL_W   (320 - SIDE_COL_X0)
#define CDC_BASE_Y   156
#define CDC_MAX_H    38
#define CDC_MIN_H    3
#define FEES_Y_TOP   204
#define FEES_Y_H     32

// last-drawn signatures — a draw happens only when its signature changes
static String   sigStatus, sigPrice, sigSub, sigFees;
static uint32_t drawnCdcVersion = 0;
static bool     cdcForce   = true;
static bool     needClear  = true;

void screenInit() {
  tft.init();
  tft.setRotation(1);   // landscape, USB on the right
  tft.fillScreen(C_BG);
}

void screenInvalidate() {
  needClear = true;
  sigStatus = sigPrice = sigSub = sigFees = "";
  cdcForce = true;
}

void screenMessage(const char* line1, const char* line2) {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(line1, 160, 100);
  if (line2) {
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString(line2, 160, 140);
  }
  screenInvalidate();
}

// ── helpers ───────────────────────────────────────────────
// GFX free fonts don't paint a background, so each dirty region is cleared
// with fillRect before redrawing

struct Seg {
  String         txt;
  uint16_t       color;
  const GFXfont* font;
};

static String segSignature(const Seg* s, int n) {
  String sig;
  for (int i = 0; i < n; i++) {
    sig += s[i].txt;
    sig += '/';
    sig += String(s[i].color, HEX);
    sig += ';';
  }
  return sig;
}

// draw a run of differently-colored/font segments as one centered line
static void drawSegs(const Seg* s, int n, int yTop) {
  int total = 0;
  for (int i = 0; i < n; i++) {
    tft.setFreeFont(s[i].font);
    total += tft.textWidth(s[i].txt);
  }
  int x = (320 - total) / 2;
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < n; i++) {
    tft.setFreeFont(s[i].font);
    tft.setTextColor(s[i].color, C_BG);
    tft.drawString(s[i].txt, x, yTop);
    x += tft.textWidth(s[i].txt);
  }
}

static String fmtThousands(uint32_t v) {
  char raw[16];
  snprintf(raw, sizeof(raw), "%lu", (unsigned long)v);
  String out;
  int len = strlen(raw);
  for (int i = 0; i < len; i++) {
    out += raw[i];
    int rem = len - 1 - i;
    if (rem && rem % 3 == 0) out += ',';
  }
  return out;
}

// >= 1 sat/vB rounds to a whole number, below 1 keeps one decimal
// (same display rule as fmtFeeRate() in the web app)
static String fmtFee(float v) {
  if (isnan(v)) return "-";
  return v >= 1 ? String((int)lroundf(v)) : String(v, 1);
}

// ── sections ──────────────────────────────────────────────
static void drawStatus() {
  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapFree  = ESP.getFreeHeap();
  int ramPct = heapTotal ? (int)(100UL * (heapTotal - heapFree) / heapTotal) : 0;

  uint32_t sketchUsed = ESP.getSketchSize();
  uint32_t sketchFree = ESP.getFreeSketchSpace();
  int flashPct = (sketchUsed + sketchFree) ?
      (int)(100UL * sketchUsed / (sketchUsed + sketchFree)) : 0;

  String sig = String(S.netState) + "/" + String(S.cpuPct) + "/" +
               String(ramPct) + "/" + String(flashPct);
  if (sig == sigStatus) return;
  sigStatus = sig;

  tft.fillRect(0, 0, 320, 24, C_BG);
  uint16_t dot = S.netState == 1 ? C_GREEN : S.netState == 0 ? C_ORANGE : C_RED;
  tft.fillCircle(14, 12, 5, dot);
  static const char* LABELS[3] = {"connecting...", "live", "reconnecting..."};
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(LABELS[S.netState], 26, 13);

  // classic 6px/char font — three metrics don't fit the smooth 9pt font
  // in this row's width, so the debug readout gets its own compact style
  char dbg[40];
  snprintf(dbg, sizeof(dbg), "[ CPU %d%% | RAM %d%% | Flash %d%% ]",
           S.cpuPct, ramPct, flashPct);
  tft.setTextFont(1);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(dbg, 316, 13);
}

// PriceFont (custom, ~66px digits) is used whenever it fits the space left
// of the F&G/change column; falls back to the 24pt bundled font for the
// rare case of a wider string (e.g. a 7-digit price) so it never overflows
static void drawPrice() {
  bool stale = S.priceOkMs == 0 || millis() - S.priceOkMs > PRICE_STALE_MS;
  String txt = S.price > 0 ? fmtThousands((uint32_t)S.price) : "-----";
  String sig = txt + (stale ? "|s" : "");
  if (sig == sigPrice) return;
  sigPrice = sig;

  tft.fillRect(0, PRICE_Y_TOP, SIDE_COL_X0, PRICE_Y_H, C_BG);
  int maxW = SIDE_COL_X0 - 2 * PRICE_LEFT_PAD;
  tft.setFreeFont(&PriceFont);
  if (tft.textWidth(txt) > maxW) tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(stale ? C_DIM : C_TEXT, C_BG);
  tft.drawString(txt, PRICE_LEFT_PAD, PRICE_Y_TOP + PRICE_Y_H / 2);
}

// F&G stacked above the 24h change, right-aligned in their own column
static void drawSub() {
  bool fngStale = S.fngOkMs == 0 || millis() - S.fngOkMs > FNG_STALE_MS;

  String fngTxt = S.fng >= 0 ? String(S.fng) : "";
  String chgTxt;
  if (!isnan(S.changePct)) {
    char c[8];
    snprintf(c, sizeof(c), "%+d%%", (int)lroundf(S.changePct));
    chgTxt = c;
  }

  String sig = fngTxt + (fngStale ? "|s" : "") + "/" + chgTxt;
  if (sig == sigSub) return;
  sigSub = sig;

  tft.fillRect(SIDE_COL_X0, PRICE_Y_TOP, SIDE_COL_W, PRICE_Y_H, C_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(MR_DATUM);
  int x = 320 - 4;
  if (fngTxt.length()) {
    tft.setTextColor(fngStale ? C_DIM : fngColor(S.fng), C_BG);
    tft.drawString(fngTxt, x, PRICE_Y_TOP + 26);
  }
  if (chgTxt.length()) {
    tft.setTextColor(S.changePct >= 0 ? C_GREEN : C_RED, C_BG);
    tft.drawString(chgTxt, x, PRICE_Y_TOP + 62);
  }
}

static void drawCDC() {
  if (!cdcForce && S.cdcVersion == drawnCdcVersion) return;
  cdcForce = false;
  drawnCdcVersion = S.cdcVersion;

  tft.fillRect(0, CDC_BASE_Y - CDC_MAX_H - 2, 320, 2 * CDC_MAX_H + 4, C_BG);
  if (!S.cdcValid) return;

  float mn = S.cdc[0].diff, mx = S.cdc[0].diff;
  for (int i = 1; i < 30; i++) {
    if (S.cdc[i].diff < mn) mn = S.cdc[i].diff;
    if (S.cdc[i].diff > mx) mx = S.cdc[i].diff;
  }
  float range = (mx - mn) > 0 ? (mx - mn) : 1;

  // 30 slots of 10 px centered; bull bars grow up from the midline, bear
  // bars hang down, height normalized over the window (renderCDC() port)
  const int X0 = 10, SLOT = 10, BARW = 7;
  for (int i = 0; i < 30; i++) {
    const CdcBlock& b = S.cdc[i];
    int h = CDC_MIN_H + (int)roundf((b.diff - mn) / range * (CDC_MAX_H - CDC_MIN_H));
    int x = X0 + i * SLOT + (SLOT - BARW) / 2;
    int y = b.bull ? CDC_BASE_Y - h : CDC_BASE_Y;
    tft.fillRect(x, y, BARW, h, b.bull ? C_GREEN : C_RED);
    if (i == 29) tft.drawRect(x - 1, y - 1, BARW + 2, h + 2, C_TEXT);  // today
  }
}

static void drawFees() {
  bool stale = S.feesOkMs == 0 || millis() - S.feesOkMs > FEES_STALE_MS;
  static const char* NAMES[4] = {"No", "Low", "Med", "High"};
  float vals[4] = {S.fees.no, S.fees.low, S.fees.med, S.fees.high};

  Seg segs[8];
  int n = 0;
  for (int i = 0; i < 4; i++) {
    segs[n++] = {String(i ? "   " : "") + NAMES[i] + " ", C_DIM, &FreeSans9pt7b};
    segs[n++] = {fmtFee(vals[i]), stale ? C_DIM : C_ORANGE, &FreeSansBold9pt7b};
  }

  String sig = segSignature(segs, n);
  if (sig == sigFees) return;
  sigFees = sig;

  tft.fillRect(0, FEES_Y_TOP, 320, FEES_Y_H, C_BG);
  drawSegs(segs, n, FEES_Y_TOP + 6);
}

static void drawOffline() {
  // Draw warning triangle
  tft.fillTriangle(160, 55, 132, 107, 188, 107, C_ORANGE);
  tft.fillRect(158, 70, 4, 22, C_BG);
  tft.fillCircle(160, 99, 2, C_BG);

  // Draw text
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("No Wi-Fi Connection", 160, 138);

  tft.setTextColor(C_DIM, C_BG);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("reconnecting to network...", 160, 168);
  tft.drawString("Setup AP: " AP_PORTAL_NAME, 160, 198);
}

// ── render ────────────────────────────────────────────────
void screenRender() {
  static bool wasOffline = false;
  static bool offlineDrawn = false;

  bool isOffline = (S.netState == 2);
  if (isOffline != wasOffline) {
    screenInvalidate();
    wasOffline = isOffline;
    offlineDrawn = false;
  }

  if (needClear) {
    tft.fillScreen(C_BG);
    needClear = false;
  }

  drawStatus();

  if (isOffline) {
    if (!offlineDrawn) {
      tft.fillRect(0, 24, 320, 216, C_BG);
      drawOffline();
      offlineDrawn = true;
    }
  } else {
    drawPrice();
    drawSub();
    drawCDC();
    drawFees();
  }
}
