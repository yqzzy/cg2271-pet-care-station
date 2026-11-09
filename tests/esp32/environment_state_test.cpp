#include "environment_state.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      throw std::runtime_error("line " + std::to_string(__LINE__) + ": " +     \
                               #expression);                                  \
    }                                                                         \
  } while (false)

void NoSampleIsUnknown() {
  EnvironmentState state;
  const EnvironmentReading &r = state.Reading();
  CHECK(!r.has_sample);
  CHECK(!r.valid);
  CHECK(!r.has_last_valid);
  CHECK(std::isnan(r.temperature_c));
  CHECK(std::isnan(r.last_valid_temperature_c));
  CHECK(Environment_LastValidAgeMs(r, 100) == UINT32_MAX);
  CHECK(!Environment_IsFresh(r, 100));
}

void ValidSampleHasUnitsAndTiming() {
  EnvironmentState state;
  state.Update(25.0f, 60.0f, 2000, 23);
  const EnvironmentReading &r = state.Reading();
  CHECK(r.has_sample && r.valid && r.has_last_valid);
  CHECK(r.temperature_c == 25.0f && r.humidity_pct == 60.0f);
  CHECK(r.last_valid_temperature_c == 25.0f);
  CHECK(r.last_valid_humidity_pct == 60.0f);
  CHECK(r.last_attempt_ms == 2000 && r.last_valid_ms == 2000);
  CHECK(r.read_duration_ms == 23);
  CHECK(r.sample_count == 1 && r.failure_count == 0);
  CHECK(Environment_LastValidAgeMs(r, 7000) == 5000);
}

void FirstFailureDoesNotInventZero() {
  EnvironmentState state;
  state.Update(NAN, NAN, 2000, 25);
  const EnvironmentReading &r = state.Reading();
  CHECK(r.has_sample && !r.valid && !r.has_last_valid);
  CHECK(std::isnan(r.temperature_c) && std::isnan(r.humidity_pct));
  CHECK(std::isnan(r.last_valid_humidity_pct));
  CHECK(r.failure_count == 1 && r.sample_count == 1);
  CHECK(Environment_LastValidAgeMs(r, 3000) == UINT32_MAX);
}

void FailedSamplePreservesLastValidAndRecovers() {
  EnvironmentState state;
  state.Update(25.0f, 60.0f, 2000, 23);
  state.Update(NAN, 62.0f, 7000, 26);
  const EnvironmentReading &r = state.Reading();
  CHECK(!r.valid);
  CHECK(std::isnan(r.temperature_c) && std::isnan(r.humidity_pct));
  CHECK(r.last_valid_temperature_c == 25.0f && r.last_valid_humidity_pct == 60.0f);
  CHECK(r.last_valid_ms == 2000 && r.last_attempt_ms == 7000);
  CHECK(r.failure_count == 1 && r.sample_count == 2);
  CHECK(!Environment_IsFresh(r, 7000));
  state.Update(26.0f, 63.0f, 12000, 24);
  CHECK(r.valid && r.last_valid_ms == 12000);
  CHECK(r.temperature_c == 26.0f && r.last_valid_humidity_pct == 63.0f);
  CHECK(r.failure_count == 1 && r.sample_count == 3);
}

void NonFiniteAndImpossibleHumidityAreInvalid() {
  const float infinity = std::numeric_limits<float>::infinity();
  const float samples[][2] = {{infinity, 50}, {-infinity, 50}, {25, infinity},
                              {25, NAN}, {25, -1}, {25, 101}};
  for (const auto &sample : samples) {
    EnvironmentState state;
    state.Update(sample[0], sample[1], 2000, 25);
    CHECK(!state.Reading().valid);
    CHECK(state.Reading().failure_count == 1);
  }
}

void RealZeroIsNotAnErrorSentinel() {
  EnvironmentState state;
  state.Update(0, 0, 0, 20);
  CHECK(state.Reading().valid);
  CHECK(state.Reading().has_last_valid);
  CHECK(Environment_LastValidAgeMs(state.Reading(), 1000) == 1000);
  CHECK(Environment_IsFresh(state.Reading(), 1000));
  state.Update(25, 100, 5000, 20);
  CHECK(state.Reading().valid);
}

void TemperatureRequiresTwoConsecutiveHighSamples() {
  EnvironmentState state;
  state.Update(30, 60, 2000, 20);
  CHECK(!state.Reading().temperature_high);
  state.Update(29, 60, 7000, 20);
  state.Update(30, 60, 12000, 20);
  CHECK(!state.Reading().temperature_high);
  state.Update(31, 60, 17000, 20);
  CHECK(state.Reading().temperature_high);
  CHECK(!state.Reading().humidity_high);
}

void HumidityRequiresTwoSamplesAtInclusiveThreshold() {
  EnvironmentState state;
  state.Update(25, 80, 2000, 20);
  CHECK(!state.Reading().humidity_high);
  state.Update(25, 80, 7000, 20);
  CHECK(state.Reading().humidity_high);
  CHECK(!state.Reading().temperature_high);
}

