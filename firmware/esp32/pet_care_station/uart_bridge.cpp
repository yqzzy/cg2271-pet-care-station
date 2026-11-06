#include "uart_bridge.h"
#include "environment_config.h"
#include <Arduino.h>
#include <esp32_uart.h>
#include <driver/gpio.h>

namespace {
bool attempted = false;
bool ready = false;
}
bool UartBridge_Init(void) {
  if (attempted) return ready;
  attempted = true;
  if (!GPIO_IS_VALID_GPIO(PET_RX_PIN) || !GPIO_IS_VALID_OUTPUT_GPIO(PET_TX_PIN) ||
      PET_RX_PIN == PET_TX_PIN || PET_RX_PIN == EnvironmentConfig::kDhtPin ||
      PET_TX_PIN == EnvironmentConfig::kDhtPin) {
    Serial.println("[bridge] Invalid/conflicting UART pins; sensor bridge disabled");
    return false;
  }
  ready = uartInit(Serial1, PET_RX_PIN, PET_TX_PIN);
  Serial.printf("[bridge] UART %s: RX=%d TX=%d at 9600 baud\n",
                ready ? "ready" : "initialization failed", PET_RX_PIN, PET_TX_PIN);
  return ready;
}
bool UartBridge_CopySensors(McuTelemetry *out) {
  if (!out) return false;
  *out = McuTelemetry{};
  if (!ready) return false;
  SensorValues values;
  if (!getSensors(&values)) return false;
  out->available = true;
  out->food_milligrams = values.food_milligrams;
  out->water_enough = values.water_enough;
  out->received_ms = values.received_ms;
  return true;
}
