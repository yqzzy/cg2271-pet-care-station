/*
 * File: uart_link.c
 * Provisional owner: Member 4 (first version written by Member 3 as a stand-in; Member 4 to review)
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: Application adapter between the RTOS app and mcxc_uart (Member 4).
 *
 * What this task does:
 *   1. TEL_BUTTON_PRESSED from telemetryQueue -> sendButton(). The module allows only ONE
 *      outstanding button event, so extra presses are counted and handed over later.
 *   2. Once a second: copy the shared-state snapshot (mutex released before sending), then
 *      sendSensors(water_raw, food_milligrams). Invalid readings are skipped (the packet has no
 *      validity fields).
 *   3. Other telemetry types are dropped for now: the UART protocol has no wire message for
 *      light results (the module ACKs consumeLight() itself) or dispense results.
 *
 * Light commands are NOT handled here: the ACTUATOR task polls consumeLight() (see
 * test_tasks.c, and Member 1's Actuator_Task later).
 */
#include "uart_link.h"
#include "mcxc_uart.h"
#include "../app/rtos_types.h"
#include "../app/shared_state.h"

#include "fsl_debug_console.h"   /* PRINTF */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Longest wait on telemetryQueue. Bounds how late a pending button retry or the sensor
 * report can be. */
#define UART_LINK_POLL_MS         50u
#define SENSOR_REPORT_PERIOD_MS   1000u
#define PENDING_BUTTON_MAX        8u

/* !!! TEMPORARY !!!  1 = when no valid sensor data exists yet (Member 2 has not pushed), send
 * the FIXED TEST VALUES water = 600 raw, food = 125000 mg (125 g), the same ones Member 4's
 * mcxc_test_main.c uses, so the ESP32 side can be tested. Set to 0 as soon as the real
 * sensor tasks update the shared state, so test data is never mistaken for a real reading. */
#define UARTLINK_USE_TEST_SENSOR_DATA  1

bool UartLink_Init(void) {
    bool ok = uartInit();   /* creates the module's queues, mutex and TX/RX tasks */
#if UARTLINK_USE_TEST_SENSOR_DATA
    PRINTF("[UartLink] WARNING: sending FIXED TEST sensor values until real data is valid\r\n");
#endif
    return ok;
}

static void reportSensors(void) {
    SystemSnapshot snap;
    uint16_t water = 0;
    int32_t  food  = 0;
    bool     have  = false;

    /* The mutex is held only inside CopySnapshot, so it is released before any UART call */
    if (SharedState_CopySnapshot(&snap) && snap.water.valid && snap.food.valid) {
        water = snap.water.raw;
        food  = snap.food.value;     /* already in milligrams */
        have  = true;
    }
#if UARTLINK_USE_TEST_SENSOR_DATA
    if (!have) {
        water = 600;
        food  = 125000;
        have  = true;
    }
#endif

    if (have) {
        /* false = TX queue full: just try again with a fresh sample next period */
        (void)sendSensors(water, food);
    }
}

void UartLink_Task(void *argument) {
    (void)argument;

    TelemetryMsg msg;
    uint8_t      pendingButtons = 0;
    TickType_t   lastSensor     = xTaskGetTickCount();

    for (;;) {
        /* 1. Bounded wait for the next event, so the checks below always run */
        if (xQueueReceive(telemetryQueue, &msg, pdMS_TO_TICKS(UART_LINK_POLL_MS)) == pdTRUE) {
            switch (msg.type) {
            case TEL_BUTTON_PRESSED:
                if (pendingButtons < PENDING_BUTTON_MAX) {
                    pendingButtons++;
                }
                break;

            case TEL_LIGHT_APPLIED:      /* module ACKs consumeLight() itself */
            case TEL_DISPENSE_RESULT:    /* no wire message defined yet       */
            default:
                break;
            }
        }

        /* 2. Hand a pending press to the module. false = a previous event is still waiting
         *    for its ACK (or the queue is full): keep it and retry on the next loop. */
        if (pendingButtons > 0u && sendButton()) {
            pendingButtons--;
        }

        /* 3. Periodic sensor report */
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - lastSensor) >= pdMS_TO_TICKS(SENSOR_REPORT_PERIOD_MS)) {
            lastSensor = now;
            reportSensors();
        }
    }
}