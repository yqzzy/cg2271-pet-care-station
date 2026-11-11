# Week 13 demo: sequence and rubric check

[Back to project README](../README.md)

For: the whole team, on demo day. The demo is 5–7 minutes at our table; the TA grades it
against the rubric in the CG2271 Assignment 4 brief (35 marks). Section 1 is what to set up,
section 2 is the script, section 3 is the code presentation, section 4 is the rubric, and
section 5 is troubleshooting. Read the quoted scripts aloud; use the notes for questions.

**Problem statement (say this first):** a pet care station that keeps the bowl topped up with
food using measured portions and a feeding lockout, watches water level, temperature and humidity,
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

**Opening engineering justification (0:00, about 30 seconds):**

> “The MCXC444 owns food and water sensing and the actuators, so feeding continues offline.
> We poll these slowly changing readings every 250 ms. The button uses an interrupt for prompt
> event capture, with debounce in a task. The ESP32 independently reads the DHT11, evaluates
> alerts and handles the cloud. Button and actuator tasks have priority 3, UART 2 and sensing 1:
> output deadlines and local response take priority, while blocking waits let other tasks run.”

References: [task priorities](../firmware/mcxc444/app/rtos_setup.c#L41),
[food sampling](../firmware/mcxc444/sensing/food_sensor.c#L387),
[water sampling](../firmware/mcxc444/sensing/water_sensor.c#L222),
[ESP32 environment task](../firmware/esp32/pet_care_station/environment.cpp#L104).

| Time | Show | What to point out | Expected output |
|---|---|---|---|
| 0:00 | Architecture and engineering justification | Use the opening script above | — |
| 0:30 | **Feeding.** Take candies out until the bowl is 10 g or less | Polled load cell, three confirming samples, then a top-up loop: one portion, wait for the gate (semaphore), 1.5 s settle, re-weigh, repeat until above 20 g. Then a 10 s lockout before another top-up | Gate opens 0.5 s, closes, long beep. MCX log: `[actuator] dispense started/completed`, `[feed] portion 1 added X.X g, bowl Y.Y g -> another portion`, finally `-> full, eating time`. Dashboard `food_weight_g` follows within 1–2 s |
| 1:30 | Empty the bowl again at once | The eating-time lockout | Nothing for 10 s, then a new top-up |
| 2:00 | **Pet button.** Press it | Interrupt-driven sensor; the ISR only queues a timestamp, the button task debounces | Short beep after debounce. MCX log: `QUEUE BUTTON seq=N`, `TX BUTTON:N*HH`, `RX BUTTON_ACK:N*HH`, `Button ACK matched; retries stopped`. Dashboard `button_presses` +1 |
| 2:30 | **Light.** Dashboard switch ON, then OFF | Cloud → ESP32 → MCXC444 → LED, confirmed back | MCX log: `RX LIGHT:N,1*HH`, `CONSUME LIGHT seq=N on=1; ACK requested`, `TX LIGHT_ACK:N*HH`. LED on; dashboard `light_on = true` |
| 3:00 | **Water.** Lift the probe out, then put it back | Polled ADC with hysteresis (500 / 1500) to reduce threshold chatter | `TX SENSOR:0,...` then `1`; dashboard `water_low` true then false; low-water alarm raised and cleared |
| 3:20 | **Environment.** Breathe on the DHT11 | ESP32's own sensor task; alarm needs two readings in a row | `humidity_pct` rises; after about 10 s `humidity_high = true` and the humidity alarm |
| 3:40 | **Offline.** Turn the hotspot off; empty the bowl a little; press the button | Local control does not depend on the cloud | Feeding and beeps still work; dashboard stops updating. Hotspot back on: ESP32 reconnects and the dashboard catches up, including the button count |
| 4:20 | **RTOS in the code** | Follow [section 3](#3-code-presentation): quote the scripts and point to code; expand snippets only if asked | — |
| 6:00 | Special features and wrap-up | Use the [special-features script](#special-features-script-about-25-seconds) | — |

## 3. Code presentation

### Tasks, priorities and the live example

Open [task creation](../firmware/mcxc444/app/rtos_setup.c#L74) and the configuration:
[pre-emption](../mcuxpresso/pet-station/freertos/freertos-kernel/template/ARM_CM0/FreeRTOSConfig_Gen.h#L44),
[time-slicing](../mcuxpresso/pet-station/freertos/freertos-kernel/template/ARM_CM0/FreeRTOSConfig_Gen.h#L60).

> “FreeRTOS runs the highest-priority Ready task. Higher-priority work pre-empts lower-priority
> work; time-slicing shares CPU time between Ready tasks of equal priority. Our tasks loop and
> block on events or delays, rather than running to completion.”

**Example:** food task (priority 1) runs → button interrupt wakes button task (3) → button task
blocks for 30 ms debounce → other Ready tasks run → button task becomes Ready again.
A high-priority task does not automatically grant lower-priority tasks a turn after a time slice.

**Show during feeding:** press the button while the servo is dispensing. Expect a beep after
debounce and a UART notification while the gate completes its normal cycle; the button neither
restarts the gate nor requests another portion. Hardware PWM keeps running while tasks block.
This demonstrates responsiveness; a scheduler trace would be needed to directly show pre-emption.

### Button ISR and queues

Open [BUTTON_ISR](../firmware/mcxc444/control/button.c#L81) and
[Button_Task](../firmware/mcxc444/control/button.c#L100).

> “The ISR captures a timestamp and queues it. The button task debounces, then sends separate
> beep and notification requests. The ISR never waits through debounce or transmits a packet.”

```c
// Shortened from BUTTON_ISR; surrounding flag check omitted.
BaseType_t higherPriorityWoken = pdFALSE;
ButtonEvent ev = { xTaskGetTickCountFromISR() }; // Interrupt-safe timestamp.
if (xQueueSendFromISR(buttonQueue, &ev, &higherPriorityWoken) != pdTRUE) {
    droppedPresses++; // Queue full: event lost; this may be switch bounce.
}
BUTTON_PORT->ISFR = (1u << BUTTON_PIN); // Clear hardware interrupt flag.
portYIELD_FROM_ISR(higherPriorityWoken); // Request scheduling at ISR exit.
```

`xQueueSendFromISR()` sets the flag if it wakes a task above the interrupted task's priority;
`portYIELD_FROM_ISR()` uses the flag, not sets it. Ready does not mean Running inside the ISR.
The queue stores events and wakes **Button_Task**, which subsequently requests the buzzer:

```text
ISR → buttonQueue → Button_Task → actuatorQueue → Actuator_Task → beep
                              → telemetryQueue → UART → ESP32
```

### Synchronisation and the completion semaphore

Open [RTOS objects](../firmware/mcxc444/app/rtos_setup.c#L60),
[snapshot mutex](../firmware/mcxc444/app/shared_state.c#L49), and its caller,
[reportSensors()](../firmware/mcxc444/communication/uart_link.c#L57).

> “Queues transfer work, the mutex protects shared sensor data, and the semaphore signals
> completion. Once a second, the UART adapter copies a snapshot under the mutex, releases it,
> then sends valid readings. Slow transmission therefore does not hold the sensor-state lock.”

```c
// Equivalent to the shared-state lock helper: convert milliseconds to ticks.
if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    *out = state;                 // Exclusive access: copy consistent data.
    xSemaphoreGive(stateMutex);  // Caller must release after successful take.
} // If acquisition times out, the real CopySnapshot returns false; report is skipped.
```

For feeding, show [request queueing](../firmware/mcxc444/sensing/food_sensor.c#L281),
[completion signal](../firmware/mcxc444/control/actuator_task.c#L127), and
[feeding state machine](../firmware/mcxc444/sensing/food_sensor.c#L318).

```c
// Actuator task: remember one pending completion signal.
xSemaphoreGive(dispenseDoneSem);
// Food task: check and consume it without blocking; 0 means "do not wait".
xSemaphoreTake(dispenseDoneSem, 0);
```

The semaphore retains a signal given between samples. The food task checks every sampling cycle,
so it keeps measuring and detects completion on a later sample. It blocks between samples on
`vTaskDelayUntil()`, not on this semaphore. Completion or rejection gives the same signal; it
does not prove delivery. A 5 s guard also advances to settling if the signal is missing.

**Settling:** keep sampling for 1.5 s so food lands, vibration settles and the four-sample average
catches up. A valid weight above 20 g starts the 10 s lockout; otherwise request another portion.
Invalid weight keeps the task in settling. There is no maximum portion/jam limit.

### Actuator timeout: why outputs still finish with no new command

Open [Actuator_Task](../firmware/mcxc444/control/actuator_task.c#L106) and
[Dispenser_Update()](../firmware/mcxc444/actuators/dispenser.c#L117).

> “A command starts the gate or beep and records a deadline. The actuator waits at most 20 ms,
> shortened for an earlier output deadline. It wakes even without new commands to finish work
> already in progress. Closing the gate and stopping the beep need no separate queue command.”

```c
// Simplified loop: the real code also checks completion and remote light requests.
Feedback_Update(now);   // Stop an expired beep automatically.
Dispenser_Update(now);  // Close/advance the gate when its deadline is reached.
if (xQueueReceive(actuatorQueue, &cmd, wait) == pdTRUE) {
    handleCommand(&cmd, Rtos_NowMs()); // Runs only if a command was received.
}
// Timeout with no command: skip handleCommand; loop back to the update functions.
```

Other tasks run **during the blocked wait**, not only after timeout. Expiry makes this task Ready;
the scheduler decides when it runs. A zero wait returns immediately; an available item also
returns immediately even when the supplied timeout is nonzero.

### UART ISR and task notification

Open [UART ISR](../firmware/mcxc444/communication/mcxc_uart.c#L221),
[sendPacket()](../firmware/mcxc444/communication/mcxc_uart.c#L263), and
[receiveTask()](../firmware/mcxc444/communication/mcxc_uart.c#L354).

> “RX interrupts collect bytes until newline and queue a packet; the receive task validates
> it. TX interrupts feed one byte when hardware is ready, then notify the sender when its
> buffer is consumed. Parsing, logging and retry decisions stay in tasks.”

```c
// TX task waits; it consumes no CPU while blocked.
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
// UART ISR signals this specific task when the buffer is consumed.
vTaskNotifyGiveFromISR(txTaskHandle, &wake);
```

That notification is local buffer completion, **not** an ESP32 ACK. ACKs arrive through RX;
[sendTask()](../firmware/mcxc444/communication/mcxc_uart.c#L292) manages retry timing.

### Special-features script (about 25 seconds)

> “Our special features are measured top-up feeding with settling and a lockout; UART packets
> with checksums, sequence numbers, acknowledgements, retries and duplicate suppression; and
> ThingsBoard telemetry, alarms and light control over MQTT with TLS. Local feeding continues
> without internet, and noticeable food or water changes trigger early dashboard updates.”

| Feature | Code to point to | Clarification if asked |
|---|---|---|
| Top-up and lockout | [FoodSensor_CheckFeeding](../firmware/mcxc444/sensing/food_sensor.c#L318) | Re-weighing determines bowl level; lockout is not a guarantee against overfeeding |
| Packet checks and retry | [handleReceived](../firmware/mcxc444/communication/mcxc_uart.c#L166), [sendPacket/sendTask](../firmware/mcxc444/communication/mcxc_uart.c#L263) | Checksum detects corruption; ACK/retry handles missing confirmation; UART is not encrypted |
| MQTT over TLS and early telemetry | [Cloud_Task](../firmware/esp32/pet_care_station/cloud_client.cpp#L171) | MQTT routes topic messages; TLS encrypts and verifies the server; SNTP supplies time for certificate checks |
| Remote light RPC | [OnMqttMessage](../firmware/esp32/pet_care_station/cloud_client.cpp#L86), [UartBridge_Poll](../firmware/esp32/pet_care_station/uart_bridge.cpp#L41) | Dashboard → ESP32 → UART → MCU; ACK confirms consumption, not measured illumination |

Code links point to repository files and line anchors; line numbers may shift after edits.

## 4. Rubric check (demo, 35 marks)

All items were checked against the code on 9 Nov 2026; feeding, water, button and the sensor
packets were also run on the hardware.

| Rubric item | Marks | Status | Evidence to show |
|---|---|---|---|
| **Sensors and actuators** (5-member group: 4 sensors, 3 actuators) | 5 | ✅ | Sensors: load cell + HX711, water level probe, push button module (all MCXC444), DHT11 (ESP32). Actuators: SG90 servo gate, buzzer, RGB LED module (all MCXC444). No on-board parts used. *Risk:* confirm with the TA that the button counts as a sensor |
| ↳ Engineering justification (up to 2 of the 5) | | Prepare | Load cell, water and feeding on the MCXC444 so feeding works offline and the HX711 gets precise clock timing. Button on an interrupt: prompt event capture with task-level debounce. Weight and water change slowly, so polling every 250 ms is enough. DHT11 on the ESP32: its data only goes to the cloud and it has a proven library for the timing-critical one-wire protocol. Priorities: button and actuator 3 (servo timing), UART 2, sensing 1 |
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

## 5. If something goes wrong

| Symptom | Fix |
|---|---|
| No `TX SENSOR` on the MCX log | Bowl reads more than 5 g below zero (lifted or zero drift): put it back, or recalibrate |
| MCX sends but the ESP32 shows no `RX SENSOR` | UART wires or shared ground; reset both boards |
| ESP32 receives but the dashboard is empty | Wi-Fi (`reason=201` = hotspot not found, use 2.4 GHz) or MQTT; watch the ESP32 log |
| Gate does not move | 5 V on J3-10 and signal on J2-4; gate binding. `SERVO_TEST_ONLY 1` flashes a servo-only test console |
| Gate cycles every 2.5 s without dropping food | Food container empty or jammed: there is deliberately no fault limit, so refill it |
| Board resets when the servo moves | USB 5 V sagging: add a 470 µF capacitor across the servo supply |
