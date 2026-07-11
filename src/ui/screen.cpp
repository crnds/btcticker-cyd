#include "screen.h"
#include "theme.h"
#include <WiFi.h>
#include "state.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "fonts/PriceFont.h"

TFT_eSPI tft;

// landscape: 1 = USB on the right (default), 3 = 180° flip (USB on the left)
static uint8_t screenRot = 1;

// 10×10 flip icon, top-right corner (logical coords after setRotation)
#define FLIP_ICON_S   10
#define FLIP_ICON_M    2   // margin from screen edge
#define FLIP_ICON_X   (320 - FLIP_ICON_S - FLIP_ICON_M)
#define FLIP_ICON_Y   FLIP_ICON_M
#define FLIP_HIT_PAD  10   // expand hit target beyond the 10×10 glyph

// ── layout (320x240 landscape) ────────────────────────────
//    2..106  price row: big price (custom PriceFont) full width
//  112..134  delta row: 24h-change pill (left), FEAR & GREED label + pill (right)
//  140..176  CDC card: 30 age-faded bars around a midline, today ringed
//  182..212  fee row: four chips (NO/LOW/MED/HIGH), micro-cap label over value
//  216..240  status row: connection dot + label, sys stats; hairline at y216
//
// Cards are borderless C_SURFACE fills — elevation comes from contrast with
// C_BG, no drawn borders (they band on the TFT at these dark levels).
// PriceFont is generated to fill PRICE_Y_H (scripts/gen_price_font.py).
#define PRICE_Y_TOP  6
#define PRICE_Y_H    104
#define PRICE_LEFT_PAD 8
#define DELTA_Y_TOP  116
#define DELTA_H      22
#define CDC_ROW_TOP  144
#define CDC_ROW_H    36
#define CDC_CARD_X   8
#define CDC_CARD_W   304
#define CDC_BASE_Y   162   // midline the bull/bear bars grow from
#define CDC_MAX_H    13
#define CDC_MIN_H    2
#define FEES_Y_TOP   186
#define FEES_Y_H     30
#define FEES_X0      8
#define CHIP_W       68
#define CHIP_GAP     6
#define STATUS_Y_TOP 218
#define STATUS_H     22

// last-drawn state — a section repaints only when what it would draw
// changed. (String-signature compares used to build 4+ heap Strings every
// 25 ms pass just to conclude "no change"; drawStatus additionally gates
// its ESP.getSketchSize()/getFreeSketchSpace() reads, which scan flash for
// the running partition's end and can cost ~300 ms, to STATUS_REFRESH_MS.)
static struct { int net, cpu, ram, flash, radius; bool force; }
    dStatus = {-1, -1, -1, -1, -1, true};
static struct { uint32_t val; bool stale, force; }
    dPrice = {0, false, true};
static struct { int fng, chg; bool fngStale, hasChg, force; }
    dDelta = {-2, 0, false, false, true};
static struct { float no, low, med, high; bool stale, force; }
    dFees = {NAN, NAN, NAN, NAN, false, true};
static uint32_t drawnCdcVersion = 0;
static bool     cdcForce   = true;
static bool     needClear  = true;

static bool feeEq(float a, float b) {
  return (isnan(a) && isnan(b)) || a == b;
}

static void loadRotation() {
  Preferences p;
  if (!p.begin("ticker", true)) return;
  uint8_t r = p.getUChar("rot", 1);
  p.end();
  screenRot = (r == 3) ? 3 : 1;
}

static void saveRotation() {
  Preferences p;
  if (!p.begin("ticker", false)) return;
  p.putUChar("rot", screenRot);
  p.end();
}

void screenInit() {
  tft.init();
  loadRotation();
  tft.setRotation(screenRot);
  tft.fillScreen(C_BG);
}

void screenInvalidate() {
  needClear = true;
  dStatus.force = true;
  dPrice.force = true;
  dDelta.force = true;
  dFees.force = true;
  cdcForce = true;
}

bool screenIsFlipped() { return screenRot == 3; }

bool screenToggleFlip() {
  screenRot = (screenRot == 1) ? 3 : 1;
  tft.setRotation(screenRot);
  saveRotation();
  screenInvalidate();
  return screenIsFlipped();
}

