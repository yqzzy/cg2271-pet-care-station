#include "environment.h"

#include <Arduino.h>
#include <DHT.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "environment_config.h"

namespace {
DHT sensor(static_cast<uint8_t>(EnvironmentConfig::kDhtPin), DHT11);
SemaphoreHandle_t snapshot_mutex = nullptr;
EnvironmentState state;  // Only Environment_Task updates this model.
EnvironmentReading snapshot;
bool initialized = false;

void LogReading(const EnvironmentReading &reading) {
  if (!Serial) {
    return;  // Native USB need not be connected for sampling to continue.
  }
  const uint32_t now_ms = millis();
  Serial.printf(
      "[environment] sample=%lu valid=%u temp_c=%.1f humidity_pct=%.1f "
      "last_temp_c=%.1f last_humidity_pct=%.1f has_last_valid=%u age_ms=%lu "
      "read_ms=%lu failures=%lu alerts_known=%u temp_high=%u humidity_high=%u\n",
      static_cast<unsigned long>(reading.sample_count),
      static_cast<unsigned>(reading.valid), reading.temperature_c,
      reading.humidity_pct, reading.last_valid_temperature_c,
      reading.last_valid_humidity_pct,
      static_cast<unsigned>(reading.has_last_valid),
      static_cast<unsigned long>(Environment_LastValidAgeMs(reading, now_ms)),
      static_cast<unsigned long>(reading.read_duration_ms),
      static_cast<unsigned long>(reading.failure_count),
      static_cast<unsigned>(Environment_IsFresh(reading, now_ms)),
      static_cast<unsigned>(reading.temperature_high),
      static_cast<unsigned>(reading.humidity_high));
}
}  // namespace

bool Environment_Init(void) {
  if (initialized) {
    return true;
  }
  // DHT communication needs a GPIO that can both drive and read the data line.
  if (!GPIO_IS_VALID_OUTPUT_GPIO(EnvironmentConfig::kDhtPin)) {
    Serial.println("[environment] Invalid DHT data GPIO; check environment_config.h");
    return false;
  }
  snapshot_mutex = xSemaphoreCreateMutex();
  if (snapshot_mutex == nullptr) {
    Serial.println("[environment] Cannot allocate snapshot mutex");
    return false;
  }
  sensor.begin();
  initialized = true;
  return true;
}

bool Environment_Read(EnvironmentReading *out) {
  if (out == nullptr) {
    return false;
  }
  *out = EnvironmentReading{};
  if (!initialized) {
    return false;
  }

  const uint32_t started_ms = millis();
  // The second getter uses the same cached DHT transaction (no forced read),
  // keeping humidity and Celsius temperature from one checksum-validated frame.
  const float humidity_pct = sensor.readHumidity();
  const float temperature_c = sensor.readTemperature();
  state.Update(temperature_c, humidity_pct, started_ms, millis() - started_ms);
  *out = state.Reading();

  // GPIO I/O above and serial logging below are outside this lock.
  if (xSemaphoreTake(snapshot_mutex,
                     pdMS_TO_TICKS(EnvironmentConfig::kSnapshotLockTimeoutMs)) != pdTRUE) {
    Serial.println("[environment] Snapshot publication timed out");
    return false;
  }
  snapshot = *out;
  xSemaphoreGive(snapshot_mutex);
  return out->valid;
}

bool Environment_CopySnapshot(EnvironmentReading *out) {
  if (out == nullptr) {
    return false;
  }
  *out = EnvironmentReading{};
  if (!initialized ||
      xSemaphoreTake(snapshot_mutex,
                     pdMS_TO_TICKS(EnvironmentConfig::kSnapshotLockTimeoutMs)) != pdTRUE) {
    return false;
  }
  *out = snapshot;
  xSemaphoreGive(snapshot_mutex);
  return out->has_sample;
}

void Environment_Task(void *argument) {
  (void)argument;
  if (!initialized) {
    Serial.println("[environment] Call Environment_Init before creating the task");
    vTaskDelete(nullptr);
    return;
  }

  // Let the module settle without blocking setup() or any other task.
  vTaskDelay(pdMS_TO_TICKS(EnvironmentConfig::kPowerUpDelayMs));
  const TickType_t period = pdMS_TO_TICKS(EnvironmentConfig::kSamplePeriodMs);
  for (;;) {
    TickType_t next_wake = xTaskGetTickCount();
    EnvironmentReading reading;
    Environment_Read(&reading);
    LogReading(reading);  // Invalid attempts are logged too; no immediate retries.
    // Do not run a burst of cached reads to catch up after a delayed iteration.
    // Normally the period is measured from this read's start; after an overrun,
    // wait a full period rather than retrying immediately.
    if (xTaskGetTickCount() - next_wake >= period) {
      next_wake = xTaskGetTickCount();
    }
    vTaskDelayUntil(&next_wake, period);
  }
}
