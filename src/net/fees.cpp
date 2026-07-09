#include "fees.h"
#include "http.h"
#include "config.h"
#include <ArduinoJson.h>

bool fetchFees(FeeTiers& t) {
  HTTPClient http;
  if (!httpGet(http, FEES_URL)) return false;

  // the response nests a large feeRange array per block — the filter keeps
  // only medianFee, so the parse stays a few hundred bytes regardless
  JsonDocument filter;
  filter[0]["medianFee"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err || !doc.is<JsonArray>() || doc.size() == 0) return false;

  JsonArray blocks = doc.as<JsonArray>();
  int n = blocks.size();
  // indices clamped so a near-empty mempool (one projected block) collapses
  // gracefully, exactly like deriveTiers() in the web app
  auto med = [&](int i) { return blocks[i < n - 1 ? i : n - 1]["medianFee"].as<float>(); };
  t.high = med(0);
  t.med  = med(2);
  t.low  = med(5);
  t.no   = med(n - 1);
  return !isnan(t.high);
}
