# UART protocol agreement

[Back to project README](../README.md)

## Ownership

Member 4 owns both endpoints and [messages.h](messages.h); Members 3 and 5 review changes. This is a proposed contract, not a wire implementation.

| Direction | Message | Intended contents |
|---|---|---|
| MCXC444 to ESP32 | SENSOR_SNAPSHOT | Food weight, water reading/status, validity and sample age |
| MCXC444 to ESP32 | BUTTON_EVENT | Debounced event ID and local event time |
| ESP32 to MCXC444 | SET_LIGHT | Request ID and desired on/off state |
| MCXC444 to ESP32 | COMMAND_RESULT | Request ID, applied light state and success/error |
| MCXC444 to ESP32 | DISPENSE_STATUS | Idle/busy/completed/failed; distinguish mechanical cycle from confirmed food delivery |

## Framing TODOs

- Agree on start marker, version, type, sequence/request ID, payload length, payload and checksum.
- Choose encoding, maximum payload, byte order if binary, checksum algorithm and timeout together before implementing either endpoint.
- Define resynchronisation after partial/corrupt data, bounds checks, queue-overflow diagnostics and stale-link detection.
- Separate receipt acknowledgement from applied-command confirmation. Correlate command results to request IDs; duplicate light commands must be harmless.
- Do not serialize raw C structs. Board layouts, padding and data sizes may differ.
- Use MCU-local time/sample age unless clocks are deliberately synchronised; do not directly compare independent uptime counters.

The wire format remains a TODO because this repository contains no protocol implementation yet. System-level responsibilities are in [system design](../docs/system-design.md).
