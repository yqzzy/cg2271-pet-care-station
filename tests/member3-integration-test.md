# Integration test: whole MCXC444 firmware plus the ESP32 UART link

For the teammate with the boards. The code has NOT been built or run on hardware yet, so expect small fixes. Record expected and actual results in the pull request.

## Settings as the repo stands

| Setting | Value | Effect |
|---|---|---|
| `USE_TEST_ACTUATOR_TASK` (`app/rtos_setup.c`) | 0 | Member 1's real `Actuator_Task` runs |
| `UARTLINK_USE_TEST_SENSOR_DATA` (`communication/uart_link.c`) | 0 | Only real sensor values are sent |
| `FOOD_CALIBRATED` (`sensing/`) | 1 | Food weight is calibrated (80 g reference), so sensor packets are sent |
| `FOOD_FEEDING_ENABLED` (`sensing/`) | 1 | Automatic feeding: one portion at 10 g or less, rearms at 20 g |
| `BUTTON_TEST_DISPENSE` (`control/button.c`) | 0 | A button press does not dispense |

## Setup

### MCXC444 (MCUXpresso)

1. Course project with FreeRTOS: pre-emption on, time slicing on (`configUSE_TIME_SLICING = 1`), mutexes on, dynamic allocation on. Raise `configTOTAL_HEAP_SIZE` if start-up halts: this firmware now has about nine tasks plus queues.
2. Build everything under `firmware/mcxc444/` except `communication/mcxc_test_main.c` (only ONE `main()`). `app/test_tasks.c` can stay in the build but is unused.
3. Wiring:
   - UART: MCXC444 PTE22 (TX) to the ESP32 RX pin, PTE23 (RX) to the ESP32 TX pin, grounds joined, 3.3 V logic.
   - Servo (SG90): signal to PTD2, red wire to a SEPARATE 5 V supply (not the board 3.3 V pin), brown wire to ground, and that ground joined to the board ground.
   - Water sensor: signal to PTB2 (J4 pin 6). HX711: DOUT to PTB1, PD_SCK to PTB3. Check each module's supply and logic voltage first.
   - Buzzer: the defaults assume a PASSIVE buzzer on PTE21. Member 4's old test used an ACTIVE buzzer on PTB0. Check which is fitted and tell Member 3 (see the table in `docs/hardware.md`).
   - Light: on-board red LED (PTE31, active low). Button: on-board SW2 (PTC3).

### ESP32

Flash `firmware/esp32/comms/esp32_test/esp32_test.ino` (the folder contains the UART module), open the Serial Monitor at 9600 baud, and reset BOTH boards together every time.

## Checks with the settings as they stand

| # | Action | Expect on the MCXC444 | Expect on the ESP32 Serial Monitor | Actual |
|---|--------|-----------------------|------------------------------------|--------|
| 1 | Reset both boards | Console shows `[MCXC UART]` log lines. No `UART init failed`, `Dispenser init failed`, `Water sensor initialization failed` or `Food sensor initialization failed`. The servo gate moves to closed and holds. | Prompt `1=ON, 0=OFF, p=consume button` | |
| 2 | Wait 10 seconds | Nothing else | No `water=... food=...` lines. This is expected while the food sensor is uncalibrated. | |
| 3 | Press SW2 once | Short beep (about 80 ms). The light does NOT toggle (that was only the stand-in task). | Nothing yet | |
| 4 | Type `p` within a few seconds | Nothing | `Consumed button N; ACK queued` | |
| 5 | Type `1`, then `0` | Red LED on, then off | `Light accepted=1`, then `Light command ACK consumed: requested state=1` (then `=0`). The `light seq=... status=2` line shows consumed. | |
| 6 | Hold SW2 down | One beep only | At most one button event | |
| 7 | Tap SW2 five times, 1 second apart, typing `p` after each | Five beeps | Five `Consumed button N` lines | |
| 8 | Light command while pressing SW2 | Beep and the light ends in the commanded state | Light ACK still arrives | |

## Sensor link test (fixed test data)

