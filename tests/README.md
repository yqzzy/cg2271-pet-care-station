# Manual test checklist

[Back to project README](../README.md)

The table below lists manual acceptance scenarios, not claims of passing hardware tests. Record date, hardware/version, expected result, actual result and evidence for each run. Start with labelled sample data where hardware is unavailable. Automated environment-logic checks are documented separately below.

Use a logging-only actuator queue receiver to test feeding requests without a motor. Run only one receiver for that queue.

| Subsystem or scenario | Expected result |
|---|---|
| Dispenser request | Motor runs for bounded operation and stops; busy requests do not restart the deadline |
| Motor timeout | Stops at configured maximum even with no cloud/UART response |
| Food calibration/tare | Known masses are reasonable; empty bowl is excluded without zeroing food at boot |
| Low food/noisy weight | Stable-low condition produces one request, not a request every sample; rearm/interval follows agreed policy |
| Food rearm and interval | No repeat while food stays low; after recovery, a new confirmed-low request respects the interval since the last accepted queue send |
| Food queue full | Failed enqueue does not latch the request or advance its timestamp; later valid low readings can retry |
| Bowl removal or implausible negative weight | Values below the configured plausibility limit are invalid; measure and tune the provisional -5000 mg limit on the actual fixture |
| HX711 DOUT stuck low | Missing DOUT-high completion is rejected; a genuine zero sample with valid completion is still accepted by the raw-read function |
| HX711 timing | Measure clock pulses under concurrent load against the HX711 datasheet limits |
| Dispense rejected while busy or failed | Record the agreed acceptance/result policy; queue success alone must not be reported as food delivered |
| Low water | Updates status and dashboard alert; no food motor activation due solely to water |
| Button bounce | One deliberate press gives one confirmed event and acknowledgement |
| Food-ready cue | Short cue follows appropriate successful dispensing result; no blocking delay |
| Shared snapshots | Concurrent updates/transmission preserve a consistent snapshot; no lock held during I/O |
| UART partial/corrupt/oversize frame | Rejected or recovered safely, no unintended output change |
| Remote light command | Desired state travels to MCXC444 and matching applied-state result returns |
| Duplicate light command | Harmless repeated set; results remain correlated |
| Failed environment read | Invalid status, not a fabricated zero reading |
| MCXC444/link failure | Environmental sampling continues on powered ESP32; local-board data is stale |
| Wi-Fi loss/reconnect | Local button/control continues; reconnect does not replay stale actuator requests |
| Concurrent operation | During dispensing, press button and send light command; measure response and motor-stop timing |
| Full queues | Failures handled without ISR blocking or unbounded waiting |

## Automated environment logic

Run `python tests/esp32/run_tests.py` from the repository root (Python 3 and g++, or set `CXX` to the compiler executable). The runner compiles the production `environment_state.cpp` with warnings treated as errors and writes its executable under ignored `build/tests/environment/`.

The 15 tests cover initial unknown state, valid units/timestamps, failed/non-finite readings, last-valid preservation and recovery, legitimate zero values, humidity bounds, consecutive alarm confirmation, hysteresis boundaries, interrupted activation/clearing, long sampling gaps, freshness expiry, and ESP32 uptime rollover. They do not simulate success in the live sketch or establish physical sensor, mutex/task timing, or UART reliability.

### Recorded software validation - 2026-10-23

- Host command: `python tests/esp32/run_tests.py`; g++ 13.2.0 with `-std=c++11 -Wall -Wextra -Werror -pedantic`; result: **15 tests, 0 failures**.
- Sketch command: `arduino-cli compile --fqbn esp32:esp32:esp32s2 --build-path build/esp32-environment --warnings all firmware/esp32/pet_care_station`; result: **compiled successfully**, 262529 bytes of program storage and 17180 bytes of global-variable memory.
- Build environment: Arduino CLI 1.5.1, ESP32 Arduino core 3.3.11, Adafruit DHT sensor library 1.4.7, Adafruit Unified Sensor 1.1.15.
- Scope: local software checks against the current environment implementation. No board upload, physical sensor measurement, disconnect/reconnect run, serial-log capture, stack-margin measurement, or UART concurrency test was performed.

## DHT11 environment bring-up

