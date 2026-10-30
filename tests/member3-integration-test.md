# Member 3 integration test: button, buzzer, light and UART link

For the teammate with the boards. The code has NOT been built or run on hardware yet. Record expected and actual results in the pull request.

## What this tests

The MCXC444 button, buzzer, light and shared-state code together with Member 4's UART module, against Member 4's ESP32 test sketch. A temporary stand-in actuator task (`app/test_tasks.c`) stands in for Member 1's task, and the UART adapter sends FIXED TEST sensor values (water = 600, food = 125000 mg) because Member 2's sensors do not exist yet.

## Setup

### MCXC444 (MCUXpresso)

1. Use the course project with FreeRTOS. In `FreeRTOSConfig.h` check: pre-emption on, time slicing on (`configUSE_TIME_SLICING = 1`), mutexes on, dynamic allocation on, and a large enough `configTOTAL_HEAP_SIZE`. If the app halts at start-up or `uartInit` fails, raise the heap first.
2. Add these folders from `firmware/mcxc444/`: `app/`, `control/button.c`, `actuators/feedback.c`, `communication/mcxc_uart.c`, `communication/uart_link.c` (and the matching `.h` files). The comment-only files (`dispenser.c`, `actuator_task.c`, `food_sensor.c`, `water_sensor.c`) can stay out of the build.
3. Only ONE `main()` may exist. Exclude `communication/mcxc_test_main.c` from the build, and either exclude the project's generated main or paste the `Rtos_Start()` call from `app/main.c` into it.
4. Wiring: connect the MCXC444 UART2 TX (PTE22) to the ESP32 RX pin, the MCXC444 RX (PTE23) to the ESP32 TX pin, and join the grounds. Logic levels are 3.3 V.
5. Before testing, check which buzzer is fitted and which pin it is on. Defaults in `feedback.c` assume a PASSIVE buzzer on PTE21 (TPM1 channel 1). Member 4's old test used an ACTIVE buzzer on PTB0. If the buzzer is on PTB0, change the four buzzer pin lines at the top of `feedback.c` to PORTB, `SIM_SCGC5_PORTB_MASK`, pin 0, channel 0 (PTB0 is also on TPM1, ALT3) and tell Member 3. If it is an active buzzer, report it, because it should be driven by a plain GPIO instead of a tone.
6. Default pins: button = on-board SW2 (PTC3), light = on-board red LED (PTE31, active low). No external parts are needed for those two.

### ESP32

1. Open `firmware/esp32/comms/esp32_test/esp32_test.ino` (the folder already contains `esp32_uart.cpp` and `esp32_uart.h`) and flash it.
2. Open the Serial Monitor at 9600 baud.
3. Reset BOTH boards together every time. Sequence counters restart on boot and there is no session recovery.

## Checks

| # | Action | Expect on the MCXC444 | Expect on the ESP32 Serial Monitor | Actual |
|---|--------|-----------------------|------------------------------------|--------|
| 1 | Reset both boards | Console prints a `[UartLink] WARNING ... FIXED TEST sensor values` line and `[MCXC UART]` log lines. No `UART init failed`. | Prompt `1=ON, 0=OFF, p=consume button` | |
| 2 | Wait 5 seconds | Nothing else | About once a second: `water=600 food=125000 mg age=...` where age stays under about 1000 ms | |
| 3 | Press SW2 once | Short beep (about 80 ms), red LED toggles, console prints `[test actuator] button ack: beep + light toggle` | Nothing yet | |
| 4 | Type `p` within a few seconds of check 3 | Nothing | `Consumed button N; ACK queued` (N is the sequence number and goes up on later presses) | |
| 5 | Type `p` again | Nothing | `No pending button` | |
| 6 | Type `1` | Console prints `[test actuator] light command on=1 applied=1`, red LED ON | `Light accepted=1`, then `Light command ACK consumed: requested state=1` | |
| 7 | Type `0` | Console prints `... on=0 applied=0`, red LED OFF | `Light accepted=1`, then `Light command ACK consumed: requested state=0` | |
| 8 | Look at the `light seq=... status=...` line after check 6 or 7 | | `status=2` (consumed). Codes: 0 idle, 1 waiting, 2 consumed, 3 timed out | |
| 9 | Press and HOLD SW2 | One beep only (falling edge) | At most one button event | |
| 10 | Tap SW2 five times, 1 second apart, typing `p` after each | Five beeps | Five `Consumed button N` lines, N increasing | |
| 11 | Tap SW2 very fast, three times, then type `p` repeatedly | Three beeps | Events arrive one at a time, because the link allows one outstanding button event until it is ACKed. `No pending button` after the last one. | |
| 12 | Press SW2 and do NOT type `p` | Beep, light toggles | After about 600 ms the MCXC444 gives up retrying (three attempts total). This is expected, and no crash. Typing `p` later may still consume it. | |
| 13 | Send a light command while pressing SW2 | Both work: beep and the light ends in the commanded state | Light ACK still arrives | |
| 14 | Unplug the ESP32 UART wires for 10 seconds, press SW2, replug, reset both | No hang, beep still works when unplugged | Link works again after the reset | |
| 15 | In the debugger, read `droppedPresses` in `control/button.c` | 0 after the checks above | | |

## What to report if something fails

- Nothing at all on the console: check the debug console baud and that `BOARD_InitDebugConsole()` runs. Tick timing is wrong if beep lengths look far off.
- `UART init failed` or a stuck start-up: raise `configTOTAL_HEAP_SIZE` first.
- Buzzer silent but the console shows the beep message: wrong buzzer pin or buzzer type (see setup step 5). If it stays silent with the right pin, set `FEEDBACK_SWITCH_CORE_CLOCK_TO_LIRC` to 1 at the top of `feedback.c`, rebuild, and re-check that the console text and the UART link are still fine.
- No sensor lines on the ESP32: check TX/RX are not swapped, the grounds are joined, and both boards were reset together.
- Button works but `p` says `No pending button`: the press was probably not accepted by the debounce (very short tap), or check 12 timing. Tap again.

## Before the real tasks replace the stand-ins

- When Member 1 pushes the actuator task: set `USE_TEST_ACTUATOR_TASK` to 0 in `app/rtos_setup.c`, enable their task there, and make sure it polls `consumeLight()` every 20 ms or faster.
- When Member 2 pushes the sensor tasks: set `UARTLINK_USE_TEST_SENSOR_DATA` to 0 in `communication/uart_link.c`.
