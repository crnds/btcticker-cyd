#pragma once
#include <HTTPClient.h>

// begin + GET; true only on HTTP 200. Caller must http.end() when done with
// the stream. All fetches share one TLS client and run sequentially — a
// second in-flight TLS connection would cost ~40 KB of heap.
bool httpGet(HTTPClient& http, const char* url);
