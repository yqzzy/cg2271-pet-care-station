# ESP32 UART public API

**Testing only:** `esp32_test.ino` is a standalone UART test sketch. It is not required for integration; do not add it to the integrated application. Integrate `esp32_uart.h` and `esp32_uart.cpp` instead, and call their functions from your application's own `setup()`, loop or tasks as described below.

For teammates connecting the MCXC444 readings and button events to the cloud, and dashboard light commands to the MCXC444. Include `esp32_uart.h` and compile `esp32_uart.cpp` in the ESP32 build.

The canonical source is now in `firmware/esp32/libraries/PetStationUart/src/`. Both sketches use the shared Arduino library; see [installation and integrated wiring](../README.md#shared-uart-library-and-wiring). Do not maintain a second copy in the test sketch directory. CLI builds can pass `--library firmware/esp32/libraries/PetStationUart` from the repository root.

## Startup

```cpp
bool uartInit(HardwareSerial &port, int rx_pin, int tx_pin);
```

Call once from `setup()`, after starting debug `Serial`. Supply a separate hardware UART, such as `Serial1`, and the pins selected for your actual wiring:

```cpp
Serial.begin(115200);
if (!uartInit(Serial1, PET_RX_PIN, PET_TX_PIN)) {
    Serial.println("UART initialization failed");
    return;
}
```

`PET_RX_PIN` and `PET_TX_PIN` must be defined by the application. RX connects to MCXC444 PTE22 TX, TX connects to MCXC444 PTE23 RX, with common GND. The module configures the board link for 9600 baud, 8N1; debug Serial can use a different baud rate.

Returns `true` on success. On `false`, stop UART-dependent startup and do not call the remaining functions or retry initialization. The module creates its own TX/RX tasks, queue and mutex, and handles parsing, checksums, sequence numbers and retransmission.

## Read food and water — cloud/telemetry task

```cpp
bool getSensors(SensorValues *out);
```

Copies the latest received readings into the supplied structure:

| Field | Meaning |
|---|---|
| `water_enough` | `bool`: `true` = enough water, `false` = low water |
| `food_milligrams` | `int32_t`: food weight in milligrams; divide by 1000.0 for grams |
| `received_ms` | `uint32_t`: ESP32-local `millis()` when the packet arrived |

```cpp
SensorValues values;
if (getSensors(&values)) {
    bool stale = uint32_t(millis() - values.received_ms) > 3000;
    float foodGrams = values.food_milligrams / 1000.0f;
    // Publish values.water_enough and foodGrams with freshness information.
    // 3000 ms is an example threshold; match it to the reporting period.
}
```

Returns `false` before the first valid packet or if `out` is null. This is a snapshot getter: repeated calls can return the same sample, and `true` alone does not mean the readings are fresh. Use the output only when the function returns `true`.

The sensor wire format is `SENSOR:1,125000*HH\n` for enough water and 125 g of food, or `SENSOR:0,125000*HH\n` for low water (`HH` is the calculated XOR checksum). Only water digits `0` and `1` are accepted. Update both boards together; the old raw-ADC water payload is no longer supported. The standalone sketch prints `water_enough=1` or `water_enough=0`.

## Consume a button event — network-independent poller

```cpp
bool consumeButton(uint32_t *sequence = nullptr);
```

Returns `true` once for a pending button event after successfully queuing `BUTTON_ACK`, then clears its pending flag. A full TX queue or busy mutex returns `false` and leaves the event pending for the next poll. Optionally copies the event sequence into the supplied pointer.

```cpp
uint32_t sequence;
if (consumeButton(&sequence)) {
    // Save the event in your application's pending notification state.
    // Publish to the cloud and retain it locally if MQTT delivery needs retrying.
}
```

Use `consumeButton()` without an argument if the sequence is unnecessary. Returns `false` when nothing is pending. Duplicate UART retransmissions do not create another application event after consumption.

The ACK acknowledges this application's consumption of the event. In the integrated sketch, `UartBridge_Poll()` owns this operation from the 20 ms main loop; never put it in a network task that can block for seconds. The bridge retains a cumulative per-boot count for cloud publication.

## Request a light setting — UART poller

```cpp
bool sendLight(bool on);
```

Call `sendLight(true)` for ON or `sendLight(false)` for OFF. Send an explicit desired state, rather than a toggle.

- `true`: request queued locally; the module waits for its ACK and performs up to two retries automatically (three total transmission attempts). Do not report completion yet.
- `false`: a request is still awaiting ACK, a previous ACK has not been consumed, the TX queue is full, or sequence IDs are exhausted. Handle the rejection in application state; do not assume it was sent.

Only one light request can be outstanding. Consume the previous ACK before submitting another request.

## Consume the light ACK — UART poller

```cpp
bool consumeLightAck(bool *on);
```

Check regularly to receive completion of the last light request:

```cpp
bool acknowledgedOn;
if (consumeLightAck(&acknowledgedOn)) {
    // Update the dashboard's acknowledged light setting to acknowledgedOn.
}
```

- Returns `true` once per matching ACK and copies the acknowledged requested setting into `*on`.
- Clears the unread ACK so the next light request can be accepted.
- Returns `false` if no ACK is pending or the pointer is null. Only use the output on `true`.
- Duplicate ACK packets do not produce another completion event.

The MCXC444 actuator task is expected to write the LED GPIO immediately after consuming the command. For this project, that consumption ACK serves as completion of the light setting; it does not measure physical illumination.

No ACK event is generated on retry exhaustion. A `false` return means no ACK is currently available; it does not distinguish waiting from timeout. `getLightResult()` reports `UART_WAITING`, `UART_CONSUMED` or `UART_TIMED_OUT` with its sequence; on a busy mutex it returns the nonterminal `UART_IDLE` sentinel with sequence zero. The integrated bridge handles timeout as an unknown outcome, never as OFF.

For the integrated sketch, cloud code uses `UartBridge_RequestLight()` and `UartBridge_TakeLightOutcome()` instead of these driver operations. The main-loop poller owns all `sendLight()` / `consumeLightAck()` calls. See the [RPC and control contract](../README.md#button-events-and-light-rpc).

## Calling rules

- Call send/get/consume functions only after successful initialization, from tasks or ordinary application code, never from an ISR.
- Give each consume operation one application owner so tasks do not compete to consume the same event.
- Keep network operations out of UART tasks. Store consumed events in application state before handling cloud delivery.
- Reset both boards together for the current demo; sequence counters restart on boot and there is no session recovery.
