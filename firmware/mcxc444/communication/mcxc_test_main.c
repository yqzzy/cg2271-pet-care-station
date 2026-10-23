/* OPTIONAL standalone test: use INSTEAD OF existing main, not in addition.
 * Active buzzer PTB0 from your original template stands in for the light.
 * Production: Member 3 owns GPIO application; UART module never touches it. */
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mcxc_uart.h"
#define BUZZER_PIN 0

static void initBuzzer(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
    PORTB->PCR[BUZZER_PIN] &= ~PORT_PCR_MUX_MASK;
    PORTB->PCR[BUZZER_PIN] |= PORT_PCR_MUX(1);
    GPIOB->PDDR |= (1u << BUZZER_PIN);
    GPIOB->PCOR = (1u << BUZZER_PIN);
}
static void buzzerOn(void) { GPIOB->PSOR = (1u << BUZZER_PIN); }
static void buzzerOff(void) { GPIOB->PCOR = (1u << BUZZER_PIN); }

static void testTask(void *unused)
{
    (void)unused;
    TickType_t lastSensor = xTaskGetTickCount();
    TickType_t lastButton = lastSensor;
    for (;;) {
        bool on;
        if (consumeLight(&on)) { // Consumption automatically queues the single ACK.
            if (on) buzzerOn(); else buzzerOff();
            PRINTF("TEST output=%u\r\n", (unsigned)on);
        }
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - lastSensor) >= pdMS_TO_TICKS(1000)) {
            lastSensor = now;
            (void)sendSensors(600, 125000); /* 125 grams; replace with shared-state snapshot */
        }
        if ((TickType_t)(now - lastButton) >= pdMS_TO_TICKS(10000)) {
            lastButton = now;
            PRINTF("Simulated button accepted=%u\r\n", (unsigned)sendButton());
        }
        TickType_t delay = pdMS_TO_TICKS(10);
        vTaskDelay(delay ? delay : 1);
    }
}
int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    BOARD_InitDebugConsole();
#endif
    initBuzzer();
    if (!uartInit()) { PRINTF("UART init failed\r\n"); for (;;) {} }
    if (xTaskCreate(testTask, "test", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL) != pdPASS) for (;;) {}
    vTaskStartScheduler();
    for (;;) {}
}
