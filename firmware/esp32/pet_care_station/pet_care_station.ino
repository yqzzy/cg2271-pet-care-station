// Milestone 2 standalone bring-up: DHT11 only, no cloud, UART, or credentials.
// Members 4 and 5 will extend startup together when their modules are ready.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "environment.h"
#include "environment_config.h"

void setup(void) {
  Serial.begin(115200);
  // Do not wait for Serial/USB, Wi-Fi, or the MCXC444 before starting sensing.
  Serial.printf("[startup] Standalone DHT11 on GPIO%d; sample period=%lu ms\n",
                EnvironmentConfig::kDhtPin,
                static_cast<unsigned long>(EnvironmentConfig::kSamplePeriodMs));
  if (!Environment_Init()) {
    Serial.println("[startup] Environment initialization failed; task not started");
    return;
  }
  // Ordinary task creation works on the single-core S2; no core-1 pinning.
  if (xTaskCreate(Environment_Task, "Environment", EnvironmentConfig::kTaskStackBytes,
                  nullptr, EnvironmentConfig::kTaskPriority, nullptr) != pdPASS) {
    Serial.println("[startup] Environment task allocation failed");
    return;
  }
  Serial.println("[startup] Environment task started; waiting for sensor settling");
}

void loop(void) {
  // Sampling belongs to its own task; yield instead of busy-spinning.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
