# ESP32: required fixes before the Week 13 demo

[Back to project README](../README.md)

For: Members 4 and 5 (ESP32 UART bridge, cloud and dashboard). Written 9 Nov 2026 after
testing both boards together.

The MCXC444 side is working: it sends `SENSOR` packets every second and `BUTTON` events, and
it accepts `LIGHT` commands. The ESP32 currently **only receives**. It never sends anything
back to the MCXC444, so the demo would lose marks in several rubric rows:

| Rubric row (demo, 35 marks) | Why it is at risk now | Fixed by |
|---|---|---|
| Communications protocol (5): "good use of 2-way communications" | ESP32 → MCXC444 traffic never happens | Tasks 2 and 3 |
| Task design (4): "no obvious bugs" | Only the first button press is ever handled (details in task 2) | Task 2 |
| Online function (5) | Data only goes up to ThingsBoard; nothing comes back down | Task 3 |
| Coherence | The station LED never turns on, because nothing sends it a command | Task 3 |

Also: the repository cannot build the ESP32 sketch (task 1), and the code zip submitted with
the final report (due Fri 13 Nov) must build.

## Task 1: push the files that are missing from the repo

`pet_care_station` includes files that exist only on your computer:

| Missing | Used by |
|---|---|
| `firmware/esp32/libraries/PetStationUart/` (`esp32_uart.h`, `esp32_uart.cpp`, `library.properties`) | `uart_bridge.cpp` (`#include <esp32_uart.h>`). The old copy was deleted in commit `3e72a21` when it moved into the library, but the library itself was never added |
| `firmware/esp32/pet_care_station/live_telemetry.h` (`McuTelemetry`, `PeriodicTelemetry`, `BuildLiveTelemetry`) | `uart_bridge.h`, `cloud_client.cpp` |
| `tests/esp32/run_tests.py` and its test sources | `firmware/esp32/README.md` |

Check that `.gitignore` does not exclude them, commit them, and confirm a clean clone builds
with the `arduino-cli compile ...` command in [the ESP32 README](../firmware/esp32/README.md).
**Never commit `secrets.h`.**

## Task 2: consume button events (fixes the "only first press" bug)

**Symptom** (ESP32 serial, seen on 6 Nov):

```
[ESP32 UART] RX BUTTON:1*1D
[ESP32 UART] Button pending for consumeButton()
[ESP32 UART] RX BUTTON:2*1E
[ESP32 UART] Ignored: unknown/malformed message or unmatched ACK
```

**Cause:** nothing in the sketch calls `consumeButton()`. Press 1 stays pending forever, so
`BUTTON_ACK` is never sent and every later press is ignored. The MCXC444 retries each press
3 times, 200 ms apart (`UART_RETRY_MS`, `UART_MAX_ATTEMPTS` in `mcxc_uart.c`), so the ACK must
go out **within about 0.6 s** of the press.

**Do not call it from `Cloud_Task`.** That task blocks for up to 10 s while connecting to
Wi-Fi or MQTT, which would miss the 0.6 s window. Poll the UART module from somewhere that
never blocks on the network, and hand events to the cloud task. Suggested shape, in
`uart_bridge.*` (one owner for every consume/send call, as `comms/README.md` requires):

```cpp
// uart_bridge.h
void UartBridge_Poll(void);                         // every 20-50 ms, never blocks, no network I/O
uint32_t UartBridge_TakeButtonPresses(void);        // cloud task: presses not yet published (clears)
void UartBridge_RequestLight(bool on);              // cloud task: latest dashboard request wins
bool UartBridge_TakeLightConfirmed(bool *on);       // cloud task: true once per MCXC444 ACK

// uart_bridge.cpp: UartBridge_Poll()
//   if (consumeButton(&seq))      -> buttonPresses++            (sends BUTTON_ACK for us)
//   if (lightRequestPending && sendLight(desiredOn)) -> lightRequestPending = false
//   if (consumeLightAck(&on))     -> confirmedOn = on; confirmedReady = true
// Protect the shared variables with a mutex or portMUX critical section.
```

Call `UartBridge_Poll()` from `loop()` and change its delay from 1000 ms to about 20 ms.
`loop()` runs in its own task, so this does not interfere with the environment or cloud tasks.

In `Cloud_Task`, when MQTT is connected, publish the press count. It can go in the next
5-second telemetry message, or straight away as its own message:

```json
{"button_presses": 3}
```

**Expected after the fix** (press the button three times):

