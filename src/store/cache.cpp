#include "cache.h"
#include <Preferences.h>

#define NS "ticker"

void cacheLoad(TickerState& s) {
  Preferences p;
  if (!p.begin(NS, true)) return;  // read-only; fails on first boot (no namespace yet)

  s.price     = p.getFloat("price", 0);
  s.changePct = p.getFloat("chg", NAN);
  s.fng       = p.getInt("fng", -1);

  float f[4];
  if (p.getBytes("fees", f, sizeof(f)) == sizeof(f)) {
    s.fees.no = f[0]; s.fees.low = f[1]; s.fees.med = f[2]; s.fees.high = f[3];
  }

  float   diffs[30];
  uint8_t bulls[30];
  if (p.getBytes("cdcDiff", diffs, sizeof(diffs)) == sizeof(diffs) &&
      p.getBytes("cdcBull", bulls, sizeof(bulls)) == sizeof(bulls)) {
    for (int i = 0; i < 30; i++) {
      s.cdc[i].bull = bulls[i] != 0;
      s.cdc[i].diff = diffs[i];
    }
    s.cdcValid = true;
    s.cdcVersion++;
  }
  p.end();
}

void cacheSave(const TickerState& s) {
  Preferences p;
  if (!p.begin(NS, false)) return;

  if (s.priceOkMs) {
    p.putFloat("price", s.price);
    if (!isnan(s.changePct)) p.putFloat("chg", s.changePct);
  }
  if (s.feesOkMs) {
    float f[4] = {s.fees.no, s.fees.low, s.fees.med, s.fees.high};
    p.putBytes("fees", f, sizeof(f));
  }
  if (s.fngOkMs) p.putInt("fng", s.fng);
  if (s.cdcOkMs && s.cdcValid) {
    float   diffs[30];
    uint8_t bulls[30];
    for (int i = 0; i < 30; i++) {
      diffs[i] = s.cdc[i].diff;
      bulls[i] = s.cdc[i].bull ? 1 : 0;
    }
    p.putBytes("cdcDiff", diffs, sizeof(diffs));
    p.putBytes("cdcBull", bulls, sizeof(bulls));
  }
  p.end();
}
