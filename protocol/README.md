# UART protocol agreement

[Back to project README](../README.md)

Member 4 owns both endpoints; Members 3 and 5 review integration. The current wire implementation is in the [MCXC444 UART module](../firmware/mcxc444/communication/README.md) and [shared ESP32 PetStationUart library](../firmware/esp32/libraries/PetStationUart/src/esp32_uart.cpp). [messages.h](messages.h) is an unused, comment-only proposal for a richer future vocabulary; it does not define the current packets.

## Current wire format

The board link uses 9600 baud, 8N1, crossed TX/RX and common ground. Packets are bounded ASCII lines: `<body>*HH\n`. `HH` is the two-digit hexadecimal XOR of the ASCII body bytes, excluding `*HH` and newline. Both parsers validate checksum and fields before changing state; partial lines accumulate in bounded buffers and oversized lines are discarded until the next newline. No raw C structs are serialized.

| Direction | Body before checksum | Meaning |
|---|---|---|
| MCXC444 to ESP32 | `SENSOR:<water>,<food_mg>` | Water `0` = low, `1` = enough; food is signed 32-bit milligrams |
| MCXC444 to ESP32 | `BUTTON:<seq>` | A debounced button event |
| ESP32 to MCXC444 | `BUTTON_ACK:<seq>` | ESP32 application consumed that button event |
| ESP32 to MCXC444 | `LIGHT:<seq>,<on>` | Explicit requested setting: `0` = OFF, `1` = ON |
| MCXC444 to ESP32 | `LIGHT_ACK:<seq>` | MCU application consumed that light command |

Sequences are positive decimal integers up to 999999999. Examples of complete frames are `BUTTON:1*1D\n`, `BUTTON_ACK:1*0B\n`, `LIGHT:1,1*48\n` and `LIGHT_ACK:1*43\n` (the displayed `\n` represents a newline byte). Sensor packets carry no sequence or source acquisition time. The ESP32 records local arrival uptime and treats a packet as stale after three seconds; the MCU normally reports once a second when both sources are valid.

## Acknowledgement and application ownership

Each sender keeps one acknowledged operation outstanding and retries after 200 ms, with three total attempts. Duplicate already-consumed sequences resend the corresponding ACK without generating a second application event. Matching sequence IDs correlate light results; malformed, corrupt and unmatched ACK packets do not confirm commands.

The integrated ESP32 [bridge](../firmware/esp32/pet_care_station/uart_bridge.cpp) polls every 20 ms from `loop()`, independently of network waits. It owns `consumeButton()`, `sendLight()` and `consumeLightAck()`. A full ACK queue leaves a button pending for a later poll; only successful consumption increments the retained per-boot cloud count. Cloud code requests light work through the bridge rather than calling the driver directly.

The MCU actuator consumes a light command and queues its ACK before writing the LED GPIO. Therefore `LIGHT_ACK` confirms application consumption, not independent GPIO readback or measured illumination. The ESP32 updates `light_on` only after consuming that ACK, and marks light state unknown after a failed command. See [MQTT RPC mapping and bounds](../firmware/esp32/README.md#button-events-and-light-rpc).

## Limitations and future changes

- Reset both boards together: sequence counters restart on boot and there is no session/reboot handshake.
- Source validity and acquisition age are not on the sensor wire format. MCU-side source validation is required; packet arrival freshness alone cannot establish sensor acquisition freshness.
- No dispensing result or independently verified applied-light result is transmitted. Do not report a motor cycle as confirmed food delivery.
- The future `SENSOR_SNAPSHOT`, `BUTTON_EVENT`, `SET_LIGHT`, `COMMAND_RESULT` and `DISPENSE_STATUS` vocabulary in `messages.h` would require coordinated changes at both endpoints; those names are not accepted current wire messages.

Use [system design](../docs/system-design.md) for task ownership and [acceptance tests](../tests/README.md#bidirectional-button-and-light-checks) for hardware evidence. Host substitutes and compilation do not measure UART timing or electrical reliability.
