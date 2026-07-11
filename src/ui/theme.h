#pragma once
#include <stdint.h>

// RGB888 -> RGB565, compile-time
#define RGB565(r, g, b) \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// "instrument panel" palette — deep blue-black ground, elevated slate cards,
// a cool gray label ramp, and bitcoin orange as the single saturated accent.
// Values chosen to stay distinct after RGB565 quantization (5/6/5 bits).
#define C_BG      RGB565(0x07, 0x09, 0x0D)  // ground
#define C_SURFACE RGB565(0x15, 0x1B, 0x26)  // card fill (elevation via contrast)
#define C_BORDER  RGB565(0x27, 0x30, 0x3F)  // hairlines / midlines
#define C_TEXT    RGB565(0xF2, 0xF5, 0xFA)  // hero digits, titles
#define C_TEXT2   RGB565(0x9A, 0xA7, 0xBD)  // secondary labels
#define C_MUTED   RGB565(0x5C, 0x69, 0x80)  // micro-caps, sys stats
#define C_DIM     RGB565(0x6E, 0x7A, 0x8F)  // stale (fetch-failed) values
#define C_GREEN   RGB565(0x00, 0xE1, 0x7B)  // positive change / bull blocks
#define C_RED     RGB565(0xFF, 0x3B, 0x5F)  // negative change / bear blocks
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

// per-channel blend a -> b, t = 0..255. RGB565 has no alpha, so tinted pill
// fills and the CDC age-fade are precomputed blends against bg/surface.
inline uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
  int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int32_t r = ar + ((br - ar) * t) / 255;
  int32_t g = ag + ((bg - ag) * t) / 255;
  int32_t bl = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// dark tint of an accent (~22% toward it from bg) — pill/badge fills
inline uint16_t tint565(uint16_t c) { return lerp565(C_BG, c, 56); }
