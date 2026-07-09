#pragma once
#include "state.h"

// mempool.space projected blocks -> 4 fee tiers (sat/vB), same derivation as
// the web app: high = next block, med = ~30m, low = ~1h, no = cheapest block.
bool fetchFees(FeeTiers& t);
