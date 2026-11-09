#ifndef PET_ESP32_UART_H
#define PET_ESP32_UART_H
#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool water_enough; // true = enough water, false = low water.
    int32_t food_milligrams;
    uint32_t received_ms; // ESP32-local time for stale-data checking.
} SensorValues;
typedef enum {
    UART_IDLE, UART_WAITING, UART_CONSUMED, UART_TIMED_OUT
} UartStatus;
typedef struct {
    uint32_t sequence;
    UartStatus status;
} UartResult;

/* Once from setup(); begin debug Serial first; use DIFFERENT UART for link. */
bool uartInit(HardwareSerial &port, int rx_pin, int tx_pin);
/* Copy latest readings, not a consume operation. Check received_ms for freshness. */
bool getSensors(SensorValues *out);
/* Consume once its ACK is queued. false leaves event/output pending when the
 * TX queue is full or state mutex is busy; poll again without blocking. */
bool consumeButton(uint32_t *sequence = nullptr);
/* Explicit desired ON/OFF. false = request pending, unread ACK, full queue,
 * busy mutex or IDs exhausted. Consume previous ACK before the next request. */
bool sendLight(bool on);
/* true ONCE per matching light ACK; output = acknowledged REQUESTED state.
 * Clears ACK flag under mutex; a busy mutex returns false without blocking.
 * Does not confirm GPIO or physical light state. */
bool consumeLightAck(bool *on);
/* Optional status getter; does not consume ACK or block on a busy mutex.
 * {0, UART_IDLE} means unavailable this poll; retry without assuming timeout. */
UartResult getLightResult(void);
#endif
