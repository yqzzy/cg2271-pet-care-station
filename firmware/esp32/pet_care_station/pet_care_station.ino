// DHT11 and UART sensor snapshots published together through ThingsBoard MQTT.
// UART acknowledgements run independently of cloud connection attempts.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "environment.h"
#include "environment_config.h"
#include "cloud_client.h"
#include "uart_bridge.h"

void setup(void) {
  Serial.begin(115200);
  // Do not wait for Serial/USB, Wi-Fi, or the MCXC444 before starting sensing.
  Serial.printf("[startup] DHT11 on GPIO%d; sample period=%lu ms\n",
                EnvironmentConfig::kDhtPin,
                static_cast<unsigned long>(EnvironmentConfig::kSamplePeriodMs));
  if (!Environment_Init()) {
    Serial.println("[startup] Environment initialization failed; task not started");
  } else {
    // Ordinary task creation works on the single-core S2; no core-1 pinning.
    if (xTaskCreate(Environment_Task, "Environment", EnvironmentConfig::kTaskStackBytes,
                    nullptr, EnvironmentConfig::kTaskPriority, nullptr) != pdPASS) {
      Serial.println("[startup] Environment task allocation failed");
    } else {
      Serial.println("[startup] Environment task started; waiting for sensor settling");
    }
  }
  // Failure leaves MCU telemetry unavailable; DHT11 and cloud still operate.
  UartBridge_Init();
  if (Cloud_Init()) {
    // Network work starts only in its own task, after environment task creation.
    if (xTaskCreate(Cloud_Task, "Cloud", 8192, nullptr, 1, nullptr) != pdPASS) {
      Serial.println("[startup] Cloud task allocation failed; sensing continues");
    }
  }
}

void loop(void) {
  UartBridge_Poll();
  vTaskDelay(pdMS_TO_TICKS(20));
}