bool screenHitFlipIcon(int16_t x, int16_t y) {
  int x0 = FLIP_ICON_X - FLIP_HIT_PAD;
  int y0 = FLIP_ICON_Y - FLIP_HIT_PAD;
  int x1 = FLIP_ICON_X + FLIP_ICON_S + FLIP_HIT_PAD;
  int y1 = FLIP_ICON_Y + FLIP_ICON_S + FLIP_HIT_PAD;
  return x >= x0 && x < x1 && y >= y0 && y < y1;
}

// 10×10 rotate glyph (1 = foreground). Two chevrons suggesting a 180° flip.
static const uint8_t FLIP_BMP[10] = {
  0b00111100,
  0b01000010,
  0b10011001,
  0b10100101,
  0b10000001,
  0b10000001,
  0b10100101,
  0b10011001,
  0b01000010,
  0b00111100,
};

static void drawFlipIcon() {
  // clear a 1px border so fee-row repaints don't leave crumbs
  tft.fillRect(FLIP_ICON_X - 1, FLIP_ICON_Y - 1, FLIP_ICON_S + 2, FLIP_ICON_S + 2, C_BG);
  for (int row = 0; row < FLIP_ICON_S; row++) {
    uint8_t bits = FLIP_BMP[row];
    for (int col = 0; col < FLIP_ICON_S; col++) {
      if (bits & (0x80 >> col))
        tft.drawPixel(FLIP_ICON_X + col, FLIP_ICON_Y + row, C_MUTED);
    }
  }
}

void screenMessage(const char* line1, const char* line2) {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(line1, 160, 100);
  if (line2) {
    tft.setTextFont(2);
    tft.setTextColor(C_TEXT2, C_BG);
    tft.drawString(line2, 160, 140);
  }
  screenInvalidate();
}

// ── helpers ───────────────────────────────────────────────
// GFX free fonts don't paint a background, so each dirty region is cleared
// with fillRect before redrawing

// micro-cap labels: 6×8 GLCD glyphs hand-tracked +1px so tiny uppercase
// reads as deliberate typography rather than debug text
static int trackedWidth(const char* s) {
  int n = (int)strlen(s);
  return n ? n * 7 - 1 : 0;
}

static void drawTracked(const char* s, int x, int y, uint16_t color, uint16_t bg) {
  // force the built-in 6x8 GLCD font: drawChar()'s raw overload actually
  // draws whatever GFXfont was last set via setFreeFont() if one is active
  // (gfxFont is a stateful TFT_eSPI member), and every other draw* fn here
  // leaves a free font selected — without this reset each glyph renders
  // at the wrong size/metrics and overlaps the next at our fixed 7px step.
  tft.setTextFont(1);
  for (; *s; s++) {
    tft.drawChar(x, y, *s, color, bg, 1);
    x += 7;
  }
}

