#pragma once
#include <Arduino.h>

// one CDC Action Zone day: bull = EMA12 > EMA26, diff = |EMA12 - EMA26|
// (bar height is normalized over the 30-day window at render time)
struct CdcBlock {
  bool  bull;
  float diff;
};

struct FeeTiers {
  float no, low, med, high;   // sat/vB
};

// single shared state — written by the fetch jobs, read by the renderer.
// Everything runs on the main loop task, so no locking is needed.
struct TickerState {
  float    price      = 0;
  float    changePct  = NAN;
  uint32_t priceOkMs  = 0;    // millis() of last successful fetch; 0 = never
  FeeTiers fees       = {NAN, NAN, NAN, NAN};
  uint32_t feesOkMs   = 0;
  int      fng        = -1;   // 0-100, -1 = unknown
  uint32_t fngOkMs    = 0;
  CdcBlock cdc[30]    = {};
  bool     cdcValid   = false;
  uint32_t cdcVersion = 0;    // bumped on new data so the UI knows to repaint
  uint32_t cdcOkMs    = 0;
  uint8_t  netState   = 0;    // 0 connecting, 1 live, 2 reconnecting
  uint8_t  cpuPct     = 0;    // main-loop busy time, smoothed (see updateCpuLoad())
};

extern TickerState S;
