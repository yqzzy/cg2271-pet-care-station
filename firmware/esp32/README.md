# ESP32 firmware

[Back to project README](../../README.md)

The main sketch publishes combined live DHT11 and MCXC444 UART sensor telemetry every five seconds, with independent source-validity/freshness flags. It also publishes a current snapshot on MQTT reconnection. Missing local cloud configuration disables networking while sensing/UART continue. The previous one-shot synthetic test payload is no longer used.

For Member 5's ESP32-S2 and DHT11 work, follow the [implementation plan and milestone checklist](../../docs/member-5/README.md). Its hardware details are user-reported and its settings are proposed pending verification.

The target chip is ESP32-S2 and the user-reported kit module is DHT11. This DHT11 uses a single digital data line, not I2C. Members 4 and 5 still need to verify the exact board, pin allocation, and eventual MQTT/UART configuration.

Member 4 owns `uart_bridge.*`; Member 5 owns `environment.*`, `cloud_client.*` and local credential setup. Each implements/tests their task entry points. Both review sketch startup/task creation. Copy `secrets.example.h` to ignored `secrets.h` when adding configuration; never commit credentials.

Keep network reconnection separate from environmental sampling and UART handling. See [dashboard requirements](../../dashboard/README.md).

## Standalone DHT11 setup

The user authorized choosing an available GPIO. The implementation defaults to **GPIO4**; this is a proposed wiring assignment, not a verified connection or Member 4's reviewed pin map. Check the kit module's printed labels and the board's GPIO numbering rather than a physical header position.

| DHT11 module connection | ESP32-S2 connection |
|---|---|
| Signal / DATA / S | GPIO4 |
| GND / - | GND |
| VCC / + | 3V3, after confirming the kit module supports 3.3 V operation |

Check the module's pull-up arrangement; a bare sensor may need a data pull-up to 3.3 V, while a breakout may already include one. If the module requires a different supply, verify the data-line voltage and appropriate interfacing before connecting it to the ESP32. Module pin order varies.

Edit [environment_config.h](pet_care_station/environment_config.h) to change the GPIO, sampling/freshness periods, alert settings, or task configuration. A compiler definition such as `-DENVIRONMENT_DHT_PIN=4` can override the data pin. Runtime initialization rejects GPIOs that cannot drive the data line; it cannot detect board-specific wiring conflicts.

### Dependencies and build

Installed versions found in the development environment:

| Tool or library | Version |
|---|---|
| Arduino CLI | 1.5.1 |
| Espressif Arduino ESP32 core | 3.3.11 |
| Adafruit DHT sensor library | 1.4.7 |
| Adafruit Unified Sensor | 1.1.15 |

In Arduino IDE, install the **esp32 by Espressif Systems** board package, **DHT sensor library by Adafruit**, and **Adafruit Unified Sensor**. Open `pet_care_station/pet_care_station.ino`. Select the board matching your exact S2 variant and its upload/flash options. If using native USB for the serial monitor, configure USB CDC On Boot for your board; USB-to-UART boards may use a different console setup. Do not assign a task to core 1 on the single-core S2.

