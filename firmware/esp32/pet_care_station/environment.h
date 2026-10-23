#ifndef PET_CARE_ENVIRONMENT_H
#define PET_CARE_ENVIRONMENT_H

#include "environment_state.h"

// Call once from setup(), before creating tasks. Returns false on invalid GPIO
// or mutex allocation failure. Initialization does not wait for the sensor.
bool Environment_Init(void);

// Hardware read: Environment_Task is the sole caller after initialization.
// Task scheduling provides power-up settling and the DHT read interval.
// Updates the snapshot on sensor success AND failure; true means a valid sample
// was read and published. Null output, missing init, or lock failure return false.
bool Environment_Read(EnvironmentReading *out);

// Safe for other tasks. No sensor/network I/O. Copies invalid/never-read status
// too; returns false if no attempt exists, initialization failed, or lock timed out.
// Use Environment_IsFresh(*out, millis()) before using values or alert flags.
bool Environment_CopySnapshot(EnvironmentReading *out);

// Owns the DHT11 and state model; create exactly one instance. Sleeps between
// reads; performs no Wi-Fi, MQTT, UART, or actuator operations.
void Environment_Task(void *argument);

#endif
