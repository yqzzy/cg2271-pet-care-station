/*
 * File: dispenser.c
 * Owner: Member 1
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: SG90 servo food gate on TPM0 (50 Hz PWM). See dispenser.h.
 *
 * Pin, angles and times are PLACEHOLDERS until the mechanism is built and docs/hardware.md is
 * filled in. Change only the #define blocks below.
 *
 * Resource claims (agree with the team):
 *  - TPM0 belongs to the servo (all channels of one TPM share MOD, i.e. the 50 Hz period).
 *    TPM1 belongs to the buzzer (feedback.c). TPM2 is still free.
 *  - The TPM clock source (SIM_SOPT2) is global. feedback.c selects MCGIRCLK = 8 MHz; this
 *    file relies on that and only checks it, so Feedback_Init() must run first.
 *
 * Wiring: SG90 brown = GND, red = +5 V from a SEPARATE supply (not the board 3.3 V pin; the
 * servo can draw several hundred mA), orange = signal pin below. Join the supply ground to the
 * board ground.
 */
#include "dispenser.h"

#include "fsl_device_registers.h"   /* TODO: confirm header name in the course project */

/* ---- Servo pin and timer (TODO: confirm the pin is free on the FRDM-MCXC444 header) ---- */
#define SERVO_TPM           TPM0
#define SERVO_TPM_CLOCK     SIM_SCGC6_TPM0_MASK
#define SERVO_CH            2u
#define SERVO_PORT          PORTD
#define SERVO_PORT_CLOCK    SIM_SCGC5_PORTD_MASK
#define SERVO_PIN           2u
#define SERVO_PIN_ALT       4u            /* PTD2 = TPM0_CH2 on ALT4 */

/* ---- Gate positions and timing (TODO: calibrate on the built mechanism) ----
 * SG90: about 0.5 ms to 2.4 ms pulse for its full ~180 degrees; 1.5 ms is the centre. */
#define SERVO_CLOSED_US     1000u         /* pulse width with the gate closed               */
#define SERVO_OPEN_US       2000u         /* pulse width with the gate open                 */
#define DISPENSE_OPEN_MS    500u          /* time held open = portion size                  */
#define SERVO_MOVE_MS       300u          /* time for the servo to swing open -> closed     */

/* TPM count rate with prescaler 64 on the 8 MHz clock = 125 kHz, i.e. 8 us per count.
 * Edge-aligned PWM: period = (MOD + 1) counts = 2500 * 8 us = 20 ms (50 Hz). */
#define SERVO_US_PER_COUNT  8u
#define SERVO_MOD           (20000u / SERVO_US_PER_COUNT - 1u)
#define SERVO_COUNTS(us)    ((us) / SERVO_US_PER_COUNT)

#define TPMSRC_MCGIRCLK     0b11u

typedef enum {
    PHASE_IDLE,               /* gate closed, no cycle running        */
    PHASE_OPEN,               /* gate commanded open, holding         */
    PHASE_CLOSING             /* gate commanded closed, still moving  */
} Phase;

static bool           initialised = false;
static Phase          phase       = PHASE_IDLE;
static uint32_t       phaseEndMs  = 0;
static DispenseStatus status      = DISPENSE_IDLE;

static void servoSetPulse(uint32_t pulse_us) {
    SERVO_TPM->CONTROLS[SERVO_CH].CnV = SERVO_COUNTS(pulse_us);
}

bool Dispenser_Init(void) {
    /* Feedback_Init() must have selected the 8 MHz MCGIRCLK for all TPMs */
    if ((SIM->SOPT2 & SIM_SOPT2_TPMSRC_MASK) != SIM_SOPT2_TPMSRC(TPMSRC_MCGIRCLK)) {
        return false;
    }

    SIM->SCGC5 |= SERVO_PORT_CLOCK;
    SIM->SCGC6 |= SERVO_TPM_CLOCK;

    /* Turn the TPM off, clear prescaler, set prescaler 64 (PS = 0b110), edge-aligned */
    SERVO_TPM->SC &= ~(TPM_SC_CMOD_MASK | TPM_SC_PS_MASK | TPM_SC_CPWMS_MASK);
    SERVO_TPM->SC |= TPM_SC_PS(0b110);
    SERVO_TPM->CNT = 0;
    SERVO_TPM->MOD = SERVO_MOD;

    /* Channel: edge-aligned PWM, high-true (MS = 10, ELS = 10), start at the closed position */
    SERVO_TPM->CONTROLS[SERVO_CH].CnSC &= ~(TPM_CnSC_MSA_MASK | TPM_CnSC_ELSA_MASK);
    SERVO_TPM->CONTROLS[SERVO_CH].CnSC |= (TPM_CnSC_MSB(1) | TPM_CnSC_ELSB(1));
    servoSetPulse(SERVO_CLOSED_US);

    /* Pin to the TPM function */
    SERVO_PORT->PCR[SERVO_PIN] &= ~PORT_PCR_MUX_MASK;
    SERVO_PORT->PCR[SERVO_PIN] |= PORT_PCR_MUX(SERVO_PIN_ALT);

    /* Start the counter: the servo moves to CLOSED and holds it */
    SERVO_TPM->SC |= TPM_SC_CMOD(0b01);

    phase       = PHASE_IDLE;
    status      = DISPENSE_IDLE;
    initialised = true;
    return true;
}

DispenseStartResult Dispenser_Start(uint32_t now_ms) {
    if (!initialised) {
        return DISPENSE_REJECTED_NOT_READY;
    }
    if (phase != PHASE_IDLE) {
        return DISPENSE_REJECTED_BUSY;      /* never restart or extend the cycle */
    }
    servoSetPulse(SERVO_OPEN_US);
    phase      = PHASE_OPEN;
    phaseEndMs = now_ms + DISPENSE_OPEN_MS;
    status     = DISPENSE_BUSY;
    return DISPENSE_STARTED;
}

void Dispenser_Update(uint32_t now_ms) {
    /* Wrap-safe: true once now_ms has reached phaseEndMs */
    if (phase == PHASE_IDLE || (int32_t)(now_ms - phaseEndMs) < 0) {
        return;
    }

    if (phase == PHASE_OPEN) {
        servoSetPulse(SERVO_CLOSED_US);
        phase      = PHASE_CLOSING;
        phaseEndMs = now_ms + SERVO_MOVE_MS;
    } else {                                /* PHASE_CLOSING: gate is back at CLOSED */
        phase  = PHASE_IDLE;
        status = DISPENSE_COMPLETED;
    }
}

void Dispenser_Stop(void) {
    if (!initialised) {
        return;
    }
    servoSetPulse(SERVO_CLOSED_US);
    if (phase != PHASE_IDLE) {
        status = DISPENSE_FAILED;
    }
    phase = PHASE_IDLE;
}

DispenseStatus Dispenser_GetStatus(void) {
    return status;
}

bool Dispenser_GetNextDeadlineMs(uint32_t *deadline_ms) {
    if (phase == PHASE_IDLE) {
        return false;
    }
    *deadline_ms = phaseEndMs;
    return true;
}
