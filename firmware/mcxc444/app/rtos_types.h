/*
 * File: rtos_types.h
 * Owner: Member 3
 * Status: PROPOSED shared interface (matches docs/system-design.md). Agree with Member 1
 *         (actuator), Member 2 (sensing) and Member 4 (UART) before merging, then record the
 *         message types in docs/system-design.md.
 *
 * Who produces / consumes each queue:
 *   buttonQueue    ISR (button.c)                              -> Button_Task (Member 3)
 *   actuatorQueue  Button_Task (beep), UartLink_Task (remote light, with request ID),
 *                  food logic (Member 2, DISPENSE_REQUEST)    -> Actuator_Task (Member 1)
 *   telemetryQueue Button_Task, Actuator_Task (results)        -> UartLink_Task (Member 4)
 *
 * Only Actuator_Task calls the Feedback_* output functions. Everyone else sends an ActuatorCmd.
 * Sensor readings do NOT use these queues: they go through the latest-state buffer
 * (shared_state.h).
 */
#ifndef RTOS_TYPES_H
#define RTOS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Milliseconds since scheduler start. Wraps after ~49 days: compare with
 * (int32_t)(now - deadline) >= 0, never with plain < or >. */
static inline uint32_t Rtos_NowMs(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ---- Button ISR -> Button_Task ---- */
typedef struct {
    TickType_t tick;            /* tick count when the interrupt fired */
} ButtonEvent;

/* ---- Anything -> Actuator_Task ---- */
typedef enum {
    ACT_BEEP_ACK,               /* acknowledgement cue (Feedback cue ACK)            */
    ACT_BEEP_FOOD_READY,        /* food-ready cue (Feedback cue FOOD_READY)          */
    ACT_LIGHT_SET,              /* value: 1 = on, 0 = off. request_id from the cloud  */
    ACT_DISPENSE                /* DISPENSE_REQUEST; value: units decided by Member 1 */
} ActuatorCmdType;

typedef struct {
    ActuatorCmdType type;
    uint32_t        value;
    uint32_t        request_id; /* echoed back in the result; 0 if not applicable     */
} ActuatorCmd;

/* ---- Anything -> UartLink_Task (forwarded to the ESP32 / cloud) ---- */
typedef enum {
    TEL_BUTTON_PRESSED,         /* confirmed (debounced) pet button press            */
    TEL_LIGHT_APPLIED,          /* value: applied light state (0/1); echoes request_id.
                                   "Applied" = commanded output state, not measured light */
    TEL_DISPENSE_RESULT         /* value: dispenser status code (Member 1: completed/failed).
                                   A finished motor cycle does not prove food was delivered. */
} TelemetryType;

typedef struct {
    TelemetryType type;
    TickType_t    tick;
    int32_t       value;
    uint32_t      request_id;
} TelemetryMsg;

/* ---- Queue handles (created in rtos_setup.c) ---- */
extern QueueHandle_t buttonQueue;
extern QueueHandle_t actuatorQueue;
extern QueueHandle_t telemetryQueue;

/* ---- Queue depths (proposed, tune after testing) ---- */
#define BUTTON_QUEUE_LEN     8
#define ACTUATOR_QUEUE_LEN   8
#define TELEMETRY_QUEUE_LEN  8

#endif /* RTOS_TYPES_H */