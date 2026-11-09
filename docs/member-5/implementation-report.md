# ESP32 implementation report draft

[Setup and interfaces](../../firmware/esp32/README.md) | [Acceptance checklist](../../tests/README.md#bidirectional-button-and-light-checks)

These sections describe implemented software. Add measured hardware results and evidence before using them as the final demonstration report. Do not describe host substitutes as board tests.

## ESP32 use

The ESP32-S2 subsystem reads a DHT11 on configurable GPIO4 and bridges UART sensor/control traffic to ThingsBoard. The environment task samples every five seconds after a two-second startup settling period. It records current validity, last-valid measurements, acquisition time and failures. High-temperature and high-humidity flags use configurable thresholds, hysteresis and consecutive valid samples. Invalid readings do not become zero measurements or falsely clear an existing condition.

The single-core S2 runs separate environment, cloud and library UART RX/TX tasks. A 20 ms main-loop poller consumes button/light acknowledgements and dispatches queued light requests, independently of network connection waits. Short critical sections protect bridge state; UART operations, serialization and network I/O occur outside them. The default board link is RX GPIO2 / TX GPIO1 at 9600 baud with common ground; actual wiring and board upload options require bench confirmation.

## Online function: sent and received data

The cloud task uses Wi-Fi and MQTT over TLS on port 8883, with device-token authentication, trusted root certificate validation and synchronized time. Reconnect delays increase from one to thirty seconds and reset after a connection. Credentials stay in ignored local configuration.

Every five seconds, and after reconnection, it publishes independent environment and MCU sensor snapshots to `v1/devices/me/telemetry`. Payloads contain Celsius temperature, relative humidity, food grams and Boolean water status only while their sources are valid/fresh. Status flags distinguish unavailable data from historical values retained by ThingsBoard. Separate control telemetry carries a cumulative per-ESP-boot button count, approximate last-press time and acknowledged light state. Counts survive MQTT outages in RAM; they are neither persistent across reset nor a guaranteed event history. Outbound telemetry uses PubSubClient QoS 0, so local publish success is not proof of cloud receipt.

The device subscribes to light RPC requests after every connection. `getValue` / `getLight` return the last acknowledged setting or an unknown-state error. `setValue` / `setLight` accept Boolean parameters and forward an explicit desired state to the UART bridge. One set can be outstanding. The MQTT callback queues work and returns; a Boolean success reply is sent only after matching command consumption acknowledgement. Errors cover malformed requests, busy/unavailable bridge and bounded command timeout. Duplicate in-flight and most-recent completed request IDs are handled within each MQTT session. Queued, unclaimed actions are cancelled on disconnection rather than replayed on reconnect.

Dashboard request controls and confirmed-state indicators have different meanings. `light_on` requires `light_known` and recent device telemetry. The ACK indicates MCU application consumption; it does not independently measure electrical output or illumination. Threshold alarms require the relevant source validity flag; a missing source is a separate condition and does not clear an active threshold alarm. Dashboard exports and setup are in [dashboard/](../../dashboard/).

## Communication protocol: ESP32 half

The shared PetStationUart library parses newline-delimited packets with XOR checksums and decimal sequence IDs. `SENSOR` packets update the latest water/food snapshot, including ESP32 arrival uptime. Water is Boolean; signed food milligrams are converted to grams for telemetry. The protocol does not carry independent source acquisition timestamps, so arrival freshness cannot establish MCU sensor acquisition freshness.

`BUTTON` becomes a single pending event. The main-loop poller consumes it only after `BUTTON_ACK` enters the TX queue, then increments the count once. Queue pressure leaves the event pending; a duplicate already-consumed sequence resends its ACK without another count. This fixes the unconsumed-first-button problem while avoiding dependency on cloud availability.

Light requests are sent as `LIGHT:<sequence>,<0|1>` with checksum and newline. The driver attempts transmission up to three times, 200 ms apart, and accepts only the matching `LIGHT_ACK`. The bridge retries transient queue/lock rejection within a three-second queue deadline. Exhausted ACK retries mark light state unknown. Because sequences restart on boot and there is no session handshake, reset both boards together for the current demo.

## Evidence still to add

- Board profile, actual wiring, calibrated food readings and verified water thresholds.
- Five real button presses, five local ACKs/cues and matching cloud count, including a Wi-Fi outage.
- ON/OFF RPC correlation, measured latency and physical LED observation; timeout/reconnect cases.
- Real threshold alarm activation/recovery, invalid-source and offline dashboard behaviour.
- Demo-room hotspot connection, stack margins, DHT timing and UART frame-loss checks under concurrent load.
- Exact final build revision and recorded software verification from [tests/README.md](../../tests/README.md).
