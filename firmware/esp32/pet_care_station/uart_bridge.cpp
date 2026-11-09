#include "uart_bridge.h"
#include "environment_config.h"
#include <Arduino.h>
#include <esp32_uart.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>

namespace {
bool attempted = false;
bool ready = false;
portMUX_TYPE control_mux = portMUX_INITIALIZER_UNLOCKED;
BridgeControl control;
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

void UartBridge_Poll(void) {
  if (!ready) return;
  uint32_t sequence;
  if (consumeButton(&sequence)) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&control_mux);
    control.Button(now);
    portEXIT_CRITICAL(&control_mux);
  }
  bool on;
  if (consumeLightAck(&on)) {
    portENTER_CRITICAL(&control_mux);
    control.Acknowledged(on);
    portEXIT_CRITICAL(&control_mux);
  }
  const UartResult result = getLightResult();
  const uint32_t now = millis();
  portENTER_CRITICAL(&control_mux);
  if (result.status == UART_TIMED_OUT) control.TimedOut();
  const bool dispatch = control.Queued(now, &on);
  // Claim before leaving the critical section: cancellation cannot race a send.
  if (dispatch) control.Claim();
  portEXIT_CRITICAL(&control_mux);
  if (dispatch) {
    const bool accepted = sendLight(on);
    portENTER_CRITICAL(&control_mux);
    if (accepted) control.Sent();
    else control.Deferred(); // Busy mutex/full TX queue: retry until queue expiry.
    portEXIT_CRITICAL(&control_mux);
  }
}

ControlSnapshot UartBridge_ControlSnapshot(void) {
  portENTER_CRITICAL(&control_mux);
  const ControlSnapshot snapshot = control.Snapshot();
  portEXIT_CRITICAL(&control_mux);
  return snapshot;
}
bool UartBridge_RequestLight(bool on, uint32_t *token) {
  if (!ready) return false;
  const uint32_t now = millis();
  portENTER_CRITICAL(&control_mux);
  const bool accepted = control.Request(on, now, token);
  portEXIT_CRITICAL(&control_mux);
  return accepted;
}
bool UartBridge_TakeLightOutcome(LightOutcome *out) {
  portENTER_CRITICAL(&control_mux);
  const bool available = control.TakeOutcome(out);
  portEXIT_CRITICAL(&control_mux);
  return available;
}
void UartBridge_CancelQueuedLight(void) {
  portENTER_CRITICAL(&control_mux);
  control.CancelQueued();
  portEXIT_CRITICAL(&control_mux);
}
