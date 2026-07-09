#pragma once
#include "state.h"

// CDC Action Zone: fetch the last 100 daily closes and compute 30 days of
// EMA12/EMA26 bull/bear blocks (last block = today, still-forming candle).
bool fetchCDC(CdcBlock* blocks30);
