/*
 * File: test_tasks.c
 * Owner: Member 3
 * Status: TEMPORARY stand-in. Written for review; NOT yet built or tested on hardware.
 *
 * TestActuator_Task stands in for Member 1's Actuator_Task until it is pushed. It is the ONLY
 * task that touches the buzzer/light outputs, and it shows how the real actuator task must
 * use Member 3's and Member 4's functions:
 *   - polls consumeLight() (Member 4) at least every LIGHT_POLL_MS and applies the light at once
 *   - consumes actuatorQueue (button acknowledgement beep, later dispense/food-ready)
 *   - calls Feedback_Update() every time it wakes so a beep ends on time
 *
 * Remove: set USE_TEST_ACTUATOR_TASK to 0 in rtos_setup.c when Member 1's task lands, then
 * delete this file.
 *
 * Expected behaviour with the default pins:
 *   - each accepted press of SW2 gives a short beep and TOGGLES the light (test only)
 *   - a light command from the ESP32 (send '1' or '0' in its Serial Monitor) sets the light
 *     ON/OFF and the ESP32 then prints that the command was ACKed
 */
#include "test_tasks.h"
#include "rtos_types.h"
#include "../actuators/feedback.h"
#include "../communication/mcxc_uart.h"

#include "fsl_debug_console.h"   /* PRINTF */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Light commands are polled, so never wait longer than this */
#define LIGHT_POLL_MS  20u

void TestActuator_Task(void *argument) {
    (void)argument;

    ActuatorCmd cmd;

    for (;;) {
        uint32_t now = Rtos_NowMs();
        Feedback_Update(now);                       /* stops an expired beep */

        /* Remote light command. consumeLight() queues the ACK itself, BEFORE we write the
         * GPIO, so apply the light immediately (the ACK means "consumed", not "lit"). */
        bool on;
        if (consumeLight(&on)) {
            bool applied = Feedback_SetLight(on);
            PRINTF("[test actuator] light command on=%u applied=%u\r\n",
                   (unsigned)on, (unsigned)applied);
        }

        /* Wait for the next command, but no longer than the light poll period or the end
         * of the current beep, whichever is first */
        TickType_t wait = pdMS_TO_TICKS(LIGHT_POLL_MS);
        uint32_t deadline;
        if (Feedback_GetNextDeadlineMs(&deadline)) {
            int32_t remaining = (int32_t)(deadline - now);
            TickType_t beepWait = (remaining > 0) ? pdMS_TO_TICKS(remaining) : 0;
            if (beepWait < wait) {
                wait = beepWait;
            }
        }

        if (xQueueReceive(actuatorQueue, &cmd, wait) != pdTRUE) {
            continue;                               /* timed out: loop again */
        }

        switch (cmd.type) {
        case ACT_BEEP_ACK:
            Feedback_StartCue(FEEDBACK_CUE_ACK, Rtos_NowMs());
            (void)Feedback_SetLight(!Feedback_LightIsOn());   /* visible toggle, test only */
            PRINTF("[test actuator] button ack: beep + light toggle\r\n");
            break;

        case ACT_BEEP_FOOD_READY:
            Feedback_StartCue(FEEDBACK_CUE_FOOD_READY, Rtos_NowMs());
            break;

        case ACT_LIGHT_SET:                         /* unused: light commands use consumeLight() */
        case ACT_DISPENSE:                          /* motor handled by Member 1 only */
        default:
            break;
        }
    }
}