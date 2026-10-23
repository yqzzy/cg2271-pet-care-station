/*
 * File: shared_state.c
 * Owner: Member 3
 * Status: Written for review; not yet built or tested on hardware.
 */
#include <stddef.h>
#include "shared_state.h"
#include "FreeRTOS.h"
#include "semphr.h"

#define STATE_LOCK_TIMEOUT_MS 50

static SemaphoreHandle_t stateMutex;
static SystemSnapshot    state;

void SharedState_Init(void) {
    stateMutex = xSemaphoreCreateMutex();
    configASSERT(stateMutex != NULL);
    /* Zero everything: valid flags start as false */
    state = (SystemSnapshot){0};
}

static bool lock(void) {
    return xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_LOCK_TIMEOUT_MS)) == pdTRUE;
}

void SharedState_UpdateFood(const FoodReading *reading) {
    if (lock()) {
        state.food = *reading;
        xSemaphoreGive(stateMutex);
    }
}

void SharedState_UpdateWater(const WaterReading *reading) {
    if (lock()) {
        state.water = *reading;
        xSemaphoreGive(stateMutex);
    }
}

void SharedState_UpdateOutputs(const OutputState *outputs) {
    if (lock()) {
        state.outputs = *outputs;
        xSemaphoreGive(stateMutex);
    }
}

bool SharedState_CopySnapshot(SystemSnapshot *out) {
    if (!lock()) {
        return false;
    }
    *out = state;
    xSemaphoreGive(stateMutex);
    return true;
}