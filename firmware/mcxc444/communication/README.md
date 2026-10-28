# MCXC444 UART public API

**Testing only:** `mcxc_test_main.c` is a standalone UART test entry point. It is not required for integration; do not add it to the integrated application or compile it alongside your application's `main.c`. Integrate `mcxc_uart.h` and `mcxc_uart.c` instead, and call their functions from your application's own startup and tasks as described below.

For teammates connecting food/water sensing, button events and light control to the ESP32. Include `mcxc_uart.h` and compile `mcxc_uart.c` in the MCXC444 project. The `uart_link.c/.h` files are application adapter templates; they do not implement this API.

## Startup

```c
bool uartInit(void);
```

Call once after board, clock and debug-console initialization, before starting the FreeRTOS scheduler. Returns `true` on success. If it returns `false`, stop startup; do not call the other functions or retry initialization.

The module creates its own queues, mutex and UART TX/RX tasks. Teammates do not need to create those tasks or implement packet parsing, checksums, ACKs or retransmission.

The link uses UART2, PTE22 TX and PTE23 RX, at 9600 baud, 8N1. Connect TX to the ESP32 RX, RX to the ESP32 TX, and common GND. Keep the debug console separate and ensure there is only one `UART2_FLEXIO_IRQHandler` implementation.

## Send sensor readings — sensing/UART adapter

```c
bool sendSensors(uint16_t water_level, int32_t food_milligrams);
```

- Sends both readings in one packet. Water is raw ADC counts; food is signed milligrams, e.g. 125 g = 125000 mg.
- Returns `true` when queued locally, `false` when the TX queue is full. Queued does not mean received by the ESP32.
- Sensor packets have no ACK or automatic retry. If the queue is full, send a fresh sample on the next reporting cycle.
- Copy shared readings under the sensor-state mutex, release the mutex, then call this function. Skip invalid readings; this packet has no validity fields.

Example after obtaining a valid snapshot and converting food to milligrams:

```c
bool queued = sendSensors(water_raw, food_mg);
// If !queued, keep sensing and try a fresh sample next reporting cycle.
```

## Send a button event — debounced button task or UART adapter

```c
bool sendButton(void);
```

Call once for each accepted, debounced button press. The existing `Button_Task` queues `TEL_BUTTON_PRESSED` into `telemetryQueue`; a UART adapter can consume that event and call this function.

- `true`: event accepted locally. The module handles its sequence, ACK matching and up to two retries automatically (three total transmission attempts). Do not resubmit that same accepted event every loop.
- `false`: another button event is awaiting ACK, the TX queue is full, or sequence IDs are exhausted. Retain the unsent event in application state and retry later with a bounded delay.
- The ESP32 ACK means its application consumed the event. Cloud delivery is handled separately on the ESP32.

## Optional button status

```c
UartResult getButtonResult(void);
```

Returns the latest button request's `sequence` and `status`. Reading it does not clear the status.

| Status | Meaning |
|---|---|
| `UART_IDLE` | No button request submitted yet |
| `UART_WAITING` | Accepted request is waiting for ACK |
| `UART_CONSUMED` | ESP32 application consumed the button event |
| `UART_TIMED_OUT` | Retry limit reached without a matching ACK; outcome unknown |

## Consume a light command — actuator task

```c
bool consumeLight(bool *on);
```

Check regularly from the task that owns the light output:

```c
bool on;
if (consumeLight(&on)) {
    Feedback_SetLight(on);
}
```

- Returns `true` once for a pending command and writes the requested setting into `*on`: `true` = ON, `false` = OFF.
- Clears the pending command and automatically requests its ACK. Apply the GPIO setting immediately afterward, as shown above; no second confirmation call is needed.
- Returns `false` if no command is pending or the pointer is null. Only use the output value when the function returns `true`.
- Retransmissions of an already consumed request are acknowledged again without re-delivering it to the actuator task.

For this project's immediate LED write, use the consumption ACK as completion of the light setting. The ACK is requested inside `consumeLight()`, before the caller writes the GPIO; it does not measure physical illumination.

## Calling rules

- After successful initialization, call all public send/get/consume functions from tasks, never from a GPIO ISR.
- Use periodic delays or bounded waits in application tasks so light commands and other work continue to be serviced.
- Reset both boards together for the current demo: sequence counters restart on boot and there is no session recovery.
- An ACK timeout has an unknown outcome; the receiving application may still consume the pending event or command later.
