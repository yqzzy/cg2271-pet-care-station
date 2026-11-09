# Week 13 demo: sequence and rubric check

[Back to project README](../README.md)

For: the whole team, on demo day. The demo is 5–7 minutes at our table; the TA grades it
against the rubric in the CG2271 Assignment 4 brief (35 marks). Section 1 is what to set up,
section 2 is the script, section 3 is the rubric with the evidence for each mark, and section 4
is what to do if something goes wrong.

**Problem statement (say this first):** a pet care station that keeps the bowl topped up with
food without overfeeding, watches the water level and the room's temperature and humidity,
and lets the owner see everything and control the station light from a ThingsBoard dashboard.
Feeding keeps working even with no internet.

## 1. Before the TA arrives

- [ ] MCXC444 flashed from `main` with `FOOD_CALIBRATION_MODE 0` (`food_calibration.h`) and `SERVO_TEST_ONLY 0` (`servo_test.h`).
- [ ] ESP32 flashed from `main` with the local `secrets.h`; its log shows `MQTT connected with certificate validation`.
- [ ] 2.4 GHz hotspot on (iPhone: *Maximize Compatibility*), phone close to the ESP32.
- [ ] Wiring checked against [the hardware table](hardware.md): UART J4-5 → GPIO2, J4-7 ← GPIO1, shared GND; servo signal J2-4, 5 V J3-10.
- [ ] Food container full of candies. **Bowl above 20 g** so it does not feed by itself at power-up.
- [ ] Water probe in water (so the first demo step can show it going low).
- [ ] Reset **both boards together** (UART sequence numbers restart on boot).
- [ ] Dashboard open on a laptop; PuTTY on the MCU-Link COM port at 115200 to show the MCXC444 log.
- [ ] MCUXpresso open with `app/rtos_setup.c`, `control/button.c`, `communication/mcxc_uart.c`, `sensing/food_sensor.c` and `FreeRTOSConfig_Gen.h` in tabs.
- [ ] Empty-bowl reading is near 0 g. If it drifts more than 5 g below zero the reading is rejected and nothing is reported; recalibrate with the bench tool ([firmware README](../firmware/mcxc444/README.md)).

## 2. Script (about 6 minutes)

| Time | Show | What to point out | Expected output |
|---|---|---|---|
| 0:00 | Architecture in one breath | MCXC444 runs sensing, feeding and the actuators under FreeRTOS; the ESP32 reads the DHT11 and talks to ThingsBoard over MQTT; the boards talk over UART | — |
| 0:30 | **Feeding.** Take candies out until the bowl is 10 g or less | Polled load cell, three confirming samples, then a top-up loop: one portion, wait for the gate (semaphore), 1.5 s settle, re-weigh, repeat until above 20 g. Then 10 s eating time, which stops overfeeding | Gate opens 0.5 s, closes, long beep. MCX log: `[actuator] dispense started/completed`, `[feed] portion 1 added X.X g, bowl Y.Y g -> another portion`, finally `-> full, eating time`. Dashboard `food_weight_g` follows within 1–2 s |
| 1:30 | Empty the bowl again at once | The eating-time lockout | Nothing for 10 s, then a new top-up |
| 2:00 | **Pet button.** Press it | Interrupt-driven sensor; the ISR only queues a timestamp, the button task debounces | Short beep at once. MCX log: `QUEUE BUTTON seq=N`, `TX BUTTON:N*HH`, `RX BUTTON_ACK:N*HH`, `Button ACK matched; retries stopped`. Dashboard `button_presses` +1 |
| 2:30 | **Light.** Dashboard switch ON, then OFF | Cloud → ESP32 → MCXC444 → LED, confirmed back | MCX log: `RX LIGHT:N,1*HH`, `CONSUME LIGHT seq=N on=1; ACK requested`, `TX LIGHT_ACK:N*HH`. LED on; dashboard `light_on = true` |
| 3:00 | **Water.** Lift the probe out, then put it back | Polled ADC with hysteresis (500 / 1500) so it never flickers | `TX SENSOR:0,...` then `1`; dashboard `water_low` true then false; low-water alarm raised and cleared |
| 3:20 | **Environment.** Breathe on the DHT11 | ESP32's own sensor task; alarm needs two readings in a row | `humidity_pct` rises; after about 10 s `humidity_high = true` and the humidity alarm |
| 3:40 | **Offline.** Turn the hotspot off; empty the bowl a little; press the button | Local control does not depend on the cloud | Feeding and beeps still work; dashboard stops updating. Hotspot back on: ESP32 reconnects and the dashboard catches up, including the button count |
| 4:20 | **RTOS in the code** (section 3 lists the files) | Tasks and priorities, ISR design, queues, mutexes and the semaphore, pre-emption and time-slicing | — |
| 6:00 | Wrap up | One sentence on the special features: top-up with lockout, ACK/retry protocol with checksums, MQTT over TLS with RPC and alarms | — |

## 3. Rubric check (demo, 35 marks)

All items were checked against the code on 9 Nov 2026; feeding, water, button and the sensor
packets were also run on the hardware.

