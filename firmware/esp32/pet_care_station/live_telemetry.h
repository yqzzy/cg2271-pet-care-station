#ifndef PET_CARE_LIVE_TELEMETRY_H
#define PET_CARE_LIVE_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>
#include "environment_state.h"

struct McuTelemetry {
  bool available = false;
  bool water_enough = false;
  int32_t food_milligrams = 0;
  uint32_t received_ms = 0;
};

// False on a missing/insufficient output buffer; no partial JSON may be sent.
bool BuildLiveTelemetry(char *out, size_t capacity,
                        const EnvironmentReading &environment,
                        const McuTelemetry &mcu, uint32_t now_ms);

// Food change that triggers an early publish, so the dashboard follows the bowl quickly.
constexpr int32_t kFoodChangeMg = 2000;
// Never publish early more often than this (pouring candies changes the weight constantly).
constexpr uint32_t kEarlyPublishGapMs = 1000;

// True when the MCU readings differ enough from the last published ones to publish now:
// MCU data appears or disappears, water flips, or food moves by kFoodChangeMg or more.
bool McuChangedNotably(const McuTelemetry &published, const McuTelemetry &now);

class PeriodicTelemetry {
 public:
  bool Due(uint32_t now_ms) const { return !attempted_ || uint32_t(now_ms - last_ms_) >= 5000; }
  bool EarlyAllowed(uint32_t now_ms) const {
    return !attempted_ || uint32_t(now_ms - last_ms_) >= kEarlyPublishGapMs;
  }
  void Attempted(uint32_t now_ms) { attempted_ = true; last_ms_ = now_ms; }
  void Reconnected() { attempted_ = false; } // Send a fresh snapshot, not history.
 private:
  bool attempted_ = false;
  uint32_t last_ms_ = 0;
};

#endif
