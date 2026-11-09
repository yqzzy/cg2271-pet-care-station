#ifndef PET_TEST_FREERTOS_H
#define PET_TEST_FREERTOS_H

#include "fake_runtime.h"
#include <stdexcept>

typedef uint32_t TickType_t;
typedef int BaseType_t;
static const BaseType_t pdPASS = 1;
static const BaseType_t pdFALSE = 0;
static const TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

struct portMUX_TYPE { bool locked; };
#define portMUX_INITIALIZER_UNLOCKED {false}
inline unsigned &fakeCriticalDepth() { static unsigned depth = 0; return depth; }
inline void portENTER_CRITICAL(portMUX_TYPE *mux) {
    if (mux->locked) throw std::runtime_error("nested critical section");
    mux->locked = true;
    ++fakeCriticalDepth();
}
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) {
    mux->locked = false;
    --fakeCriticalDepth();
}

#endif