// tinted pill badge (the delta-row language): dark accent-tint fill,
// accent-colored bold text. Returns pill width so callers can lay out.
static int drawPill(const String& txt, int x, int yTop, uint16_t fg, uint16_t fill,
                    bool rightAlign) {
  tft.setFreeFont(&FreeSansBold9pt7b);
  int w = tft.textWidth(txt) + 20;
  if (rightAlign) x -= w;
  tft.fillSmoothRoundRect(x, yTop, w, DELTA_H, DELTA_H / 2, fill, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, fill);
  tft.drawString(txt, x + w / 2, yTop + DELTA_H / 2 + 1);
  return w;
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
  static uint32_t lastSample = 0;
  static uint32_t lastFlashSample = 0;
  static int flashPct = 0;
  uint32_t now = millis();
  bool netChanged = S.netState != dStatus.net;

  // Fast refresh of 500ms when connected/live to animate the pulsing dot
  uint32_t refreshMs = (S.netState == 1) ? 500UL : STATUS_REFRESH_MS;
  if (!dStatus.force && !netChanged && now - lastSample < refreshMs) return;
  lastSample = now;

  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapFree  = ESP.getFreeHeap();
  int ramPct = heapTotal ? (int)(100UL * (heapTotal - heapFree) / heapTotal) : 0;

  // Flash spaces only read every 60s as they are extremely slow and static
  if (lastFlashSample == 0 || now - lastFlashSample > 60000UL) {
    uint32_t sketchUsed = ESP.getSketchSize();
    uint32_t sketchFree = ESP.getFreeSketchSpace();
    flashPct = (sketchUsed + sketchFree) ?
        (int)(100UL * sketchUsed / (sketchUsed + sketchFree)) : 0;
    lastFlashSample = now;
  }

  int r = 4;
  if (S.netState == 1) {
    r = (now / 500) % 2 ? 3 : 5;
  }

  if (!dStatus.force && S.netState == dStatus.net && S.cpuPct == dStatus.cpu &&
      ramPct == dStatus.ram && flashPct == dStatus.flash && r == dStatus.radius) return;
  dStatus = {S.netState, S.cpuPct, ramPct, flashPct, r, false};

  char sCpu[8], sRom[8], sRam[8];
  snprintf(sCpu, sizeof(sCpu), "%d", S.cpuPct);
  snprintf(sRom, sizeof(sRom), "%d", flashPct);
  snprintf(sRam, sizeof(sRam), "%d", ramPct);

  TFT_eSprite spr = TFT_eSprite(&tft);
  if (spr.createSprite(320, STATUS_H)) {
    spr.fillSprite(C_BG);

    uint16_t dot = S.netState == 1 ? C_GREEN : S.netState == 0 ? C_ORANGE : C_RED;
    spr.fillSmoothCircle(14, 10, r, dot, C_BG);

    String ssid = WiFi.SSID();
    const char* label = (ssid.length() > 0) ? ssid.c_str() : (S.netState == 2 ? "offline" : "connecting...");
    spr.setTextFont(1);
    spr.setTextColor(C_TEXT2, C_BG);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(label, 26, 10);
    int labelRight = 26 + spr.textWidth(label);

    if (labelRight + 8 <= 144) {
      spr.setTextFont(1);
      int y = 10;

      // CPU block
      spr.setTextColor(C_MUTED, C_BG);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("CPU ", 144, y);
      spr.setTextDatum(MR_DATUM);
      spr.setTextColor(C_TEXT, C_BG);
      spr.drawString(sCpu, 186, y);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("%", 186, y);

      // ROM block
      spr.setTextColor(C_MUTED, C_BG);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("ROM ", 204, y);
      spr.setTextDatum(MR_DATUM);
      spr.setTextColor(C_TEXT, C_BG);
      spr.drawString(sRom, 246, y);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("%", 246, y);

      // RAM block
      spr.setTextColor(C_MUTED, C_BG);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("RAM ", 264, y);
      spr.setTextDatum(MR_DATUM);
      spr.setTextColor(C_TEXT, C_BG);
      spr.drawString(sRam, 306, y);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("%", 306, y);
    }

    spr.fillRect(0, 0, 320, 1, C_BORDER);   // hairline above the status band
    spr.pushSprite(0, STATUS_Y_TOP);
    spr.deleteSprite();
  } else {
    // fallback if sprite allocation fails
    tft.fillRect(0, STATUS_Y_TOP, 320, STATUS_H, C_BG);
    uint16_t dot = S.netState == 1 ? C_GREEN : S.netState == 0 ? C_ORANGE : C_RED;
    tft.fillSmoothCircle(14, STATUS_Y_TOP + 10, r, dot, C_BG);

    String ssid = WiFi.SSID();
    const char* label = (ssid.length() > 0) ? ssid.c_str() : (S.netState == 2 ? "offline" : "connecting...");
    tft.setTextFont(1);
    tft.setTextColor(C_TEXT2, C_BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(label, 26, STATUS_Y_TOP + 10);
    int labelRight = 26 + tft.textWidth(label);

    if (labelRight + 8 <= 144) {
      tft.setTextFont(1);
      int y = STATUS_Y_TOP + 10;

      // CPU block
      tft.setTextColor(C_MUTED, C_BG);
      tft.setTextDatum(ML_DATUM);
      tft.drawString("CPU ", 144, y);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_TEXT, C_BG);
      tft.drawString(sCpu, 186, y);
      tft.setTextDatum(ML_DATUM);
      tft.drawString("%", 186, y);

      // ROM block
      tft.setTextColor(C_MUTED, C_BG);
      tft.setTextDatum(ML_DATUM);
      tft.drawString("ROM ", 204, y);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_TEXT, C_BG);
      tft.drawString(sRom, 246, y);
      tft.setTextDatum(ML_DATUM);
      tft.drawString("%", 246, y);

      // RAM block
      tft.setTextColor(C_MUTED, C_BG);
      tft.setTextDatum(ML_DATUM);
      tft.drawString("RAM ", 264, y);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_TEXT, C_BG);
      tft.drawString(sRam, 306, y);
      tft.setTextDatum(ML_DATUM);
      tft.drawString("%", 306, y);
    }

    tft.fillRect(0, STATUS_Y_TOP, 320, 1, C_BORDER);   // hairline above the status band
  }
}

