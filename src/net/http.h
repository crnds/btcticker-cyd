#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// begin + GET; true only on HTTP 200. Caller must http.end() when done with
// the stream. Used by fees/fng/cdc, each opening its own one-shot
// WiFiClientSecure — at 60s+ cadence a persistent session gains nothing
// since idle keep-alive sockets get closed between polls anyway. price.cpp
// is the exception: it polls every second and keeps its own persistent
// keep-alive session instead of this one-shot path (see price.cpp).
bool httpGet(HTTPClient& http, WiFiClientSecure& tls, const char* url);
