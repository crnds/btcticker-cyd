#include "fng.h"
#include "http.h"
#include "config.h"
#include <ArduinoJson.h>

bool fetchFng(int& value) {
  HTTPClient http;
  if (!httpGet(http, FNG_URL)) return false;

  JsonDocument doc;   // response is ~300 bytes, no filter needed
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  const char* v = doc["data"][0]["value"];
  if (!v) return false;
  int n = atoi(v);
  if (n < 0 || n > 100) return false;
  value = n;
  return true;
}
