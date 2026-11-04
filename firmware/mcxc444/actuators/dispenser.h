/*
 * File: dispenser.h
 * Owner: Member 1
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: SG90 servo food gate: one bounded, non-blocking dispense cycle.
 * Dependencies/consumers: Actuator_Task ONLY (never an ISR, never another task).
 *
 * One cycle: open the gate, hold it open for a fixed time, close it, wait for the servo to
 * reach the closed position, then report COMPLETED. The cycle length is fixed, so the
 * maximum run time is enforced here even if the UART, cloud or food sensing stop.
 *
 * No sensor policy or cloud calls here: food sensing decides WHEN to dispense.
 * COMPLETED means the gate cycle finished, not that food reached the bowl (no feedback).
 */
#ifndef DISPENSER_H
#define DISPENSER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DISPENSE_IDLE = 0,        /* no cycle run since start-up                          */
    DISPENSE_BUSY,            /* gate opening, open or closing                        */
    DISPENSE_COMPLETED,       /* last cycle finished: gate opened and closed          */
    DISPENSE_FAILED           /* last cycle aborted by Dispenser_Stop()               */
} DispenseStatus;

typedef enum {
    DISPENSE_STARTED = 0,
    DISPENSE_REJECTED_BUSY,      /* a cycle is running; it is not restarted or extended  */
    DISPENSE_REJECTED_NOT_READY  /* Dispenser_Init() failed or was not called            */
} DispenseStartResult;

/* Call once before the scheduler starts and AFTER Feedback_Init(), which selects the shared
 * 8 MHz TPM clock. Moves the gate to CLOSED. Returns false if the TPM clock is not set up. */
bool Dispenser_Init(void);

/* Starts one cycle, unless busy or not initialised. A rejected request changes nothing.
 * How often to dispense (minimum interval, rearm) is food sensing's policy, not checked here. */
DispenseStartResult Dispenser_Start(uint32_t now_ms);

/* Advances the cycle. The actuator task calls this every time it wakes. */
void Dispenser_Update(uint32_t now_ms);

/* Closes the gate at once. A running cycle ends as DISPENSE_FAILED. */
void Dispenser_Stop(void);

DispenseStatus Dispenser_GetStatus(void);

/* Same pattern as Feedback_GetNextDeadlineMs(): time of the next step of a running cycle,
 * so the actuator task can wake for it. False if no cycle is running. */
bool Dispenser_GetNextDeadlineMs(uint32_t *deadline_ms);

#endif /* DISPENSER_H */
