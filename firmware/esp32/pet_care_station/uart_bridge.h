#ifndef PET_CARE_UART_BRIDGE_H
#define PET_CARE_UART_BRIDGE_H
#include "live_telemetry.h"

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
#endif
