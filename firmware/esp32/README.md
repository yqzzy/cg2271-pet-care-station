# ESP32 firmware

[Back to project README](../../README.md)

The Arduino C++ sketch implements the DHT11 environmental task and optional ThingsBoard test connectivity. With local configuration, a separate cloud task sends one labelled synthetic telemetry message per boot. Without configuration, sensing runs and cloud startup reports that it is disabled. UART integration and live cloud telemetry remain future work; hardware operation still requires verification.

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
arduino-cli compile --fqbn esp32:esp32:esp32s2 --build-path build/esp32-environment --warnings all firmware/esp32/pet_care_station
```

This generic S2 build target checks compilation; it does not identify the physical board or validate its USB/flash configuration. Use your verified board profile to upload from the IDE, then open Serial Monitor at **115200 baud**. Install **PubSubClient 2.8** as well as the DHT libraries to build the current sketch. Missing cloud credentials disable networking at runtime; ArduinoJson and the MCXC444 are not required.

### Runtime and public interface

`setup()` checks `Environment_Init()` and task allocation, then starts exactly one `Environment_Task`. Initialization now returns `bool` so startup can handle invalid GPIO/mutex allocation failure; the original comment-only proposal used `void`. Future startup integration should retain this check.

The task waits two seconds for sensor settling, reads a humidity/Celsius-temperature pair every five seconds, and never retries immediately after a failed read. Sensor transactions and serial logging occur outside the snapshot mutex. Overruns do not trigger a burst of catch-up reads. Cloud startup is optional and uses a separate task; UART startup remains absent.

## ThingsBoard connectivity test

This first cloud increment sends a fixed sample, not DHT11 measurements. It has no dashboard RPC, actuator control, UART event forwarding, or offline event history.

1. In your ThingsBoard account, create a device named `pet-care-test` using access-token credentials. Use a separate test device so synthetic values cannot be confused with real measurements.
2. Copy `pet_care_station/secrets.example.h` to `pet_care_station/secrets.h` locally. The latter is ignored by Git. Set `WIFI_SSID`, `WIFI_PASSWORD`, `THINGSBOARD_HOST`, and `THINGSBOARD_DEVICE_TOKEN` there. An empty password is allowed for an open network. Supply only a hostname, such as `thingsboard.cloud` or your account's regional hostname, without a URL scheme/path. Never paste secrets into a commit or serial log.
3. Obtain the endpoint's trusted **root CA PEM** from its **Check connectivity** dialog. Paste the full certificate, including BEGIN/END lines, between the `R"PEM(` and `)PEM"` delimiters of `TLS_CA_CERT`. The public CA is distinct from a device token or private key. Do not disable TLS verification. Empty configuration or missing certificate markers disables cloud startup.
4. Install **PubSubClient by Nick O'Leary, version 2.8** through Library Manager (or `arduino-cli lib install "PubSubClient@2.8"`). Wi-Fi and NetworkClientSecure come from ESP32 Arduino core 3.3.11; no ThingsBoard SDK or ArduinoJson dependency is needed for this fixed JSON message.
5. Upload the main `pet_care_station.ino` sketch using your board/USB settings. Open Serial Monitor at 115200 baud. The cloud task joins Wi-Fi, obtains SNTP time, validates the server certificate on port 8883, and authenticates MQTT using the device token as username.
6. Look for `[cloud] MQTT connected with certificate validation` followed by `[cloud] Test telemetry sent (QoS 0); verify Latest telemetry in ThingsBoard`. In ThingsBoard, open the test device's **Latest telemetry** tab and confirm the values below. The serial publish log alone does not prove server receipt.

The non-retained JSON is published to `v1/devices/me/telemetry`:

```json
{"test_telemetry":true,"data_source":"synthetic","test_message":"esp32-connectivity-check","test_temperature_c":25.0,"test_humidity_pct":60.0}
```

The sample is sent once per boot after a successful local publish. MQTT remains serviced after that and may reconnect, but the sample is not intentionally resent. Reset the board to request another test message. Failed publish calls are retried; PubSubClient uses outbound QoS 0, so loss or an ambiguous duplicate is possible if a connection fails during transmission. This is a connectivity smoke test, not an exactly-once delivery guarantee.

Network waits are confined to `Cloud_Task`. Wi-Fi and SNTP each have ten-second attempt windows; failed attempts use 1/2/4/8/16/30-second backoff. TCP connection timeout is three seconds, TLS handshake timeout five seconds, and MQTT protocol timeout two seconds; DNS also uses the core's own timeout. These are separate bounds, not a promised total connection latency. Both tasks yield; no startup wait for Wi-Fi/time is added.

`Cloud_Init()` returns a Boolean configuration/resource check and does not connect. Create exactly one `Cloud_Task` on success; the sketch allocates an 8192-byte stack at priority 1. Only that task accesses Wi-Fi/TLS/MQTT. Hardware measurements of stack margin, sampling jitter, and reconnect timing are still needed.

If cloud startup is disabled, check your local configuration. For repeated connection failures, check SSID/password and 2.4 GHz availability, hostname/region, device token, access to NTP, outbound port 8883, and the correct root CA. The implementation never falls back to insecure TLS or prints credentials. No live connection has been verified without your local configuration.

References: [ThingsBoard authentication and TLS](https://thingsboard.io/docs/reference/mqtt-api/getting-connected/), [telemetry API](https://thingsboard.io/docs/reference/mqtt-api/telemetry/), and [PubSubClient API](https://pubsubclient.knolleary.net/api).

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
