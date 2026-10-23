#ifndef PET_CARE_ENVIRONMENT_STATE_H
#define PET_CARE_ENVIRONMENT_STATE_H

#include <math.h>
#include <stdint.h>

// All timestamps use ESP32-local millis(), not the MCXC444 clock or Unix time.
struct EnvironmentReading {
  bool has_sample = false;
  bool valid = false;             // Both measurements in the latest attempt.
  bool has_last_valid = false;    // A timestamp of zero can be a valid sample.
  float temperature_c = NAN;      // Latest attempt; NAN on failure, never zeroed.
  float humidity_pct = NAN;
  float last_valid_temperature_c = NAN;
  float last_valid_humidity_pct = NAN;
  uint32_t last_attempt_ms = 0;
  uint32_t last_valid_ms = 0;
  uint32_t read_duration_ms = 0;
  uint32_t sample_count = 0;
  uint32_t failure_count = 0;
  // Latched during invalid/stale conditions. Interpret only when IsFresh is true.
  bool temperature_high = false;
  bool humidity_high = false;
};

// Returns UINT32_MAX if no valid sample has ever been received. Callers should
// also consult has_last_valid; unsigned subtraction handles one millis() wrap.
uint32_t Environment_LastValidAgeMs(const EnvironmentReading &reading,
                                    uint32_t now_ms);
bool Environment_IsFresh(const EnvironmentReading &reading, uint32_t now_ms);

// Hardware-independent state/alert logic. Owned exclusively by Environment_Task;
// other tasks consume the mutex-protected copy through Environment_CopySnapshot.
class EnvironmentState {
 public:
  void Update(float temperature_c, float humidity_pct, uint32_t attempt_ms,
              uint32_t duration_ms);
  const EnvironmentReading &Reading() const { return reading_; }

 private:
  static void UpdateAlert(float value, float high, float clear, bool &active,
                          uint8_t &confirmation_count);
  EnvironmentReading reading_;
  uint8_t temperature_confirmation_count_ = 0;
  uint8_t humidity_confirmation_count_ = 0;
};

#endif
