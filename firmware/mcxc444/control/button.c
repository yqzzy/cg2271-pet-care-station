/*
 * File: button.c
 * Owner: Member 3
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: Pet button interrupt, debounce, and split into the local cue
 *                 (beep) and the cloud notification.
 *
 * Flow:  falling edge -> ISR (queue a timestamp, nothing else)
 *        -> Button_Task (debounce) -> actuatorQueue: ACT_BEEP_ACK
 *                                  -> telemetryQueue: TEL_BUTTON_PRESSED
 *
 * TODO(pins): docs/hardware.md has no button pin yet. The defaults below use the
 * on-board SW2 (PTC3, active low) so it can be tested with no wiring. When the real
 * button pin is agreed, change ONLY the block of #defines below. The button must be on
 * Port A, C or D (the only ports with GPIO interrupts).
 */
#include "button.h"
#include "../app/rtos_types.h"

#include "fsl_device_registers.h"   /* TODO: confirm header name in the course project */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---- Pin configuration: change these when the pin worksheet is filled in ---- */
#define BUTTON_PORT         PORTC                    /* SW2 is PTC3                       */
#define BUTTON_GPIO         GPIOC
#define BUTTON_PIN          3u
#define BUTTON_PORT_CLOCK   SIM_SCGC5_PORTC_MASK
#define BUTTON_IRQn         PORTC_PORTD_IRQn         /* Port A: PORTA_IRQn                 */
#define BUTTON_ISR          PORTC_PORTD_IRQHandler   /* Port A: PORTA_IRQHandler           */

/* NVIC priority. The lecture slides pass 0/64/128/192; the standard CMSIS call expects 0-3.
 * TODO: confirm in the SDK's core_cm0plus.h which one applies, then set this value. */
#define BUTTON_IRQ_PRIO     2u

#define BUTTON_DEBOUNCE_MS  30u

/* Presses lost because buttonQueue was full (check in the debugger) */
static volatile uint32_t droppedPresses = 0;

void Button_Init(void) {
    NVIC_DisableIRQ(BUTTON_IRQn);

    /* Clock gating to the button's port */
    SIM->SCGC5 |= BUTTON_PORT_CLOCK;

    /* GPIO function (ALT1) */
    BUTTON_PORT->PCR[BUTTON_PIN] &= ~PORT_PCR_MUX_MASK;
    BUTTON_PORT->PCR[BUTTON_PIN] |= PORT_PCR_MUX(1);

    /* Internal pull-up: PS = 1 (pull-up), PE = 1 (enable) */
    BUTTON_PORT->PCR[BUTTON_PIN] &= ~PORT_PCR_PS_MASK;
    BUTTON_PORT->PCR[BUTTON_PIN] |= PORT_PCR_PS(1);
    BUTTON_PORT->PCR[BUTTON_PIN] &= ~PORT_PCR_PE_MASK;
    BUTTON_PORT->PCR[BUTTON_PIN] |= PORT_PCR_PE(1);

    /* Input */
    BUTTON_GPIO->PDDR &= ~(1u << BUTTON_PIN);

    /* Interrupt on falling edge (0b1010): the button is active low */
    BUTTON_PORT->PCR[BUTTON_PIN] &= ~PORT_PCR_IRQC_MASK;
    BUTTON_PORT->PCR[BUTTON_PIN] |= PORT_PCR_IRQC(0b1010);

    /* Clear any stale flag, then set priority, clear pending and enable */
    BUTTON_PORT->ISFR = (1u << BUTTON_PIN);
    NVIC_SetPriority(BUTTON_IRQn, BUTTON_IRQ_PRIO);
    NVIC_ClearPendingIRQ(BUTTON_IRQn);
    NVIC_EnableIRQ(BUTTON_IRQn);
}

/* ISR: do the minimum. Timestamp the press and hand it to the task.
 * NOTE: Port C and D share one vector. If anyone else uses a Port C/D interrupt,
 * their flag must also be handled here (check ISFR for their pin) or merged. */
void BUTTON_ISR(void) {
    BaseType_t higherPriorityWoken = pdFALSE;

    NVIC_ClearPendingIRQ(BUTTON_IRQn);

    if (BUTTON_PORT->ISFR & (1u << BUTTON_PIN)) {
        ButtonEvent ev = { xTaskGetTickCountFromISR() };
        if (xQueueSendFromISR(buttonQueue, &ev, &higherPriorityWoken) != pdTRUE) {
            droppedPresses++;
        }
        BUTTON_PORT->ISFR = (1u << BUTTON_PIN);   /* write 1 to clear */
    }

    portYIELD_FROM_ISR(higherPriorityWoken);
}

void Button_Task(void *argument) {
    (void)argument;

    ButtonEvent ev;
    TickType_t  lastAccepted = 0;
    bool        haveAccepted = false;

    for (;;) {
        if (xQueueReceive(buttonQueue, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Ignore bounces that arrive shortly after a press we already accepted */
        if (haveAccepted &&
            (ev.tick - lastAccepted) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
            continue;
        }

        /* Let the contact settle, then make sure it is still pressed (active low) */
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        if (BUTTON_GPIO->PDIR & (1u << BUTTON_PIN)) {
            continue;   /* already released: treat as a glitch */
        }

        lastAccepted = ev.tick;
        haveAccepted = true;

        /* Local cue and cloud notification are queued separately (timeout 0: never
         * stall this task if a consumer is slow) */
        ActuatorCmd cmd = { .type = ACT_BEEP_ACK, .value = 0, .request_id = 0 };
        (void)xQueueSend(actuatorQueue, &cmd, 0);

        TelemetryMsg msg = { .type = TEL_BUTTON_PRESSED, .tick = ev.tick, .value = 0, .request_id = 0 };
        (void)xQueueSend(telemetryQueue, &msg, 0);
    }
}