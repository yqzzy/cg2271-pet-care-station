/*
 * File: shared_state.h
 * Owner: Member 3
 * Status: PROPOSED types, implemented in shared_state.c (not yet built or tested on hardware).
 * Responsibility: Mutex-protected latest readings and output snapshot.
 *
 * Rules: call from TASKS only, never from an ISR. Locks are held only while copying.
 * Units are decided by the producers (Member 2 for readings, Member 1 for outputs).
 */
#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"

typedef struct {
    int32_t    value;       /* food weight in MILLIGRAMS (matches sendSensors food_milligrams) */
    bool       valid;       /* false until the first good sample   */
    TickType_t tick;        /* when it was sampled                 */
} FoodReading;

typedef struct {
    uint16_t   raw;         /* raw ADC value (matches sendSensors water_level) */
    bool       low;         /* true when below the low-water threshold */
    bool       valid;
    TickType_t tick;
} WaterReading;

typedef struct {
    bool light_on;          /* applied light state                 */
    bool motor_running;     /* dispenser motor currently moving    */
} OutputState;

typedef struct {
    FoodReading  food;
    WaterReading water;
    OutputState  outputs;
} SystemSnapshot;

void SharedState_Init(void);
void SharedState_UpdateFood(const FoodReading *reading);
void SharedState_UpdateWater(const WaterReading *reading);
void SharedState_UpdateOutputs(const OutputState *state);
bool SharedState_CopySnapshot(SystemSnapshot *out);   /* false if the lock timed out */

#endif /* SHARED_STATE_H */