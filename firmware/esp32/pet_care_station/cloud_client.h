#ifndef PET_CARE_CLOUD_CLIENT_H
#define PET_CARE_CLOUD_CLIENT_H

// Check local configuration and prepare TLS/MQTT resources without connecting.
// False disables cloud startup; environment sensing can still run.
bool Cloud_Init(void);
// Create exactly one task after successful Cloud_Init. Owns all network clients.
// Sends one labelled synthetic sample per boot; no live data, UART or RPC yet.
void Cloud_Task(void *argument);

#endif
