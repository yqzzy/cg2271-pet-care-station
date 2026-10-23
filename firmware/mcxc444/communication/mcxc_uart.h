#ifndef PET_MCXC_UART_H
#define PET_MCXC_UART_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UART_IDLE, UART_WAITING, UART_CONSUMED, UART_TIMED_OUT
} UartStatus;
typedef struct {
    uint32_t sequence;
    UartStatus status;
} UartResult;

/* Once before scheduler, after board/debug init. Stop on failure.
 * All other functions are TASK-context calls, never GPIO ISR calls. */
bool uartInit(void);
/* One packet with both readings. Signed food milligrams; no ACK/retry.
 * true = queued; false = full. Call outside shared-sensor-state mutex. */
bool sendSensors(uint16_t water_level, int32_t food_milligrams);
/* One outstanding button event; saved string/sequence + three retries.
 * true = queued; false = previous event pending, queue full or IDs exhausted. */
bool sendButton(void);
/* Check button ACK/timeout status; reads without clearing it. */
UartResult getButtonResult(void);
/* Copy desired ON/OFF and clear pending flag; queue ONE consumption ACK.
 * true once per command; false if none or null output pointer.
 * Caller then writes GPIO. ACK means consumed, not GPIO/physical confirmation. */
bool consumeLight(bool *on);
#endif