// PriceFont (custom monospace, height-filling digits) is used whenever it
// fits the full row width; falls back to FreeMonoBold24 for the rare case
// of a wider string (e.g. a 7-digit price) so it never overflows
static void drawPrice() {
  bool stale = S.priceOkMs == 0 || millis() - S.priceOkMs > PRICE_STALE_MS;
  uint32_t v = S.price > 0 ? (uint32_t)S.price : 0;
  if (!dPrice.force && v == dPrice.val && stale == dPrice.stale) return;
  dPrice = {v, stale, false};

  String txt = v > 0 ? fmtThousands(v) : "-----";
  int maxW = 320 - 2 * PRICE_LEFT_PAD;
  tft.setFreeFont(&PriceFont);
  bool useMono = tft.textWidth(txt) > maxW;
  const GFXfont* font = useMono ? &FreeMonoBold24pt7b : &PriceFont;

  TFT_eSprite spr = TFT_eSprite(&tft);
  if (spr.createSprite(320, PRICE_Y_H)) {
    spr.fillSprite(C_BG);
    spr.setFreeFont(font);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(stale ? C_DIM : C_TEXT, C_BG);
    spr.drawString(txt, PRICE_LEFT_PAD, PRICE_Y_H / 2);
    spr.pushSprite(0, PRICE_Y_TOP);
    spr.deleteSprite();
  } else {
    // fallback if sprite allocation fails
    tft.fillRect(0, PRICE_Y_TOP, 320, PRICE_Y_H, C_BG);
    tft.setFreeFont(font);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(stale ? C_DIM : C_TEXT, C_BG);
    tft.drawString(txt, PRICE_LEFT_PAD, PRICE_Y_TOP + PRICE_Y_H / 2);
  }
}

// delta row under the price: 24h change as a tinted pill on the left,
// F&G micro-label + band-tinted pill on the right
static void drawDelta() {
  bool fngStale = S.fngOkMs == 0 || millis() - S.fngOkMs > FNG_STALE_MS;
  bool hasChg = !isnan(S.changePct);
  int chg = hasChg ? (int)lroundf(S.changePct) : 0;

  if (!dDelta.force && S.fng == dDelta.fng && fngStale == dDelta.fngStale &&
      hasChg == dDelta.hasChg && (!hasChg || chg == dDelta.chg)) return;
  dDelta = {S.fng, chg, fngStale, hasChg, false};

  String fngTxt = S.fng >= 0 ? String(S.fng) : "";
  String chgTxt;
  if (hasChg) {
    char c[8];
    snprintf(c, sizeof(c), "%+d%%", chg);
    chgTxt = c;
  }

  tft.fillRect(0, DELTA_Y_TOP, 320, DELTA_H, C_BG);
  if (chgTxt.length()) {
    uint16_t fg = S.changePct >= 0 ? C_GREEN : C_RED;
    drawPill(chgTxt, PRICE_LEFT_PAD, DELTA_Y_TOP, fg, tint565(fg), false);
  }
  if (fngTxt.length()) {
    uint16_t band = fngColor(S.fng);
    uint16_t fg   = fngStale ? C_DIM : band;
    uint16_t fill = fngStale ? C_SURFACE : tint565(band);
    int w = drawPill(fngTxt, 312, DELTA_Y_TOP, fg, fill, true);
    const char* lbl = "FEAR & GREED";
    drawTracked(lbl, 312 - w - 8 - trackedWidth(lbl),
                DELTA_Y_TOP + DELTA_H / 2 - 4, C_MUTED, C_BG);
  }
}

