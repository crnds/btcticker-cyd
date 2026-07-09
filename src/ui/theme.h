#pragma once
#include <stdint.h>

// RGB888 -> RGB565, compile-time
#define RGB565(r, g, b) \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// palette ported from the web app's style.css / app.js
#define C_BG      RGB565(0x00, 0x00, 0x00)
#define C_TEXT    RGB565(0xE8, 0xE8, 0xE8)
#define C_DEC     RGB565(0xBD, 0xBD, 0xBD)  // price decimals, slightly dimmer
#define C_DIM     RGB565(0x9E, 0x9E, 0x9E)  // labels / stale values
#define C_GREEN   RGB565(0x00, 0xE6, 0x76)  // positive change / bull blocks
#define C_RED     RGB565(0xFF, 0x17, 0x44)  // negative change / bear blocks
#define C_ORANGE  RGB565(0xF7, 0x93, 0x1A)  // bitcoin orange, fee values

// F&G quintile bands, fear -> greed (same hexes as app.js FNG_COLORS)
static const uint16_t FNG_COLORS[5] = {
  RGB565(0xFF, 0x17, 0x44),   // 0-19  extreme fear
  RGB565(0xFF, 0x6D, 0x00),   // 20-39 fear
  RGB565(0xFF, 0xEB, 0x3B),   // 40-59 neutral
  RGB565(0x69, 0xF0, 0xAE),   // 60-79 greed
  RGB565(0x00, 0xE6, 0x76),   // 80-100 extreme greed
};

inline uint16_t fngColor(int v) { return FNG_COLORS[v >= 100 ? 4 : v / 20]; }
