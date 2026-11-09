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

class PeriodicTelemetry {
 public:
  bool Due(uint32_t now_ms) const { return !attempted_ || uint32_t(now_ms - last_ms_) >= 5000; }
  void Attempted(uint32_t now_ms) { attempted_ = true; last_ms_ = now_ms; }
  void Reconnected() { attempted_ = false; } // Send a fresh snapshot, not history.
 private:
  bool attempted_ = false;
  uint32_t last_ms_ = 0;
};

#endif
