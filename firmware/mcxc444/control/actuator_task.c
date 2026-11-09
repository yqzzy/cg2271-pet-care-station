/*
 * File: actuator_task.c
 * Owner: Member 1
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: The ONLY task that drives the outputs: servo gate (dispenser.c), buzzer and
 *                 light (Member 3's feedback.c).
 *
 * Inputs:
 *   - actuatorQueue: ACT_BEEP_ACK (Button_Task), ACT_DISPENSE (food sensing, value 1 = one
 *     portion), ACT_BEEP_FOOD_READY, ACT_LIGHT_SET (unused for now, handled for later).
 *   - consumeLight() (Member 4's UART module): remote light commands, polled at least every
 *     LIGHT_POLL_MS. The module sends the ACK itself ("consumed", not "lit").
 * Outputs:
 *   - dispenseDoneSem: given when a gate cycle ends or a dispense request is rejected, so the
 *     food sensing task knows when to re-check the bowl.
 *   - telemetryQueue: TEL_DISPENSE_RESULT when a cycle ends (value = DispenseStatus) and
 *     TEL_LIGHT_APPLIED for ACT_LIGHT_SET. uart_link.c currently drops TEL_DISPENSE_RESULT:
 *     the protocol has no dispense-result message yet.
 *
 * Timing: waits for the next command, but never past the light poll period, the end of a beep
 * or the next servo step, so deadlines are met even while commands keep arriving.
 */
#include "actuator_task.h"
#include "../app/rtos_types.h"
#include "../actuators/dispenser.h"
#include "../actuators/feedback.h"
#include "../communication/mcxc_uart.h"

#include "fsl_debug_console.h"   /* PRINTF */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Light commands are polled, so never wait longer than this */
#define LIGHT_POLL_MS        20u

/* ACT_DISPENSE value for one portion (agreed with food sensing) */
#define DISPENSE_ONE_PORTION 1u

/* Shortens *wait so the task wakes by deadline_ms */
static void limitWait(TickType_t *wait, uint32_t now_ms, uint32_t deadline_ms) {
    int32_t remaining = (int32_t)(deadline_ms - now_ms);
    TickType_t ticks = (remaining > 0) ? pdMS_TO_TICKS((uint32_t)remaining) : 0;
    if (ticks < *wait) {
        *wait = ticks;
    }
}

/* Never blocks: a full telemetry queue only loses the report */
static void sendTelemetry(TelemetryType type, int32_t value, uint32_t request_id) {
    TelemetryMsg msg = {
        .type       = type,
        .tick       = xTaskGetTickCount(),
        .value      = value,
        .request_id = request_id
    };
    if (xQueueSend(telemetryQueue, &msg, 0) != pdPASS) {
        PRINTF("[actuator] telemetry queue full, report %d dropped\r\n", (int)type);
    }
}

static void handleCommand(const ActuatorCmd *cmd, uint32_t now) {
    switch (cmd->type) {
    case ACT_BEEP_ACK:
        Feedback_StartCue(FEEDBACK_CUE_ACK, now);
        break;

    case ACT_BEEP_FOOD_READY:
        Feedback_StartCue(FEEDBACK_CUE_FOOD_READY, now);
        break;

    case ACT_DISPENSE:
        if (cmd->value != DISPENSE_ONE_PORTION) {
            PRINTF("[actuator] dispense value %u unsupported, ignored\r\n", (unsigned)cmd->value);
            break;
        }
        /* Busy only happens with an extra request (e.g. a bench test): food sensing waits for
         * dispenseDoneSem before it asks for the next portion. */
        switch (Dispenser_Start(now)) {
        case DISPENSE_STARTED:
            PRINTF("[actuator] dispense started\r\n");
            break;
        case DISPENSE_REJECTED_BUSY:
            PRINTF("[actuator] dispense rejected: busy\r\n");
            (void)xSemaphoreGive(dispenseDoneSem);   /* never leave the requester waiting */
            break;
        default:
            PRINTF("[actuator] dispense rejected: not ready\r\n");
            (void)xSemaphoreGive(dispenseDoneSem);
            break;
        }
        break;

    case ACT_LIGHT_SET: {
        bool applied = Feedback_SetLight(cmd->value != 0u);
        sendTelemetry(TEL_LIGHT_APPLIED, applied ? 1 : 0, cmd->request_id);
        break;
    }

    default:
        break;
    }
}

void Actuator_Task(void *argument) {
    (void)argument;

    ActuatorCmd cmd;
    bool wasBusy = false;

    for (;;) {
        uint32_t now = Rtos_NowMs();
        Feedback_Update(now);                       /* stops an expired beep    */
        Dispenser_Update(now);                      /* advances the gate cycle  */

        /* Report each finished cycle once. Food-ready cue only for a completed cycle. */
        DispenseStatus status = Dispenser_GetStatus();
        if (wasBusy && status != DISPENSE_BUSY) {
            sendTelemetry(TEL_DISPENSE_RESULT, (int32_t)status, 0u);
            if (status == DISPENSE_COMPLETED) {
                Feedback_StartCue(FEEDBACK_CUE_FOOD_READY, now);
            }
            PRINTF("[actuator] dispense %s\r\n",
                   (status == DISPENSE_COMPLETED) ? "completed" : "failed");
            /* Gate closed: food sensing can now let the food settle and re-check the bowl */
            (void)xSemaphoreGive(dispenseDoneSem);
        }
        wasBusy = (status == DISPENSE_BUSY);

        /* Remote light command: apply at once */
        bool on;
        if (consumeLight(&on)) {
            (void)Feedback_SetLight(on);
        }

        /* Wait for the next command, but no longer than the light poll period, the end of
         * the current beep or the next servo step, whichever is first */
        TickType_t wait = pdMS_TO_TICKS(LIGHT_POLL_MS);
        uint32_t deadline;
        if (Feedback_GetNextDeadlineMs(&deadline)) {
            limitWait(&wait, now, deadline);
        }
        if (Dispenser_GetNextDeadlineMs(&deadline)) {
            limitWait(&wait, now, deadline);
        }

        if (xQueueReceive(actuatorQueue, &cmd, wait) == pdTRUE) {
            handleCommand(&cmd, Rtos_NowMs());
        }
    }
}
