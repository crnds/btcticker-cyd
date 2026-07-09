#pragma once

// Binance 24h ticker: price (USDT) + 24h % change. changePct is left
// untouched (NAN) if the field is missing.
bool fetchPrice(float& price, float& changePct);