| Board | Log |
|---|---|
| ESP32 | `[ESP32 UART] CONSUME BUTTON seq=1; BUTTON_ACK requested`, then `TX BUTTON_ACK:1*..` |
| MCXC444 (MCU-Link COM port, 115200) | `[MCXC UART] Button ACK matched; retries stopped` |
| Both | seq 2 and seq 3 are handled the same way: no more `Ignored` lines |

## Task 3: control the station LED from ThingsBoard (RPC)

The MCXC444 already handles `LIGHT:<seq>,<0|1>`: its actuator task polls `consumeLight()`
every 20 ms, drives the LED (external RGB LED module on PTD6) and replies `LIGHT_ACK`
automatically. The ESP32 only has to send the command:

1. **Subscribe.** Call `mqtt.setCallback(OnMqttMessage)` once in `Cloud_Init()`. After every
   successful `mqtt.connect(...)`, call `mqtt.subscribe("v1/devices/me/rpc/request/+")`;
   subscriptions are lost on reconnect.
2. **Handle the request in the callback.** It runs inside `mqtt.loop()` in `Cloud_Task`. The
   topic ends with the request id, and the payload looks like
   `{"method":"setValue","params":true}`.
   - For a set method, call `UartBridge_RequestLight(params == true)`.
   - For a get method, answer with the last confirmed state.
   - Reply on `v1/devices/me/rpc/response/<id>`; publishing from inside the callback is fine
     with PubSubClient.
   - Use the method names your switch widget is configured with (open its settings). The
     legacy *Switch control* widget uses `getValue` and `setValue`.
3. **Report the confirmed state.** When `UartBridge_TakeLightConfirmed(&on)` returns true,
   publish `{"light_on": true}` or `false` to `v1/devices/me/telemetry`. Show `light_on` on
   the dashboard as the *confirmed* state, separate from the switch, which is only the
   *requested* state.

**Expected after the fix** (flip the dashboard switch on):

| Board | Log |
|---|---|
| ESP32 | `QUEUE LIGHT seq=1`, `TX LIGHT:1,1*..`, then `CONSUME LIGHT ACK on=1` |
| MCXC444 | `[MCXC UART] RX LIGHT:1,1*..`, then `New light command pending for consumeLight()` |
| Hardware | LED turns on within about 0.1 s; `light_on` becomes `true` on the dashboard |

## Task 4: ThingsBoard dashboard and alarms

Telemetry keys already published every 5 s: `food_weight_g`, `water_enough`, `water_low`,
`temperature_c`, `humidity_pct`, `temperature_high`, `humidity_high`, `mcu_data_fresh`,
`environment_valid`. Tasks 2 and 3 add `button_presses` and `light_on`.

- **Widgets:**
  - Food weight: value card plus a time-series chart.
  - Water: boolean indicator. `water_enough` and `water_low` are true/false, so they will
    not appear on a line chart.
  - Temperature and humidity: charts.
  - Button presses: count, plus the time of the last press.
  - Light: switch for the request, indicator for `light_on`.
- **Alarm rules** (device profile → Alarm rules): low food `food_weight_g <= 10`, low water
  `water_low == true`, `temperature_high == true`, `humidity_high == true`. Mark food and
  water as stale when `mcu_data_fresh == false`, and environment data as stale when
  `environment_valid == false`.
- Export the dashboard JSON into `dashboard/`, with no tokens in it.

## Task 5: Wi-Fi on demo day

The last run showed `[cloud] Wi-Fi disconnected: reason=201`, which means the ESP32 cannot
find the hotspot at all (not a password problem).

- Use a **2.4 GHz** hotspot. On an iPhone, turn on *Maximize Compatibility* and keep the
  Personal Hotspot screen open while the ESP32 connects.
- Check that `WIFI_SSID` in `secrets.h` matches the hotspot name exactly.
- Test in the lab room before the demo.

## Done when

- [ ] A fresh clone builds the ESP32 sketch (task 1)
- [ ] Five button presses produce five ACKs, five beeps and `button_presses` 5 on the dashboard (task 2)
- [ ] The dashboard switch turns the LED on and off, and `light_on` follows (task 3)
- [ ] The low-food, low-water and temperature/humidity alarms fire on the dashboard (task 4)
- [ ] The ESP32 reaches `MQTT connected` on the demo-day hotspot (task 5)
- [ ] Final report sections written: *ESP32 use*, *Online function* (data sent and received),
      and the ESP32 half of *Communication protocol*

Reset both boards together before the demo: the UART sequence numbers restart on boot.
