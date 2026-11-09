#ifndef PET_TEST_SEMPHR_H
#define PET_TEST_SEMPHR_H

#include "FreeRTOS.h"

struct FakeMutex { bool locked; };
typedef FakeMutex *SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new FakeMutex{false}; }

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    if (fakeCriticalDepth()) throw std::runtime_error("driver operation under bridge critical section");
    if (mutex->locked) {
        if (wait) throw fake::BlockingWait();
        return pdFALSE;
    }
    mutex->locked = true;
    ++fake::mutexDepth;
    return pdPASS;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    mutex->locked = false;
    --fake::mutexDepth;
    return pdPASS;
}

#endif
