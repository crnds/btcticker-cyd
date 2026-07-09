#include "price.h"
#include "http.h"
#include "config.h"
#include <ArduinoJson.h>

bool fetchPrice(float& price, float& changePct) {
  HTTPClient http;
  if (!httpGet(http, PRICE_URL)) return false;

  JsonDocument filter;
  filter["lastPrice"] = true;
  filter["priceChangePercent"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;

  const char* p = doc["lastPrice"];
  const char* c = doc["priceChangePercent"];
  if (!p) return false;
  price = atof(p);
  changePct = c ? atof(c) : NAN;
  return price > 0;
}