void HysteresisRequiresTwoStrictlyBelowClearSamples() {
  EnvironmentState state;
  state.Update(30, 80, 2000, 20);
  state.Update(30, 80, 7000, 20);
  state.Update(28, 75, 12000, 20);
  state.Update(28, 75, 17000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
  state.Update(27, 74, 22000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
  state.Update(27, 74, 27000, 20);
  CHECK(!state.Reading().temperature_high && !state.Reading().humidity_high);
}

void NeutralSampleBreaksClearingSequence() {
  EnvironmentState state;
  state.Update(30, 80, 2000, 20);
  state.Update(30, 80, 7000, 20);
  state.Update(27, 74, 12000, 20);
  state.Update(29, 77, 17000, 20);
  state.Update(27, 74, 22000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
  state.Update(27, 74, 27000, 20);
  CHECK(!state.Reading().temperature_high && !state.Reading().humidity_high);
}

void InvalidSampleBreaksActivationSequence() {
  EnvironmentState state;
  state.Update(30, 80, 2000, 20);
  state.Update(NAN, NAN, 7000, 20);
  state.Update(30, 80, 12000, 20);
  CHECK(!state.Reading().temperature_high && !state.Reading().humidity_high);
  state.Update(30, 80, 17000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
}

void InvalidSamplePreservesAlarmsAndBreaksClearingSequence() {
  EnvironmentState state;
  state.Update(30, 80, 2000, 20);
  state.Update(30, 80, 7000, 20);
  state.Update(27, 74, 12000, 20);
  state.Update(NAN, NAN, 17000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
  CHECK(!Environment_IsFresh(state.Reading(), 17000));
  state.Update(27, 74, 22000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
  state.Update(27, 74, 27000, 20);
  CHECK(!state.Reading().temperature_high && !state.Reading().humidity_high);
}

void LongSamplingGapBreaksConfirmationSequence() {
  EnvironmentState state;
  state.Update(30, 80, 2000, 20);
  state.Update(30, 80, 17000, 20);
  CHECK(!state.Reading().temperature_high && !state.Reading().humidity_high);
  state.Update(30, 80, 22000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
}

void FreshnessExpiresAtFifteenSeconds() {
  EnvironmentState state;
  state.Update(25, 60, 2000, 20);
  CHECK(Environment_IsFresh(state.Reading(), 16999));
  CHECK(!Environment_IsFresh(state.Reading(), 17000));
  CHECK(!Environment_IsFresh(state.Reading(), 20000));
}

void UptimeRolloverPreservesFreshnessAndConsecutiveSamples() {
  EnvironmentState state;
  state.Update(30, 80, UINT32_MAX - 999, 20);
  CHECK(Environment_LastValidAgeMs(state.Reading(), 1000) == 2000);
  CHECK(Environment_IsFresh(state.Reading(), 1000));
  state.Update(30, 80, 4000, 20);
  CHECK(state.Reading().temperature_high && state.Reading().humidity_high);
  CHECK(Environment_LastValidAgeMs(state.Reading(), 5000) == 1000);
}

int main() {
  struct Test { const char *name; void (*run)(); };
  const Test tests[] = {
      {"no sample is unknown", NoSampleIsUnknown},
      {"valid sample units and timing", ValidSampleHasUnitsAndTiming},
      {"first failure does not invent zero", FirstFailureDoesNotInventZero},
      {"last valid sample and recovery", FailedSamplePreservesLastValidAndRecovers},
      {"invalid numeric samples", NonFiniteAndImpossibleHumidityAreInvalid},
      {"real zero is valid", RealZeroIsNotAnErrorSentinel},
      {"temperature consecutive confirmation", TemperatureRequiresTwoConsecutiveHighSamples},
      {"humidity inclusive activation", HumidityRequiresTwoSamplesAtInclusiveThreshold},
      {"strict clearing and hysteresis", HysteresisRequiresTwoStrictlyBelowClearSamples},
      {"neutral sample breaks clearing", NeutralSampleBreaksClearingSequence},
      {"invalid sample breaks activation", InvalidSampleBreaksActivationSequence},
      {"invalid sample preserves alarms", InvalidSamplePreservesAlarmsAndBreaksClearingSequence},
      {"long gap breaks confirmation", LongSamplingGapBreaksConfirmationSequence},
      {"freshness expiry boundary", FreshnessExpiresAtFifteenSeconds},
      {"uptime rollover", UptimeRolloverPreservesFreshnessAndConsecutiveSamples},
  };
  int failures = 0;
  for (const Test &test : tests) {
    try {
      test.run();
      std::printf("PASS: %s\n", test.name);
    } catch (const std::exception &error) {
      ++failures;
      std::printf("FAIL: %s: %s\n", test.name, error.what());
    }
  }
  std::printf("%zu tests, %d failures\n", sizeof(tests) / sizeof(tests[0]), failures);
  return failures == 0 ? 0 : 1;
}
