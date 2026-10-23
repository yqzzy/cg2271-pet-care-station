#include "environment_state.h"

#include "environment_config.h"

uint32_t Environment_LastValidAgeMs(const EnvironmentReading &reading,
                                    uint32_t now_ms) {
  return reading.has_last_valid ? now_ms - reading.last_valid_ms : UINT32_MAX;
}

bool Environment_IsFresh(const EnvironmentReading &reading, uint32_t now_ms) {
  return reading.valid && reading.has_last_valid &&
         Environment_LastValidAgeMs(reading, now_ms) <
             EnvironmentConfig::kFreshnessLimitMs;
}

void EnvironmentState::UpdateAlert(float value, float high, float clear,
                                    bool &active, uint8_t &confirmation_count) {
  const bool transition = active ? value < clear : value >= high;
  if (!transition) {
    confirmation_count = 0;
    return;
  }
  ++confirmation_count;
  if (confirmation_count >= EnvironmentConfig::kConfirmationSamples) {
    active = !active;
    confirmation_count = 0;
  }
}

void EnvironmentState::Update(float temperature_c, float humidity_pct,
                               uint32_t attempt_ms, uint32_t duration_ms) {
  const bool gap = reading_.has_sample &&
                   attempt_ms - reading_.last_attempt_ms >=
                       EnvironmentConfig::kFreshnessLimitMs;
  const bool valid = isfinite(temperature_c) && isfinite(humidity_pct) &&
                     humidity_pct >= 0.0f && humidity_pct <= 100.0f;
  if (gap || !valid) {
    temperature_confirmation_count_ = 0;
    humidity_confirmation_count_ = 0;
  }

  reading_.has_sample = true;
  reading_.valid = valid;
  reading_.last_attempt_ms = attempt_ms;
  reading_.read_duration_ms = duration_ms;
  ++reading_.sample_count;

  if (!valid) {
    reading_.temperature_c = NAN;
    reading_.humidity_pct = NAN;
    ++reading_.failure_count;
    return;  // Keep last-valid data and latched alarms unchanged.
  }

  reading_.temperature_c = temperature_c;
  reading_.humidity_pct = humidity_pct;
  reading_.last_valid_temperature_c = temperature_c;
  reading_.last_valid_humidity_pct = humidity_pct;
  reading_.last_valid_ms = attempt_ms;
  reading_.has_last_valid = true;
  UpdateAlert(temperature_c, EnvironmentConfig::kTemperatureHighC,
              EnvironmentConfig::kTemperatureClearC, reading_.temperature_high,
              temperature_confirmation_count_);
  UpdateAlert(humidity_pct, EnvironmentConfig::kHumidityHighPct,
              EnvironmentConfig::kHumidityClearPct, reading_.humidity_high,
              humidity_confirmation_count_);
}
