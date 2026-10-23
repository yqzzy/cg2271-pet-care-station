#ifndef PET_ESP32_UART_H
#define PET_ESP32_UART_H
#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t water_level;
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
/* Clear pending button flag and queue its consumption ACK. */
bool consumeButton(uint32_t *sequence = nullptr);
/* Explicit desired ON/OFF. false = request pending, unread ACK, full queue or
 * IDs exhausted. Consume previous ACK before submitting next light request. */
bool sendLight(bool on);
/* true ONCE per matching light ACK; output = acknowledged REQUESTED state.
 * Clears ACK flag under mutex. Does not confirm GPIO or physical light state. */
bool consumeLightAck(bool *on);
/* Optional status getter for waiting/timeout display; does not consume ACK. */
UartResult getLightResult(void);
#endif
