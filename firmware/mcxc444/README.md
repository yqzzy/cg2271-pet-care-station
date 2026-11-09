# MCXC444 firmware

[Back to project README](../../README.md)

C implementations and unfinished module templates; not yet a configured MCUXpresso/SDK project. Sensor hardware behaviour remains unverified.

Use a course-approved FRDM-MCXC444 FreeRTOS project with its generated startup, pins, clocks, debug console, linker settings and kernel port. Member 3 coordinates the shared project; Member 2 has added food/water initialization and task registration in `app/rtos_setup.c`. Record the agreed toolchain/SDK versions and verified build steps here after setup.

Keep one `main()` and one scheduler startup. Exclude the standalone `communication/mcxc_test_main.c` from the integrated build. The button code targets the external button on PTD4 with the Port C/D handler; confirm pins, vector ownership and interrupt priorities in the actual project.

Member 2 owns `sensing/`; Member 1 owns `actuators/dispenser.*` and `control/actuator_task.c`; Member 3 owns `app/`, `control/button.c` and feedback; Member 4 owns `communication/`. Each owner implements/tests their task entry points. Member 3 integrates task creation and shared queue/mutex setup, with affected owners reviewing interfaces. See [system design](../../docs/system-design.md) for interface agreements.

## Sensor setup

Normal startup initializes shared state and queues before creating tasks. `FoodSensor_Init()` configures GPIO; `WaterSensor_Init()` configures and calibrates ADC0. Each sensor task is created only when its initialization returns true. Food initialization does not detect whether an HX711 is connected.

| Current sensor setting | Value | Before real operation |
|---|---|---|
| HX711 data and clock | PTB1 and PTB3 | Confirm with the team and check signal voltage |
| Water input | PTB2 / ADC0 channel 12 | Reserve ADC0 and verify module voltage |
| Sampling periods | 250 ms each | Measure under concurrent load |
| Food filter | Four samples | Tune noise versus response time |
| `FOOD_CALIBRATED` | `1u` | Measured 2026-11-06 (80 g reference); redo if the cell or bowl is remounted |
| `WATER_THRESHOLDS_CONFIGURED` | `0u` | Enter measured low/clear thresholds |
| `FOOD_FEEDING_ENABLED` | `1u` | Top up from 10 g or less until above 20 g, one portion at a time, then 10 s eating time |
| Sensor debug logging | Disabled | Enable temporarily for calibration |
| UART fake-data fallback | Disabled | Keep disabled for real-sensor testing |

The current UART adapter sends readings only when both sensors are valid. It does not send validity flags or `water.low`; coordinate a protocol change with Member 4. Do not treat queue acceptance as successful dispensing: the feeding logic re-weighs the bowl after each portion.

Automatic feeding is enabled. After remounting the bowl or load cell, recalibrate with the bench tool below before relying on it.

## Bench tools

Both are switched off by default and are not built unless enabled. Rebuild and flash after
changing a switch, then open the MCU-Link COM port at 115200 (for example PuTTY) and type `h`.

| Tool | Switch | Use |
|---|---|---|
| Load cell and gate tuning (`sensing/food_calibration.c`) | `FOOD_CALIBRATION_MODE 1` in `food_calibration.h` | Replaces the food task. `z` zero, `c <grams>` calibrate, `m` measure; `gate <us>`, `closed <us>`, `open <us>`, `time <ms>`, `drop` tune the dispenser; `p` prints the `#define`s for `food_sensor.c` and `dispenser.c`. Starts from the saved calibration |
| Servo only (`app/servo_test.c`) | `SERVO_TEST_ONLY 1` in `servo_test.h` | Replaces the whole application (no FreeRTOS). Type an angle 0-180, `us <n>` or `sweep` |
