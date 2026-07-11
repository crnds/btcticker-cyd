#pragma once
#include <stdint.h>

void screenInit();
// repaints only the regions whose content changed since the last call —
// cheap enough to run every loop pass
void screenRender();
// full-screen two-line notice (Wi-Fi portal etc.); screenRender() repaints
// the ticker after screenInvalidate()
void screenMessage(const char* line1, const char* line2);
void screenInvalidate();

// 180° flip (rotation 1 ↔ 3). Persisted in NVS. Returns true if flipped.
bool screenToggleFlip();
bool screenIsFlipped();
// hit-test for the bottom-right flip icon (includes a finger-friendly pad)
bool screenHitFlipIcon(int16_t x, int16_t y);
