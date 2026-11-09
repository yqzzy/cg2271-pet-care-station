#include "live_telemetry.h"
#include <math.h>
#include <stdio.h>

bool BuildLiveTelemetry(char *out, size_t capacity,
                        const EnvironmentReading &environment,
                        const McuTelemetry &mcu, uint32_t now_ms) {
  if (!out || !capacity) return false;
  out[0] = '\0';
  const bool env_ok = Environment_IsFresh(environment, now_ms) &&
                      isfinite(environment.temperature_c) && isfinite(environment.humidity_pct);
  const bool mcu_ok = mcu.available && uint32_t(now_ms - mcu.received_ms) < 3000;
  char env_fields[160] = "";
  char mcu_fields[128] = "";
  char env_age[48] = "";
  char mcu_age[48] = "";
  // These intermediate buffers bound fixed-format fields and finite float values.
  // Check every snprintf result so unusually large values never produce partial JSON.
  int env_length = 0, mcu_length = 0;
  if (env_ok) {
    env_length = snprintf(env_fields, sizeof(env_fields),
        ",\"temperature_c\":%.1f,\"humidity_pct\":%.1f,\"temperature_high\":%s,\"humidity_high\":%s",
        environment.temperature_c, environment.humidity_pct,
        environment.temperature_high ? "true" : "false", environment.humidity_high ? "true" : "false");
  }
  if (mcu_ok) {
    mcu_length = snprintf(mcu_fields, sizeof(mcu_fields),
        ",\"food_weight_g\":%.3f,\"water_enough\":%s,\"water_low\":%s",
        static_cast<double>(mcu.food_milligrams) / 1000.0,
        mcu.water_enough ? "true" : "false", mcu.water_enough ? "false" : "true");
  }
  if (env_length < 0 || static_cast<size_t>(env_length) >= sizeof(env_fields) ||
      mcu_length < 0 || static_cast<size_t>(mcu_length) >= sizeof(mcu_fields)) return false;
  if (environment.has_last_valid)
    snprintf(env_age, sizeof(env_age), ",\"environment_age_ms\":%lu",
             static_cast<unsigned long>(Environment_LastValidAgeMs(environment, now_ms)));
  if (mcu.available)
    snprintf(mcu_age, sizeof(mcu_age), ",\"mcu_age_ms\":%lu",
             static_cast<unsigned long>(uint32_t(now_ms - mcu.received_ms)));
  const int length = snprintf(out, capacity,
      "{\"data_source\":\"live\",\"environment_valid\":%s,\"mcu_data_fresh\":%s%s%s%s%s}",
      env_ok ? "true" : "false", mcu_ok ? "true" : "false",
      env_age, mcu_age, env_fields, mcu_fields);
  if (length < 0 || static_cast<size_t>(length) >= capacity) {
    out[0] = '\0';
    return false;
  }
  return true;
}
