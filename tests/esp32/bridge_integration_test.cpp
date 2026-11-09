#include <cassert>
#include <cstdio>
#include <stdexcept>
#define ESP_UART_DEBUG 0
#include "../../firmware/esp32/libraries/PetStationUart/src/esp32_uart.cpp"
#include "../../firmware/esp32/pet_care_station/uart_bridge.cpp"

namespace fake {
uint32_t now = 100;
unsigned mutexDepth = 0;
unsigned ioWhileLocked = 0;
unsigned queueReceivesLeft = 0;
}
DebugSerial Serial;
HardwareSerial Serial1;

int main() {
  assert(UartBridge_Init());
  uint32_t token;
  assert(UartBridge_RequestLight(true, &token));
  assert(xSemaphoreTake(stateMutex, 0) == pdPASS);
  UartBridge_Poll(); // RX/TX task owns driver mutex: defer, never block/fail.
  auto snapshot = UartBridge_ControlSnapshot();
  assert(snapshot.light_pending && !snapshot.light_known);
  LightOutcome outcome;
  assert(!UartBridge_TakeLightOutcome(&outcome));
  assert(lightResult.sequence == 0);
  xSemaphoreGive(stateMutex);
  UartBridge_Poll(); // Same request is now accepted by actual sendLight().
  assert(lightResult.status == UART_WAITING && lightResult.sequence == 1);
  UartMessage ack = {"LIGHT_ACK:1*43\n"};
  handleReceived(&ack);
  UartBridge_Poll();
  assert(UartBridge_TakeLightOutcome(&outcome));
  assert(outcome.success && outcome.on && outcome.token == token);
  assert(UartBridge_ControlSnapshot().light_known);

  UartMessage button = {"BUTTON:1*1D\n"};
  handleReceived(&button);
  UartBridge_Poll();
  assert(UartBridge_ControlSnapshot().button_presses == 1);
  button = UartMessage{"BUTTON:1*1D\n"};
  handleReceived(&button);
  UartBridge_Poll();
  assert(UartBridge_ControlSnapshot().button_presses == 1);

  assert(UartBridge_RequestLight(false, &token));
  UartBridge_CancelQueuedLight();
  UartBridge_Poll();
  assert(UartBridge_TakeLightOutcome(&outcome) && !outcome.success);
  assert(lightResult.sequence == 1); // Cancelled request never reached driver.
  assert(fake::ioWhileLocked == 0 && fakeCriticalDepth() == 0);
  puts("PASS: actual bridge/driver mutex contention retry, correlated ACK, button and cancellation");
}
