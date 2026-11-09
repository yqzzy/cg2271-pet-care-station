#ifndef PET_TEST_FAKE_RUNTIME_H
#define PET_TEST_FAKE_RUNTIME_H

#include <cstddef>
#include <cstdint>

namespace fake {
struct StopTask {};
struct BlockingWait {};
extern uint32_t now;
extern unsigned mutexDepth;
extern unsigned ioWhileLocked;
extern unsigned queueReceivesLeft;
}

#endif