static void drawCDC() {
  if (!cdcForce && S.cdcVersion == drawnCdcVersion) return;
  cdcForce = false;
  drawnCdcVersion = S.cdcVersion;

  tft.fillRect(0, CDC_ROW_TOP, 320, CDC_ROW_H, C_BG);
  tft.fillSmoothRoundRect(CDC_CARD_X, CDC_ROW_TOP, CDC_CARD_W, CDC_ROW_H, 10,
                          C_SURFACE, C_BG);
  if (!S.cdcValid) return;

  float mn = S.cdc[0].diff, mx = S.cdc[0].diff;
  for (int i = 1; i < 30; i++) {
    if (S.cdc[i].diff < mn) mn = S.cdc[i].diff;
    if (S.cdc[i].diff > mx) mx = S.cdc[i].diff;
  }
  float range = (mx - mn) > 0 ? (mx - mn) : 1;

  // midline the bars grow from — visible in the slot gaps, ties the strip
  tft.fillRect(CDC_CARD_X + 10, CDC_BASE_Y - 1, CDC_CARD_W - 20, 1, C_BORDER);

  // 30 slots centered in the card; bull bars grow up from the midline, bear
  // bars hang down (renderCDC() port). Bars fade with age — oldest dimmest,
  // today full color with a ring — so the strip itself encodes time.
  const int SLOT = 9, BARW = 6;
  const int X0 = CDC_CARD_X + (CDC_CARD_W - 30 * SLOT) / 2;
  for (int i = 0; i < 30; i++) {
    const CdcBlock& b = S.cdc[i];
    int h = CDC_MIN_H + (int)roundf((b.diff - mn) / range * (CDC_MAX_H - CDC_MIN_H));
    int x = X0 + i * SLOT + (SLOT - BARW) / 2;
    int y = b.bull ? CDC_BASE_Y - h : CDC_BASE_Y;
    uint16_t col = lerp565(C_SURFACE, b.bull ? C_GREEN : C_RED,
                           96 + i * 159 / 29);
    if (h >= 6) tft.fillSmoothRoundRect(x, y, BARW, h, 2, col, C_SURFACE);
    else        tft.fillRect(x, y, BARW, h, col);
    if (i == 29) tft.drawRoundRect(x - 2, y - 2, BARW + 4, h + 4, 3, C_TEXT);  // today
  }
}

static void drawFees() {
  bool stale = S.feesOkMs == 0 || millis() - S.feesOkMs > FEES_STALE_MS;
  static const char* NAMES[4] = {"NO", "LOW", "MED", "HIGH"};
  float vals[4] = {S.fees.no, S.fees.low, S.fees.med, S.fees.high};

  if (!dFees.force && stale == dFees.stale && feeEq(vals[0], dFees.no) &&
      feeEq(vals[1], dFees.low) && feeEq(vals[2], dFees.med) &&
      feeEq(vals[3], dFees.high)) return;
  dFees = {vals[0], vals[1], vals[2], vals[3], stale, false};

  tft.fillRect(0, FEES_Y_TOP, 320, FEES_Y_H, C_BG);
  for (int i = 0; i < 4; i++) {
    int cx = FEES_X0 + i * (CHIP_W + CHIP_GAP);
    tft.fillSmoothRoundRect(cx, FEES_Y_TOP, CHIP_W, FEES_Y_H, 8, C_SURFACE, C_BG);
    drawTracked(NAMES[i], cx + (CHIP_W - trackedWidth(NAMES[i])) / 2,
                FEES_Y_TOP + 5, C_MUTED, C_SURFACE);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(stale ? C_DIM : C_ORANGE, C_SURFACE);
    tft.drawString(fmtFee(vals[i]), cx + CHIP_W / 2, FEES_Y_TOP + 21);
  }
}

static void drawOffline() {
  // centered card with a wifi glyph built from smooth arcs (0° = 6 o'clock,
  // clockwise, so 135..225 sweeps over the top of the fan)
  tft.fillSmoothRoundRect(36, 27, 248, 164, 16, C_SURFACE, C_BG);
  for (int r = 10; r <= 26; r += 8)
    tft.drawSmoothArc(160, 101, r, r - 2, 135, 225, C_ORANGE, C_SURFACE);
  tft.fillSmoothCircle(160, 101, 3, C_ORANGE, C_SURFACE);

  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_TEXT, C_SURFACE);
  tft.drawString("No Wi-Fi Connection", 160, 135);

  tft.setTextFont(2);
  tft.setTextColor(C_TEXT2, C_SURFACE);
  tft.drawString("reconnecting to network...", 160, 159);
  tft.setTextColor(C_MUTED, C_SURFACE);
  tft.drawString("Setup AP: " AP_PORTAL_NAME, 160, 179);
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
      tft.fillRect(0, 0, 320, STATUS_Y_TOP, C_BG);
      drawOffline();
      offlineDrawn = true;
    }
  } else {
    drawPrice();
    drawDelta();
    drawCDC();
    drawFees();
  }

  // always last — fee-row fillRect would otherwise erase it
  drawFlipIcon();
}
