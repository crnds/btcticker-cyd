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

// [SETTINGS] button (top-left of ticker) hit-test — includes finger-friendly pad
bool screenHitSettingsButton(int16_t x, int16_t y);

// Page navigation: settings page ↔ ticker page
void screenShowSettings();
void screenShowTicker();
bool screenIsOnSettings();

// Call when a touch lands on the settings page.
// Returns: 0-4 = brightness step, -1 = flip row tapped, -2 = back, -3 = dead zone
int  screenSettingsHandleTouch(int16_t x, int16_t y);

// Current backlight PWM duty (0-255), driven by the settings brightness step
uint8_t screenGetBlDuty();