| Rubric item | Marks | Status | Evidence to show |
|---|---|---|---|
| **Sensors and actuators** (5-member group: 4 sensors, 3 actuators) | 5 | ✅ | Sensors: load cell + HX711, water level probe, push button module (all MCXC444), DHT11 (ESP32). Actuators: SG90 servo gate, buzzer, RGB LED module (all MCXC444). No on-board parts used. *Risk:* confirm with the TA that the button counts as a sensor |
| ↳ Engineering justification (up to 2 of the 5) | | Prepare | Load cell, water and feeding on the MCXC444 so feeding works offline and the HX711 gets precise clock timing. Button on an interrupt: instant response, never misses a short press. Weight and water change slowly, so polling every 250 ms is enough. DHT11 on the ESP32: its data only goes to the cloud and it has a proven library for the timing-critical one-wire protocol. Priorities: button and actuator 3 (servo timing), UART 2, sensing 1 |
| **Polling and interrupt** | 2 | ✅ | Polled: `FoodSensor_Task` (HX711 every 250 ms), `WaterSensor_Task` (ADC every 250 ms), `Environment_Task` (DHT11 every 5 s). Interrupt: `BUTTON_ISR` in `control/button.c` (falling edge on PTD4) |
| **ISR design** | 3 | ✅ | `BUTTON_ISR`: timestamp → `xQueueSendFromISR` → clear flag → `portYIELD_FROM_ISR`; debounce happens in `Button_Task`. `UART2_FLEXIO_IRQHandler` (`mcxc_uart.c`): stores bytes, queues a complete line with `xQueueSendFromISR`, wakes the TX task with `vTaskNotifyGiveFromISR`; never waits for input or prints |
| **Task design** | 4 | ✅ | MCXC444: 7 tasks (`button`, `actuator`, `uartlink`, `uart_tx`, `uart_rx`, `food`, `water`) created in `app/rtos_setup.c`. Every task blocks on a queue or uses `vTaskDelayUntil`/`vTaskDelay`; none spins. `Actuator_Task` waits with a timeout set to the next servo or beep deadline. ESP32: `Environment` and `Cloud` tasks plus the 20 ms bridge poll in `loop()` |
| **Synchronisation tools** | 2 | ✅ | Queues: `buttonQueue`, `actuatorQueue`, `telemetryQueue` (`rtos_setup.c`), UART `txQueue`/`rxQueue`. Mutexes: shared sensor state (`app/shared_state.c`), UART state (`mcxc_uart.c`), ESP32 environment snapshot. Binary semaphore `dispenseDoneSem`: actuator → food task when the gate closes |
| **RTOS enabled** | 2 | ✅ | `FreeRTOSConfig_Gen.h`: `configUSE_PREEMPTION 1`, `configUSE_TIME_SLICING 1`. Unused parts switched off: software timers, trace, recursive mutexes, counting semaphores, thread-local storage, unused task APIs. Old stand-in test tasks removed |
| **ESP32 use** | 7 | ✅ | Its own sensor (DHT11) with alarm logic, FreeRTOS tasks, a non-blocking UART bridge with ACK handling, MQTT over TLS with certificate validation and SNTP time, RPC handling, JSON. Libraries beyond the template: `DHT`, `PubSubClient`, `ArduinoJson`, `NetworkClientSecure`, `PetStationUart` (ours) |
| **Communications protocol** | 5 | ✅ incl. bonus | Two-way: `SENSOR` and `BUTTON` up; `BUTTON_ACK` and `LIGHT` down; `LIGHT_ACK` up. Beyond lab 3: packetised ASCII frames `<body>*HH\n` with XOR checksum, sequence numbers, acknowledgements, 200 ms retry with three attempts, duplicate suppression ([protocol](../protocol/README.md)) |
| **Online function** | 5 | ✅ incl. bonus | MQTT (listed as advanced) to ThingsBoard Cloud over TLS: live telemetry every 5 s and early on change, RPC light control with confirmed state, dashboard, five alarm rules (`dashboard/alarm-rules/`) |
| **Coherence** | — | ✅ | Every part serves the station: weight drives feeding and the low-food alarm, water drives the low-water alarm, the button notifies the owner, the light is owner-controlled, environment alarms protect the pet |

**Also required** (brief, not the demo rubric): final report PDF and `B0X_YY_Code.zip` with all
source code, both due **Fri 13 Nov 2026, 23:59**. Zip the repository without `build/`,
`Debug/` and `secrets.h`.

## 4. If something goes wrong

| Symptom | Fix |
|---|---|
| No `TX SENSOR` on the MCX log | Bowl reads more than 5 g below zero (lifted or zero drift): put it back, or recalibrate |
| MCX sends but the ESP32 shows no `RX SENSOR` | UART wires or shared ground; reset both boards |
| ESP32 receives but the dashboard is empty | Wi-Fi (`reason=201` = hotspot not found, use 2.4 GHz) or MQTT; watch the ESP32 log |
| Gate does not move | 5 V on J3-10 and signal on J2-4; gate binding. `SERVO_TEST_ONLY 1` flashes a servo-only test console |
| Gate cycles every 2.5 s without dropping food | Food container empty or jammed: there is deliberately no fault limit, so refill it |
| Board resets when the servo moves | USB 5 V sagging: add a 470 µF capacitor across the servo supply |
