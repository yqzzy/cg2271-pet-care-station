#ifndef PET_TEST_QUEUE_H
#define PET_TEST_QUEUE_H

#include "FreeRTOS.h"
#include <cstring>
#include <deque>
#include <vector>

struct FakeQueue {
    size_t capacity;
    size_t itemSize;
    std::deque<std::vector<uint8_t> > items;
};
typedef FakeQueue *QueueHandle_t;

inline QueueHandle_t xQueueCreate(size_t capacity, size_t itemSize) {
    FakeQueue *queue = new FakeQueue;
    queue->capacity = capacity;
    queue->itemSize = itemSize;
    return queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    if (wait) throw fake::BlockingWait();
    if (queue->items.size() == queue->capacity) return pdFALSE;
    const uint8_t *bytes = static_cast<const uint8_t *>(item);
    queue->items.push_back(std::vector<uint8_t>(bytes, bytes + queue->itemSize));
    return pdPASS;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    if (!fake::queueReceivesLeft) throw fake::StopTask();
    --fake::queueReceivesLeft;
    if (queue->items.empty()) {
        fake::now += wait;
        return pdFALSE;
    }
    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    return pdPASS;
}

#endif
