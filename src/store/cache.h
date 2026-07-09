#pragma once
#include "state.h"

// NVS-backed last-known values — the device equivalent of the web app's
// localStorage tiers: boot paints instantly from cache (rendered dim/stale
// until the first live fetch replaces it).
void cacheLoad(TickerState& s);
// writes only metrics that have succeeded at least once this boot, so a bad
// session never clobbers good cached data
void cacheSave(const TickerState& s);
