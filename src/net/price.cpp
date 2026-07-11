#include "price.h"
#include "config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Price polls at 1 Hz — a fresh TLS handshake per poll costs ~1-2 s of
// mbedTLS CPU and was the main-loop's biggest hog. This module keeps ONE
// keep-alive HTTP/1.1 session to Binance so a steady-state poll is a single
// request on the already-open socket. fees/fng/cdc stay on httpGet()'s
// one-shot HTTP/1.0 path (src/net/http.cpp): at 60 s+ cadence the server
// closes idle sockets between polls anyway, and cdc's parseCloses() depends
// on unchunked streaming, which reuse would break.
bool fetchPrice(float& price, float& changePct) {
  // persistent session: ~45 KB heap held while connected — same peak as the
  // old per-poll alloc/free, but without the once-per-second fragmentation
  static WiFiClientSecure tls;
  static HTTPClient http;
  static bool fresh = true;      // true -> (re)configure + full handshake next GET

  if (fresh) {
    tls.setInsecure();           // v1: no cert pinning (see README roadmap)
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    http.setReuse(true);         // default, kept explicit: HTTP/1.1 keep-alive
    fresh = false;
    // NOTE: never call useHTTP10() here — it forces _reuse=false in
    // HTTPClient, and an HTTP/1.0 response can't keep the socket alive.
  }

  // begin() on an unchanged host is cheap and never drops a reusable
  // socket; GET() reuses the open connection when the server honored
  // keep-alive, otherwise transparently re-handshakes.
  bool ok = http.begin(tls, PRICE_URL);
  int code = ok ? http.GET() : -1;
  if (code != HTTP_CODE_OK) {
    Serial.printf("price GET failed: code %d\n", code);
    // full teardown so the next attempt (after serviceJobs' backoff) starts
    // with a clean TCP+TLS handshake — covers the server-closed-idle-socket
    // and half-open cases where GET() returns a negative HTTPC_ERROR_*
    http.end();
    tls.stop();
    fresh = true;
    return false;
  }

  // HTTP/1.1 may answer chunked; getString() decodes both chunked and
  // Content-Length framing (raw getStream() would hand ArduinoJson the
  // chunk-size headers instead of JSON). Body is ~700 B — buffering it is
  // trivial.
  String body = http.getString();
  http.end();                    // keeps the socket open when reusable

  JsonDocument filter;
  filter["lastPrice"] = true;
  filter["priceChangePercent"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) return false;

  const char* p = doc["lastPrice"];
  const char* c = doc["priceChangePercent"];
  if (!p) return false;
  price = atof(p);
  changePct = c ? atof(c) : NAN;
  return price > 0;
}
