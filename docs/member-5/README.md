# Member 5 Development and Implementation Plan

[Project README](../../README.md) | [ESP32 setup](../../firmware/esp32/README.md) | [Team guide](../team-guide.md)

**Status: partial implementation.** DHT11 sensing, periodic MQTT telemetry, button ACK/count handling and light RPC are implemented in source. The source now follows the [current ESP32 contract](../../firmware/esp32/README.md#button-events-and-light-rpc): a 20 ms main-loop UART poller, cumulative per-boot button count, `LIGHT_ACK` consumption confirmation, and one outstanding light RPC. This supersedes the proposed event queue and `COMMAND_RESULT` interfaces below. Hardware integration and MCU calibration still require verification. Checked actions identify implemented code, not successful hardware tests.

Software verification on 9 Nov: all eight host suites and both configured/clean-clone S2 builds passed; dashboard and alarm exports are saved. See [recorded evidence and remaining bench checks](../../tests/README.md#recorded-software-validation---2026-11-09) and the [report draft](implementation-report.md).

**User-reported hardware:** ESP32-S2 and a DHT11 sensor from a 45-in-1 component set. GPIO4 is selected as a configurable default with the user's authorization. The exact development-board variant, sensor breakout pin order, supply requirements, connection to GPIO4, and upload settings still require physical verification.

## Goal and responsibilities

Build the ESP32 subsystem so it reads the DHT11 independently, publishes station telemetry to ThingsBoard, displays alerts, and forwards remote light commands through Member 4's UART bridge. Start with standalone environmental sensing and labelled cloud sample data, then integrate the MCXC444.

| Area | Member 5 responsibility | Collaboration or boundary |
|---|---|---|
| Environment | Implement `environment.h/.cpp`, including sampling, validity, freshness, and threshold evaluation | Verify the actual DHT11 module and GPIO wiring |
| Cloud | Implement `cloud_client.h/.cpp`, Wi-Fi, MQTT over TLS, reconnection, telemetry, and RPC handling | Agree on bridge-to-cloud types with Member 4 |
| Configuration | Maintain the placeholder `secrets.example.h` and local credential setup instructions | Keep real values only in ignored `secrets.h` |
| Dashboard | Create ThingsBoard devices, widgets, alarms, and reusable configuration exports | Display other members' measurements without duplicating their sensing or feeding policy |
| Startup | Coordinate task and local synchronization setup in the sketch | Joint review with Member 4; Member 4 owns `uart_bridge.*` and both UART endpoints |
| Integration | Coordinate cloud integration and collect test/demo evidence | Each member fixes and verifies their own module |

Starting code: [ESP32 sketch and modules](../../firmware/esp32/pet_care_station/). Supporting agreements: [system design](../system-design.md), [hardware worksheet](../hardware.md), [UART protocol](../../protocol/README.md), [dashboard requirements](../../dashboard/README.md), and [manual test checklist](../../tests/README.md).

Automatic water replenishment, remote dispensing, feeding-policy changes, and email/push notifications are outside this plan. Dashboard alarms are the initial notification channel.

## Architecture and interfaces

```text
DHT11 -> Environment_Task -> latest environment snapshot --+
                                                         +-> Cloud_Task -> ThingsBoard
MCXC444 <-> UART RX/TX tasks + 20 ms bridge poll ----------+

ThingsBoard light request -> Cloud_Task -> UART bridge -> MCXC444
ThingsBoard RPC response <- Cloud_Task <- matching consumption ACK
```

- `Environment_Task` alone accesses the DHT11; `Cloud_Task` alone accesses Wi-Fi, TLS, and MQTT clients. The shared UART library has RX/TX tasks; the main-loop bridge poller owns all command/event send and consume operations.
- The ESP32-S2 is single-core. Use ordinary FreeRTOS task creation without assigning work to a second core. Network retries must not prevent environmental sampling or UART handling.
- Store continuous measurements in latest-state snapshots. Use a bounded event queue for button events, dispensing transitions, and command results.
- Hold mutexes only while updating or copying shared data. Release them before sensor reads, serial/network I/O, or waits.
- Start sensing before attempting network connections. Standalone mode must compile and run without Member 4's UART implementation.

### Proposed contracts to review with Member 4

The environment interfaces below are implemented in [environment.h](../../firmware/esp32/pet_care_station/environment.h), and cloud startup in [cloud_client.h](../../firmware/esp32/pet_care_station/cloud_client.h). Both initialization functions return `bool` so startup can check failures. The cloud/bridge APIs in the original proposal below are superseded by [uart_bridge.h](../../firmware/esp32/pet_care_station/uart_bridge.h); Member 4 should review this shared contract.

| Interface | Planned behavior |
|---|---|
| `bool Environment_Init(void)` | Initialize the configured DHT11 pin/driver and snapshot mutex; report initialization failure |
| `bool Environment_Read(EnvironmentReading *out)` | Perform a bounded read and report validity and timing |
| `void Environment_Task(void *argument)` | Read periodically and update the local snapshot |
| `bool Environment_CopySnapshot(EnvironmentReading *out)` | Copy a consistent snapshot without sensor I/O; report whether a sample attempt exists. Check `Environment_IsFresh` separately for current values/flags |
| `bool Cloud_Init(void)` / `void Cloud_Task(void *argument)` | Check local configuration/resources and service test connectivity in a separate task |
| `bool Cloud_QueueEvent(const CloudEvent *event)` | Copy an event without blocking; return `false` when full |
| `void Cloud_HandleLightCommand(bool on, uint32_t request_id)` | Route a validated request through the bridge and track its outcome |
| `bool UartBridge_RequestLight(bool on, uint32_t request_id)` | Member 4 interface: report acceptance for forwarding, not application of the light state |
| `bool UartBridge_Init(void)` / `bool UartBridge_CopySensors(McuTelemetry *out)` | Implemented adapter: initialize the shared driver, then copy food/water values and packet arrival time; failed initialization leaves the source unavailable |

`EnvironmentReading` needs temperature in degrees Celsius, relative humidity in percent, validity, last-attempt time, last-valid time, and environmental alert flags. Preserve the last valid sample separately after failures; invalid data must never become a fabricated zero reading.

`BoardSnapshot` needs food weight in grams, water reading with its actual unit, per-source validity and age, low-food/low-water flags, dispensing status, and confirmed light state. Do not label uncalibrated water readings as percentages or a completed motor cycle as confirmed food delivery.

`CloudEvent` needs a tagged payload for button events, dispensing transitions, and command results, including source IDs, timing, and success/error details. Member 4 owns UART framing and encoding; do not serialize raw application structs or directly compare the boards' independent uptime counters.

## Six implementation milestones

Work through these in order. Milestones 1-4 can progress without functioning MCXC444 firmware. Real command confirmation in Milestone 5 depends on Member 4's bridge and the local actuator path.

### Milestone 1 - Reproducible ESP32 build

- [ ] Create a focused Member 5 feature branch while preserving unrelated local files.
- [ ] Identify the exact ESP32-S2 board variant, flash configuration, and USB upload method. Select its Arduino board profile; use a generic S2 profile only when appropriate.
- [ ] Use Arduino IDE 2.x for bring-up and Arduino CLI for repeatable compilation.
- [ ] Install Espressif's Arduino core, Adafruit DHT sensor library and Unified Sensor dependency, PubSubClient, and ArduinoJson. Record the exact installed versions after verification.
- [ ] Convert the sketch into minimal startup code that initializes local snapshots, mutexes, queues, and the environment/cloud tasks, checking resource-creation failures.
- [ ] Add standalone mode that omits UART initialization and unresolved bridge calls until Member 4's implementation is ready. Never represent unavailable board data as valid.
- [ ] Document board selection, dependencies, compile/upload commands, and serial-monitor settings in the ESP32 setup guide.

**Deliverable and acceptance target:** a teammate can compile and upload the sketch using the documented setup. This milestone does not establish successful sensor or cloud operation.

### Milestone 2 - Independent DHT11 sensing

Implementation is available in the standalone sketch; see [setup, wiring, and verification instructions](../../firmware/esp32/README.md#standalone-dht11-setup). GPIO4 is selected in `environment_config.h`; physical wiring and Member 4's pin-map review remain outstanding.

- [ ] Verify the kit module's printed signal, supply, and ground labels; do not assume another breakout's pin order. Check its supply requirements and pull-up arrangement before powering it, and keep the data line at ESP32-compatible logic levels.
- [ ] Confirm the selected GPIO4 allocation with Member 4 before integration and verify the physical connection. GPIO4 is recorded in configuration and the hardware worksheet, but team pin-map review and wiring checks are still pending.
- [x] Implement `environment.*` with sensor type `DHT11` and cloud connectivity disabled for initial testing.
- [x] Sample every five seconds using periodic FreeRTOS delays. Avoid immediate repeated retries after a failed read.
- [x] Reject failed or non-finite readings; update attempt status without advancing the last-valid timestamp on failure.
- [x] Log measurements, validity, read duration, and failure counts without holding a shared-state lock during sensor I/O.
- [x] Implement configurable high-temperature/high-humidity evaluation with hysteresis. Invalid samples must not falsely clear alarms or count as consecutive valid samples.
- [ ] Verify normal readings and sensor disconnect/reconnect recovery without Wi-Fi or the MCXC444.

**Deliverable and acceptance target:** standalone serial evidence of real DHT11 readings, honest failure status, freshness, and recovery.

### Milestone 3 - ThingsBoard connectivity and telemetry

Combined live telemetry is implemented: see [ThingsBoard connectivity test](../../firmware/esp32/README.md#thingsboard-connectivity-test). The main sketch starts DHT11, the shared UART driver and cloud tasks. Current snapshots publish every five seconds with independent source freshness; the former synthetic one-shot publisher is removed. MCU calibration, source-age checks, physical integration and dashboard validation remain outstanding.

- [ ] Create a ThingsBoard Cloud account and separate devices named `pet-care-test` and `pet-care-station`. Keep generated readings and simulated results on the clearly labelled test device.
- [ ] Turn `secrets.example.h` into usable placeholder configuration and copy it locally to ignored `secrets.h`. Configure Wi-Fi, endpoint, device token, and trusted CA locally; do not commit their real values.
- [ ] Implement MQTT over TLS on port 8883 with the device token as MQTT username and server-certificate validation enabled. Do not bypass validation to work around connection failures.
- [ ] Obtain system time asynchronously for certificate validation while environmental sampling continues.
- [ ] Publish labelled sample data first, then replace it with environment snapshots on the live device.
- [ ] Publish telemetry to `v1/devices/me/telemetry`; check serialization size, publish return values, and diagnostic counters.
- [ ] Use stable names including `temperature_c`, `humidity_pct`, `environment_valid`, `environment_age_ms`, `food_weight_g`, `water_low`, `uart_connected`, and `light_on`. Publish only meaningful fields for sources that exist; omit invalid numeric updates rather than substituting zero.

**Deliverable and acceptance target:** real DHT11 readings appear in ThingsBoard, with invalid status and last-valid freshness represented correctly after failures.

### Milestone 4 - Dashboard and alerts

- [ ] Create temperature/humidity value cards and history graphs, food-weight and water-status widgets, and separate device/UART connectivity indicators.
- [ ] Show validity and freshness for each measurement source. Missing or stale board data must not prevent display of current environmental data.
- [ ] Add button-event and dispensing-history displays with honest outcome wording.
- [ ] Add a light control with separate requested/pending status and confirmed device state.
- [ ] Configure environmental, low-food, and low-water alarms. Environmental thresholds are evaluated on ESP32; dashboard alarm rules consume the corresponding flags and validity instead of implementing conflicting thresholds.
- [ ] Make environmental condition status unknown when readings are invalid or stale; do not treat missing data as recovery.
- [ ] Calculate staleness using elapsed time and source validity. A frozen age field or continued ESP32 heartbeat must not make an old measurement look fresh.
- [ ] Demonstrate normal, alarm, invalid, stale, and disconnected states using the test device.

**Deliverable and acceptance target:** a dashboard that visibly distinguishes live data, unavailable data, pending requests, and confirmed outcomes.

### Milestone 5 - Confirmed remote light commands

- [x] Subscribe to `v1/devices/me/rpc/request/+` after every MQTT connection.
- [x] Accept `setValue` / `setLight` with Boolean `params` and `getValue` / `getLight`; reject malformed requests and unsupported methods without forwarding them.
- [x] Preserve the ThingsBoard request ID and map it to a unique bridge request ID. Discard old RPC replies across disconnects.
- [x] Allow one outstanding light command; respond with a busy error to different requests and suppress in-flight duplicate IDs. Forward without waiting inside the MQTT callback.
- [x] Keep the dashboard pending until matching `LIGHT_ACK` consumption confirmation. Bridge acceptance or UART transmission alone must not update confirmed state.
- [x] Respond on `v1/devices/me/rpc/response/<requestId>` using the original ThingsBoard ID and the actual success/error outcome.
- [x] Bound queue acceptance to three seconds and UART retries to three attempts, 200 ms apart. On failure mark the current state unknown; reject unmatched ACKs.
- [x] Cancel unclaimed requests on disconnection and never replay old commands after reconnection; an already claimed dispatch can finish its bounded attempts. Configure nonpersistent dashboard RPC.
- [ ] Exercise success, failure, timeout, and delayed results with a labelled simulator, then repeat with Member 4's real bridge and the MCXC444 output path.

**Deliverable and acceptance target:** dashboard success means the MCXC444 consumed the light command; its actuator writes the output immediately afterwards. Verify the physical LED separately.

### Milestone 6 - Resilience, integration, and handoff

- [ ] Implement timed reconnect backoff of 1, 2, 4, 8, 16, then 30 seconds, resetting after a successful connection. Avoid continuous connection loops.
- [ ] Configure finite TCP, TLS, and MQTT timeouts and measure their effect on task responsiveness. Network failures must not block environmental sampling or UART service.
- [ ] Resubscribe after reconnection and publish current snapshots before draining eligible historical events.
- [ ] Limit the event queue to 16 entries, reject new entries when full, and increment a dropped-event counter without blocking producers.
- [ ] Expire buffered events after 30 seconds. Include source age so delayed events are not presented as freshly generated; never buffer actuator actions for replay.
- [ ] Document PubSubClient's outbound QoS 0 limitation: event history is best effort, not guaranteed lossless delivery.
- [ ] Check task stack margins, memory behavior, sampling intervals, and UART error/overflow counters during network failures and concurrent traffic.
- [ ] Export dashboard configuration and separately required alarm/profile configuration into the dashboard folder, excluding credentials.
- [ ] Update setup instructions, shared agreements, test evidence, remaining limitations, and a reproducible demonstration sequence. Request Member 4's review of shared interfaces.

**Deliverable and acceptance target:** reconnection restores current telemetry without replaying actions, while local sensing and board handling continue independently.

## Proposed settings and unresolved hardware facts

These are starting values for implementation and measurement, not verified timings, finished configuration, or animal-care recommendations. Keep configurable settings together and record any agreed changes with test evidence.

| Setting | Proposed starting value |
|---|---|
| DHT11 sampling / periodic telemetry | 5 seconds each |
| Environmental freshness limit | 15 seconds since the last valid reading |
| Light applied-state confirmation timeout | 3 seconds |
| Dashboard RPC timeout | 10 seconds |
| Reconnect backoff | 1, 2, 4, 8, 16, then 30 seconds |
| Event queue / maximum event age | 16 entries / 30 seconds |
| High-temperature alarm | Activate at or above 30 degrees Celsius; clear below 28 degrees Celsius |
| High-humidity alarm | Activate at or above 80% RH; clear below 75% RH |
| Alarm transition confirmation | Two consecutive valid samples satisfying the activation or clearing condition |
| Notifications | Dashboard alarms only |

The DHT11 default is now GPIO4, environment-task priority 1, and a 4096-byte stack; the installed dependency versions and configuration are recorded in the ESP32 setup notes. The exact development-board profile and USB options, physical GPIO4 wiring, breakout supply/pull-up details, task timing/stack margins, UART timing and framing, and board-source freshness limits still need verification with their owners. Member 4 defines the UART heartbeat/link timeout. Member 2 retains ownership of low-food/low-water interpretation and feeding policy.

## Verification and evidence

These are future acceptance scenarios. No result is claimed until a run is recorded. Use labelled test-device data for simulations and identify which checks require real hardware.

| Scenario | Required result |
|---|---|
| Sensor unplugged at boot or during operation | Invalid status without fabricated zero; recovery after reconnection |
| Threshold activation, clearing, and interrupted sequences | Correct hysteresis and consecutive-sample behavior; invalid data does not clear alarms |
| Wi-Fi unavailable at startup | Environmental sampling starts and continues |
| Broker failure, invalid token, or untrusted certificate | Failure diagnostics and bounded retries; certificate validation stays enabled |
| MCXC444/UART disconnected | Environment remains available; local-board measurements become stale |
| Light success, failure, timeout, duplicate, or late result | Correct request correlation; requested state never substitutes for confirmation |
| Malformed RPC or full queue | Safe rejection with diagnostics; responsive tasks |
| DHT11 reads during UART traffic | Measure errors/overflows and latency; investigate unexplained frame loss |
| Network restored | Current snapshots return and subscriptions recover; old actuator requests are not replayed |
| Fifteen-minute concurrent run | No resets, progressive memory loss, or missed sampling cycles |
| Documentation and exports | Reproducible setup, valid links, no real credentials, and clearly identified unverified work |

The Adafruit DHT driver briefly disables interrupts during reads. On the single-core ESP32-S2, measure UART behavior while sampling rather than assuming separate tasks eliminate interference.

For every run, use the [test evidence template](../../tests/README.md#evidence-record-template): scenario/tester/date, firmware commit, hardware and dependency versions, configured timings and thresholds, expected result, actual result with measured latency, pass/fail, and evidence link. Keep dashboard screenshots and serial logs free of secrets.

Your first implementation deliverable is a repeatable build plus standalone DHT11 readings. The final handoff adds dashboard/configuration exports, confirmed light-command behavior, reconnection evidence, and a list of unresolved issues. Check off each milestone action only after its corresponding work is verified.

## Technical references

- [ESP32-S2 hardware reference](https://documentation.espressif.com/esp32-s2_datasheet_en.html): chip capabilities; verify the development board separately.
- [Adafruit DHT wiring guidance](https://learn.adafruit.com/dht/connecting-to-a-dhtxx-sensor) and [driver source](https://github.com/adafruit/DHT-sensor-library/blob/master/DHT.cpp): wiring considerations, reading intervals, and interrupt behavior. The kit breakout may have a different pin order.
- [ThingsBoard MQTT connection and TLS](https://thingsboard.io/docs/reference/mqtt-api/getting-connected/), [telemetry](https://thingsboard.io/docs/reference/mqtt-api/telemetry/), and [RPC](https://thingsboard.io/docs/reference/mqtt-api/rpc/): device authentication and topic conventions.
- [PubSubClient documentation](https://pubsubclient.knolleary.net/api) and [limitations](https://github.com/knolleary/pubsubclient): buffer sizing, socket timeouts, and outbound QoS support.
