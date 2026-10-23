#ifndef PET_CARE_ENVIRONMENT_CONFIG_H
#define PET_CARE_ENVIRONMENT_CONFIG_H

#include <stdint.h>

// Proposed GPIO allocation, not a claim that hardware has been wired/tested.
// GPIO4 avoids ESP32-S2 USB GPIO19/20 and boot-strapping GPIO0/45/46.
// Override with a build flag or change this value after checking your board.
#ifndef ENVIRONMENT_DHT_PIN
#define ENVIRONMENT_DHT_PIN 4
#endif

namespace EnvironmentConfig {
constexpr int kDhtPin = ENVIRONMENT_DHT_PIN;
constexpr uint32_t kSamplePeriodMs = 5000;
constexpr uint32_t kPowerUpDelayMs = 2000;
constexpr uint32_t kFreshnessLimitMs = 15000;
constexpr float kTemperatureHighC = 30.0f;
constexpr float kTemperatureClearC = 28.0f;
constexpr float kHumidityHighPct = 80.0f;
constexpr float kHumidityClearPct = 75.0f;
constexpr uint8_t kConfirmationSamples = 2;
constexpr uint32_t kSnapshotLockTimeoutMs = 50;
constexpr uint32_t kTaskStackBytes = 4096;  // ESP-IDF task stack size is in bytes.
constexpr uint32_t kTaskPriority = 1;

static_assert(kSamplePeriodMs >= 2000, "DHT reads must be at least 2 seconds apart");
static_assert(kPowerUpDelayMs >= 1000, "Allow the DHT11 to stabilize after power-up");
static_assert(kFreshnessLimitMs > kSamplePeriodMs, "Freshness must exceed the sample period");
static_assert(kConfirmationSamples > 0, "At least one confirmation sample is required");
static_assert(kTemperatureClearC < kTemperatureHighC, "Temperature hysteresis must be positive");
static_assert(kHumidityClearPct < kHumidityHighPct, "Humidity hysteresis must be positive");
static_assert(kHumidityClearPct >= 0 && kHumidityHighPct <= 100, "Humidity limits must be percentages");
}  // namespace EnvironmentConfig

#endif
