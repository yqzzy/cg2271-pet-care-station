#ifndef PET_TEST_TASK_H
#define PET_TEST_TASK_H

#include "FreeRTOS.h"

inline BaseType_t xTaskCreate(void (*)(void *), const char *, unsigned,
                             void *, unsigned, void *) { return pdPASS; }
// Run one RX iteration, then hand control back to the test without real threads.
inline void vTaskDelay(TickType_t wait) {
    fake::now += wait;
    throw fake::StopTask();
}

#endif
