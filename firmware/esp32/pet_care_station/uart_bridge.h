#ifndef PET_CARE_UART_BRIDGE_H
#define PET_CARE_UART_BRIDGE_H
#include "live_telemetry.h"
#include "bridge_control.h"

// Same proposed pins as the standalone UART test; confirm actual wiring.
#ifndef PET_RX_PIN
#define PET_RX_PIN 2
#endif
#ifndef PET_TX_PIN
#define PET_TX_PIN 1
#endif
bool UartBridge_Init(void);
// False on initialization failure or no packet. No network I/O.
// Snapshot contains packet arrival time; source sampling time is not on the wire.
bool UartBridge_CopySensors(McuTelemetry *out);
// loop() owns every UART consume/send; never runs on the network task.
void UartBridge_Poll(void);
ControlSnapshot UartBridge_ControlSnapshot(void);
bool UartBridge_RequestLight(bool on, uint32_t *token);
bool UartBridge_TakeLightOutcome(LightOutcome *out);
// On cloud disconnect, discard requests not yet claimed by the UART poller.
void UartBridge_CancelQueuedLight(void);
#endif