From the repository root, with Arduino CLI on PATH and the dependencies installed:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32s2 --library firmware/esp32/libraries/PetStationUart --build-path build/esp32-environment --warnings all firmware/esp32/pet_care_station
```

This generic S2 build target checks compilation; it does not identify the physical board or validate its USB/flash configuration. Use your verified board profile to upload from the IDE, then open Serial Monitor at **115200 baud**. Install **PetStationUart** (instructions below) and **PubSubClient 2.8** as well as the DHT libraries to build the current sketch. Missing cloud credentials disable networking at runtime; ArduinoJson and the MCXC444 are not required.

### Runtime and public interface

`setup()` checks `Environment_Init()` and task allocation, then starts exactly one `Environment_Task`. Initialization now returns `bool` so startup can handle invalid GPIO/mutex allocation failure; the original comment-only proposal used `void`. Future startup integration should retain this check.

The task waits two seconds for sensor settling, reads a humidity/Celsius-temperature pair every five seconds, and never retries immediately after a failed read. Sensor transactions and serial logging occur outside the snapshot mutex. Overruns do not trigger a burst of catch-up reads. Cloud startup is optional and uses a separate task; The shared UART driver starts before the cloud task.

## ThingsBoard connectivity test

The current sketch sends live sensor snapshots, not the former fixed test message. Keep your existing local `secrets.h` (Wi-Fi SSID/password, MQTT hostname, device token, trusted root CA). For the EU endpoint shown by ThingsBoard's connectivity dialog, use `mqtt.eu.thingsboard.cloud`. Upload the main `pet_care_station.ino`; the DHT11 task is enabled.

### Shared UART library and wiring

The single canonical driver now lives in [PetStationUart](libraries/PetStationUart/), shared by this application and `comms/esp32_test/esp32_test.ino`. Its behavior/wire encoding is unchanged. Do not add the standalone test `.ino` to the main sketch.

The library was packaged into ignored `build/PetStationUart.zip` and installed in the local Arduino library directory during implementation. On another computer, from the repository root:

```powershell
Compress-Archive -LiteralPath firmware/esp32/libraries/PetStationUart -DestinationPath "$env:TEMP/PetStationUart.zip" -Force
```

In Arduino IDE, use **Sketch > Include Library > Add .ZIP Library** and select that ZIP, then reopen the sketch. Repeat installation after changes to the canonical driver. CLI builds can use the repository copy directly via the documented `--library` argument.

Default bridge allocation in [uart_bridge.h](pet_care_station/uart_bridge.h):

| ESP32-S2 | MCXC444 |
|---|---|
| RX GPIO2 | TX PTE22 |
| TX GPIO1 | RX PTE23 |
| GND | GND |

The board link is **9600 baud, 8N1**; debug Serial remains **115200 baud**, and DHT11 DATA stays on GPIO4. Match the actual wiring before powering the boards. Override `PET_RX_PIN`/`PET_TX_PIN` through build flags or edit the bridge header. Initialization rejects invalid pins and conflicts with the DHT pin. Failed UART initialization does not stop environmental/cloud tasks; the adapter never calls the driver getter unless initialization succeeded.

### Payload and timing

After MQTT connects, then every five seconds, Cloud_Task copies the two snapshots, releases their locks, builds JSON and publishes non-retained data to `v1/devices/me/telemetry`. Example values below illustrate the format only:

```json
{"data_source":"live","environment_valid":true,"mcu_data_fresh":true,"environment_age_ms":500,"mcu_age_ms":250,"temperature_c":21.6,"humidity_pct":69.3,"temperature_high":false,"humidity_high":false,"food_weight_g":125.000,"water_enough":true,"water_low":false}
```

Environment data must pass `Environment_IsFresh` (latest attempt valid, last valid sample less than 15 seconds old). UART packets must exist and be less than three seconds old, matching the current one-second MCU reporting period. The cloud builder and scheduler are hardware-independent in [live_telemetry.h](pet_care_station/live_telemetry.h).

A failed/stale environment source sets `environment_valid=false` and omits temperature, humidity and environment alarm fields. A missing/stale UART packet sets `mcu_data_fresh=false` and omits food weight and both water fields. Known ages are still reported; ages are omitted before the first sample. A valid water-low reading is **false water_enough / true water_low**, never an unknown reading.

Food is converted from signed milligrams to grams using division by 1000.0. Negative calibrated readings are preserved for diagnosis; they are not clamped. Water is an enough/low Boolean, not a percentage or raw ADC value.

In ThingsBoard, bind widgets to these new keys and gate their current displays/alarms with the corresponding status flags. ThingsBoard retains previous numeric values when an update omits a key, so their presence alone does not mean they are still current. Old `test_*` keys may remain in history but are no longer updated. Verify timestamps change approximately every five seconds; each source continues independently when the other fails.

Use the server's latest telemetry timestamp/device connectivity as an additional gate (for example, mark unavailable after 15 seconds without an update). During an internet outage the last source flags and age values freeze too. Local source expiry appears in the next periodic publication, not exactly when the threshold is crossed.

### MCXC444 prerequisites and limitations

The MCU application must calibrate food sensing before enabling `FOOD_CALIBRATED`; it is currently 0 in source. `uart_link.c` sends sensor packets only when both food and water states are valid, and test fallback is disabled. No packet before calibration is therefore expected. Verify water thresholds and run the integrated MCU application, rather than its standalone test that sends fixed 125 g sample data.

`mcu_data_fresh` measures **UART packet arrival freshness**, not independent food/water acquisition age. The current wire format carries no source age, validity fields or synthetic-data marker. The MCU must reject stale source snapshots before transmission; that check is still required in its adapter. A simulated UART packet cannot be identified as synthetic by this ESP32 publisher. Do not present standalone MCU test fixtures as real measurements.

The cloud task logs `[cloud] Live telemetry sent (QoS 0): {...}`. This confirms a local publish only; verify receipt in ThingsBoard Latest telemetry. Reconnect sends current snapshots immediately without replaying an offline history. Serialization failure skips the packet, never publishes truncated JSON, and is retried on a later period. MQTT buffer allocation is 1024 bytes for the 512-byte payload buffer plus topic/protocol overhead. All network waits remain in Cloud_Task; existing certificate validation, time synchronization and backoff remain in effect.

References: [ThingsBoard telemetry API](https://thingsboard.io/docs/reference/mqtt-api/telemetry/) and [PubSubClient API](https://pubsubclient.knolleary.net/api).

Serial output reports `sample`, `valid`, `temp_c`, `humidity_pct`, last-valid values, `has_last_valid`, `age_ms`, `read_ms`, cumulative failures, `alerts_known`, and high-temperature/high-humidity flags. Before any successful sample, values are `nan`, `has_last_valid=0`, and `age_ms=4294967295` means unknown. A real numeric zero is not treated as a failed read.

Alert defaults are two consecutive valid samples at or above 30 degrees Celsius / 80% RH to activate, and two below 28 degrees Celsius / 75% RH to clear. Neutral or invalid samples break pending confirmation sequences. Invalid readings preserve latched flags and last-valid values, but their current values are `nan` and alerts must be displayed as unknown. A sampling gap of at least 15 seconds also breaks pending confirmations. These are prototype settings, not animal-care recommendations.

Other tasks should call `Environment_CopySnapshot(&reading)` from [environment.h](pet_care_station/environment.h), then evaluate `Environment_IsFresh(reading, millis())` before displaying or acting on the current values or flags. Snapshot copying does no sensor I/O; its Boolean return indicates that an attempt exists, not that the reading is valid. A reading becomes stale at 15 seconds since its last valid acquisition, or immediately invalid after a failed attempt. Last-valid fields are explicitly historical, not substitutes for current data. Treat a copy failure as unavailable data. Only `Environment_Task` calls `Environment_Read`.

## Verification

### Wi-Fi hotspot diagnostics

If the hotspot is already on 2.4 GHz, upload the current sketch again and look for `Wi-Fi diagnostics enabled; firmware built ...`. The build stamp confirms the new diagnostic firmware is running. No SSID, password, token, or credential contents are printed.

Wi-Fi events now distinguish association (`Wi-Fi associated with hotspot; waiting for IP address`), DHCP success (`Wi-Fi obtained IP address`), and a numeric disconnect `reason`. Before the ten-second attempt is deliberately stopped, the cloud task prints its numeric `status` and elapsed time. A disconnect logged after `requesting disconnect` may be caused by the retry logic itself; preserve the full earlier event sequence when diagnosing.

For an iPhone, keep **Allow Others to Join** and **Maximize Compatibility** on, and stay on the Personal Hotspot settings screen while the ESP32 attempts to connect. Verify the hotspot name matches `WIFI_SSID` byte-for-byte; straight and typographic apostrophes are different. If needed, test with a simple ASCII device name, then update local `secrets.h` and re-upload. First confirm another device can join that hotspot using its password. See [Apple's hotspot troubleshooting guidance](https://support.apple.com/en-us/119837).

The disconnect reason narrows the next test; an authentication/handshake failure alone does not prove the password is wrong. Association without an IP address points toward DHCP/connection timing. Do not change the device token or CA to fix a failure that occurs before Wi-Fi obtains an IP address. These logs add diagnostic evidence; they do not claim the physical hotspot problem has been fixed.

Run the hardware-independent C++ tests from the repository root:

```powershell
python tests/esp32/run_tests.py
```

The runner requires Python 3 and g++ (or `CXX` set to its executable path). It builds into ignored `build/tests/environment/`. It exercises the production state/alert logic without mocking readings into the live sketch. Host tests and compilation do not verify GPIO signaling, serial output, FreeRTOS timing, or physical sensor recovery.

For bench checks, follow the [DHT11 procedure](../../tests/README.md#dht11-environment-bring-up) and record the actual board, wiring, versions, sample timing, observed logs, and pass/fail evidence. DHT reads briefly disable interrupts; later UART integration must measure frame loss/overflow while sampling.
