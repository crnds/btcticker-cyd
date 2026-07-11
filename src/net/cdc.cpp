#include "cdc.h"
#include "http.h"
#include "config.h"

#define MAX_CLOSES 100
#define CDC_DAYS   30

// Binance klines come as [[openTime,"open","high","low","close","vol",...],...].
// The full payload is ~25 KB; buffering it in a JSON document would hold every
// candle's strings in RAM, so instead extract field 4 (close) of each candle
// straight off the TLS stream with a bracket-depth scanner.
static int parseCloses(HTTPClient& http, float* closes) {
  Stream& s = http.getStream();
  int  depth = 0, field = 0, n = 0;
  bool inStr = false;
  char buf[24];
  int  bl = 0;
  uint32_t lastByte = millis();

  while (millis() - lastByte < HTTP_TIMEOUT_MS) {
    int c = s.read();
    if (c < 0) {
      if (!http.connected()) break;
      delay(2);
      continue;
    }
    lastByte = millis();
    char ch = (char)c;

    if (inStr) {
      if (ch == '"') inStr = false;
      else if (depth == 2 && field == 4 && bl < (int)sizeof(buf) - 1) buf[bl++] = ch;
      continue;
    }
    switch (ch) {
      case '"': inStr = true; break;
      case '[':
        depth++;
        if (depth == 2) { field = 0; bl = 0; }
        break;
      case ',':
        if (depth == 2) { field++; if (field == 4) bl = 0; }
        break;
      case ']':
        if (depth == 2) {          // candle finished
          buf[bl] = '\0';
          float v = atof(buf);
          if (n < MAX_CLOSES) closes[n++] = v;
        }
        depth--;
        if (depth == 0) return n;  // end of the outer array
        break;
    }
  }
  return 0;  // timed out mid-body
}

// straight port of calcEMA() from the web app's app.js
static void calcEMA(const float* c, int n, int period, float* out) {
  float k = 2.0f / (period + 1);
  float seed = 0;
  for (int i = 0; i < period; i++) seed += c[i];
  out[period - 1] = seed / period;
  for (int i = period; i < n; i++) out[i] = c[i] * k + out[i - 1] * (1 - k);
}

bool fetchCDC(CdcBlock* blocks30) {
  WiFiClientSecure tls;
  HTTPClient http;
  if (!httpGet(http, tls, KLINES_URL)) return false;

  float closes[MAX_CLOSES];
  int n = parseCloses(http, closes);
  http.end();
  if (n < CDC_DAYS + 26) {
    Serial.printf("fetchCDC parsed %d, need %d\n", n, CDC_DAYS + 26);
    return false;  // 30 blocks + EMA26 warmup
  }

  float ema12[MAX_CLOSES], ema26[MAX_CLOSES];
  calcEMA(closes, n, 12, ema12);
  calcEMA(closes, n, 26, ema26);

  for (int i = 0; i < CDC_DAYS; i++) {
    int idx = n - CDC_DAYS + i;
    blocks30[i].bull = ema12[idx] > ema26[idx];
    blocks30[i].diff = fabsf(ema12[idx] - ema26[idx]);
  }
  return true;
}
