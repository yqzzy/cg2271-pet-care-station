/*
 * File: main.c
 * Owner: Member 3
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: Application startup only. No business logic here.
 *
 * Follows the same startup pattern as Member 4's mcxc_test_main.c (generated board files).
 * Do NOT compile mcxc_test_main.c together with this file: only ONE main() may exist. Exclude
 * mcxc_test_main.c from the MCUXpresso build. If the generated project already has its own
 * main(), keep its generated initialisation and add only the Rtos_Start() call and include.
 *
 * TODO: record the toolchain/SDK version and the build steps in firmware/mcxc444/README.md.
 */
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"

#include "rtos_setup.h"

int main(void) {
    /* Board-generated initialisation */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    BOARD_InitDebugConsole();
#endif

    /* Creates queues, state and tasks, initialises the button, buzzer, light and UART, and
     * starts the scheduler. Never returns. */
    Rtos_Start();

    for (;;) { }
}