Use the [ESP32 setup guide](../firmware/esp32/README.md#standalone-dht11-setup). GPIO4 is selected by default, with module DATA/S connected to GPIO4 and common ground; verify VCC and pull-up requirements against the actual kit module before powering it.

1. Verify the board profile/upload options and DHT11 module pin labels, then upload the standalone sketch. Open the correct serial console at 115200 baud. No MCXC444 or network connection is required.
2. Confirm the first sample follows the two-second settling delay and subsequent attempts occur about five seconds apart. Record the observed period and `read_ms`, including failures; no immediate retry burst should occur.
3. With a connected sensor, record reasonable temperature/humidity, `valid=1`, `has_last_valid=1`, and increasing sample counts. Readings are not calibrated by these tests.
4. Reset with the sensor disconnected. Check `valid=0`, `nan` current and historical values before any valid sample, `has_last_valid=0`, unknown age (`4294967295`), and increasing failure count. No false zero measurement should appear.
5. After a successful run, disconnect the sensor. The next attempt must be invalid; historical fields remain unchanged and their age increases. Alarm flags may remain latched, but `alerts_known=0`. Reconnect and verify valid sampling resumes without resetting the ESP32.
6. Exercise alarm activation and clearing. For a controlled bench demonstration, temporarily move the configurable thresholds around measured room conditions or use a controlled environment. Record the chosen thresholds and restore the defaults afterwards. Verify two consecutive valid samples, exact activation/clearing boundaries, and interruption by a failed read. Host tests cover deterministic boundary values without heating or wetting the electronics.
7. For a future cloud consumer, verify `Environment_CopySnapshot` followed by `Environment_IsFresh` treats the latest failed sample as invalid and stops considering an otherwise valid snapshot fresh after 15 seconds. Copy/lock failure means unavailable data, not permission to reuse an old snapshot as current.
8. Run for at least 15 minutes and record actual timing, failures, resets, and stack margin. Repeat UART traffic/overflow checks when Member 4's bridge exists; the DHT driver briefly disables interrupts.

Hardware results remain pending until recorded using the template below. Selectively marking code actions complete in the Member 5 plan does not mark these bench checks complete.

## ThingsBoard test-message checks

The 2026-11-04 one-shot synthetic connectivity test has been superseded by live periodic telemetry. Run `python tests/esp32/run_tests.py` for the 15 environment tests, reconnect policy checks, and live payload/scheduler tests. Coverage includes independent invalid/stale sources, three-second UART and fifteen-second environment boundaries, signed milligram conversion, correct low-water inversion, pre-first-sample status, buffer rejection without partial JSON, timer rollover, five-second scheduling and immediate current snapshots after reconnect.

Software validation on **2026-11-06**: all three host suites passed. The integrated sketch compiled for `esp32:esp32:esp32s2` with ESP32 Arduino core 3.3.11, PubSubClient 2.8, and the repository's shared PetStationUart library: 975052 bytes program storage, 42964 bytes global memory. The compiler reported the existing constant-CA-marker-check warnings in Cloud_Init; no build errors. The UART driver's relocated files were reviewed against the originals and are unchanged. This validates software compilation/logic, not physical sensor calibration, UART wiring, task timing or receipt of combined values in ThingsBoard.

The standalone `comms/esp32_test` sketch also compiled against the same shared library: 259272 bytes program storage, 16648 bytes global memory. Both builds used `--library firmware/esp32/libraries/PetStationUart`; no board upload or physical UART test was performed during this implementation.

Follow the [integrated setup](../firmware/esp32/README.md#thingsboard-connectivity-test), then record these bench results:

- Main sketch starts the DHT11 task, reports UART RX2/TX1 initialization, connects to ThingsBoard, and publishes live JSON approximately every five seconds.
- With real calibrated MCU data, verify food grams and Boolean enough/low water agree with board measurements. Do not use the standalone MCU's fixed 125 g fixture as proof of sensing.
- Disconnect UART: after three seconds from the last packet, `mcu_data_fresh=false`; food/water keys are omitted while DHT11 continues.
- Disconnect DHT11: `environment_valid=false`, temperature/humidity/alert fields omitted, UART data continues. Reconnect each source and verify recovery.
- With no UART packet or no first DHT sample, flags remain false and missing measurements are not zero-filled.
- Lose Wi-Fi and restore it: sensing/UART continue; reconnect publishes current values without replaying history.
- Confirm dashboard widgets and alarms honor validity flags instead of showing previously stored values as current.
- Measure stack margins, sensor sampling jitter and UART overflow/frame loss during TLS connection and DHT reads. Hardware results remain pending until recorded.

## Evidence record template

- Scenario / tester / date:
- Commit and hardware setup:
- Configured periods, thresholds and deadlines:
- Expected result:
- Actual result and measured latency:
- Pass/fail, evidence link and follow-up:

Integration sequence and responsible members are in [the team guide](../docs/team-guide.md#integration-order).

## Software review status

Desktop checks of the extracted food conversion, filter and feeding functions passed for calibration in both raw-count directions, filter warm-up/rolling average, low confirmation, rearm, queue-full retry and interval arithmetic across a simulated 32-bit tick wrap. These used simulated calibration settings and queue/GPIO substitutes, not the target SDK or hardware. The temporary review harness is not a committed regression suite.

Both review fixes are implemented. Follow-up desktop checks passed for the -5000 mg boundary and values below it, both calibration directions, unchanged output on rejection, valid zero/positive/negative HX711 transfers, stuck-low DOUT, missing completion, not-ready/null handling and balanced critical sections. Filter and queue behaviour were also checked, including an invalid negative sample clearing a pending low-food confirmation. GPIO transitions and timing were simulated; neither a full target firmware build nor physical acceptance tests have been demonstrated by these checks.