1. Set `UARTLINK_USE_TEST_SENSOR_DATA` to 1 in `communication/uart_link.c`, rebuild, reset both boards.
2. Console prints `[UartLink] WARNING ... FIXED TEST sensor values`. The ESP32 prints `water=600 food=125000 mg age=...` about once a second, with age under about 1000 ms.
3. Set it back to 0 afterwards so test data is never mistaken for a real reading.

## Water sensor test

1. Set `WATER_DEBUG_LOG` to 1 in `sensing/water_sensor.c` and rebuild.
2. Console prints `water raw=<0..4095> ...` about four times a second. Dry sensor, then wet it a little, and note whether the number rises with water. Record both values.
3. Verify the trial thresholds with the mounted probe: dry at or below 100 gives LOW, wet at or above 4000 gives OK. Drain below the probe and verify it returns to LOW. Turn off raw logging after verification.

## Food sensor test (needs the HX711 and bowl)

1. Set `FOOD_DEBUG_LOG` to 1 in `sensing/food_sensor.c`. Console prints `food raw=...`.
2. Record the raw count with the empty bowl, then with a known weight on it (note the weight in milligrams). Put these in `FOOD_EMPTY_RAW`, `FOOD_REFERENCE_RAW` and `FOOD_REFERENCE_MASS_MG`, set `FOOD_CALIBRATED` to 1, turn the debug log off, and rebuild.
3. With the real water sensor running, the ESP32 should now show real `water=... food=... mg` lines. Add 50 g and check the food number rises by about 50000.
4. `food reading not ready or transfer failed` on every line means the HX711 is not wired or not answering.

## Dispenser test (no HX711 needed)

1. Set `BUTTON_TEST_DISPENSE` to 1 in `control/button.c` and rebuild. This is a bench test only.
2. Press SW2: short beep, then the console shows `[actuator] dispense started`, the servo gate swings open for about half a second and closes, the console shows `[actuator] dispense completed`, and a longer beep (about 400 ms) sounds as the food-ready cue.
3. Press SW2 twice quickly: the second press shows `[actuator] dispense rejected: busy` and does not restart or extend the cycle.
4. The open/closed angles and times in `actuators/dispenser.c` (`SERVO_OPEN_US`, `SERVO_CLOSED_US`, `DISPENSE_OPEN_MS`) are placeholders. Adjust them if the gate does not open or close fully. Note the values that work.
5. Set `BUTTON_TEST_DISPENSE` back to 0.

## Automatic feeding test (after food calibration)

1. Choose a low threshold and a re-arm threshold from measured bowl weights (low must be below re-arm), set `FOOD_FEEDING_ENABLED` to 1, rebuild.
2. Empty the bowl below the low threshold: after about three samples (under a second) exactly ONE dispense happens, with the food-ready beep.
3. Leave the bowl low: no repeat. Fill above the re-arm threshold, then empty again: a new dispense only after the 10-second minimum interval.

## Soak and failure checks

| Check | Expect |
|---|---|
| Run 10 minutes with button presses and light commands | No reset, no hang, beeps stay on time |
| Unplug the ESP32 wires, press SW2, dispense if possible | Local beep, light and servo still work. Link works again after resetting both boards. |
| In the debugger, read `droppedPresses` in `control/button.c` | 0 |

## What to report if something fails

- Nothing on the console: check the debug console baud and `BOARD_InitDebugConsole()`.
- Halts at start-up or a task will not start: raise `configTOTAL_HEAP_SIZE`.
- Buzzer silent but the logs look right: wrong buzzer pin or type. If it stays silent with the right pin, set `FEEDBACK_SWITCH_CORE_CLOCK_TO_LIRC` to 1 at the top of `actuators/feedback.c`, rebuild, and re-check the console text and the UART link.
- Servo twitches or the board resets when the servo moves: the servo is drawing too much from the wrong supply, or the grounds are not joined.
- No `Consumed button` after a press: check TX/RX are not swapped, grounds are joined, and both boards were reset together.
