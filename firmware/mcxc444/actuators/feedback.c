/*
 * File: feedback.c
 * Owner: Member 3
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: Passive buzzer (TPM PWM tone) and station light (GPIO).
 *
 * Pins and timer are PLACEHOLDERS until docs/hardware.md is filled in. Change only the
 * #define blocks below.
 *
 * Buzzer design: the TPM runs continuously. Duty 0 = silent, duty 50% = tone. Feedback_StartCue()
 * starts the tone and stores its end time; the actuator task calls Feedback_Update(now_ms) to
 * stop it. No blocking, no RTOS timer, no ISR use.
 *
 * Resource claims to agree with the team:
 *  - Buzzer uses a whole TPM (all channels of one TPM share MOD, i.e. the frequency).
 *    Default is TPM1 (channel 1 on PTE21, ALT3, from LL-5). Member 1 should NOT put a
 *    servo/motor PWM on TPM1. PTE21 is also ADC0_SE4a, so Member 2 should avoid it.
 *  - The TPM clock source (SIM_SOPT2) is global; every TPM user must select MCGIRCLK 8 MHz.
 */
#include "feedback.h"

#include "fsl_device_registers.h"   /* TODO: confirm header name in the course project */
#include "FreeRTOS.h"

/* ---- Buzzer configuration (TODO: confirm pin / timer with the team) ---- */
#define BUZZER_TPM          TPM1
#define BUZZER_TPM_CLOCK    SIM_SCGC6_TPM1_MASK
#define BUZZER_CH           1u
#define BUZZER_PORT         PORTE
#define BUZZER_PORT_CLOCK   SIM_SCGC5_PORTE_MASK
#define BUZZER_PIN          21u
#define BUZZER_PIN_ALT      3u            /* PTE21 = TPM1_CH1 on ALT3 */
#define BUZZER_TONE_HZ      2000u         /* TODO: check the buzzer's rated frequency */
#define BEEP_ACK_MS         80u
#define BEEP_FOOD_READY_MS  400u

/* TPM count rate with prescaler 8 on the 8 MHz clock = 1 MHz.
 * Centre-aligned PWM counts up AND down, so frequency = 1 MHz / (2 * MOD). */
#define BUZZER_MOD          (1000000u / (2u * BUZZER_TONE_HZ))

/* ---- Station light configuration (TODO: external LED pin; default = on-board red LED) ---- */
#define LIGHT_PORT          PORTE
#define LIGHT_GPIO          GPIOE
#define LIGHT_PORT_CLOCK    SIM_SCGC5_PORTE_MASK
#define LIGHT_PIN           31u           /* PTE31 = red LED, active low */
#define LIGHT_ACTIVE_LOW    1

static bool     cueActive = false;
static uint32_t cueEndMs  = 0;

/* Same clock setup as the TPM lectures: MCGIRCLK = 8 MHz */
static void setMCGIRClk(void) {
    MCG->C1 &= ~MCG_C1_CLKS_MASK;
    MCG->C1 |= (MCG_C1_CLKS(0b01) | MCG_C1_IRCLKEN_MASK);   /* LIRC, enabled */

    MCG->C2 |= MCG_C2_IRCS_MASK;                            /* 8 MHz */

    MCG->SC &= ~MCG_SC_FCRDIV_MASK;
    MCG->SC |= MCG_SC_FCRDIV(0b0);                          /* divide by 1 */

    MCG->MC &= ~MCG_MC_LIRC_DIV2_MASK;
    MCG->MC |= MCG_MC_LIRC_DIV2(0b0);                       /* divide by 1 */
}

static void buzzerOff(void) {
    BUZZER_TPM->CONTROLS[BUZZER_CH].CnV = 0;                /* 0% duty = silent */
    cueActive = false;
}

static void buzzerOn(uint32_t now_ms, uint32_t duration_ms) {
    BUZZER_TPM->CONTROLS[BUZZER_CH].CnV = BUZZER_MOD / 2u;  /* 50% duty = tone */
    cueEndMs  = now_ms + duration_ms;
    cueActive = true;
}

static void buzzerInit(void) {
    setMCGIRClk();

    SIM->SCGC5 |= BUZZER_PORT_CLOCK;
    SIM->SCGC6 |= BUZZER_TPM_CLOCK;

    /* TPM clock source = MCGIRCLK (0b11) */
    SIM->SOPT2 &= ~SIM_SOPT2_TPMSRC_MASK;
    SIM->SOPT2 |= SIM_SOPT2_TPMSRC(0b11);

    /* Turn the TPM off, clear prescaler, set prescaler 8 (PS = 0b011), centre-aligned */
    BUZZER_TPM->SC &= ~(TPM_SC_CMOD_MASK | TPM_SC_PS_MASK);
    BUZZER_TPM->SC |= TPM_SC_PS(0b011);
    BUZZER_TPM->SC |= TPM_SC_CPWMS_MASK;
    BUZZER_TPM->CNT = 0;
    BUZZER_TPM->MOD = BUZZER_MOD;

    /* Channel: centre-aligned PWM, high-true (MS = 10, ELS = 10), duty 0 = silent */
    BUZZER_TPM->CONTROLS[BUZZER_CH].CnSC &= ~(TPM_CnSC_MSA_MASK | TPM_CnSC_ELSA_MASK);
    BUZZER_TPM->CONTROLS[BUZZER_CH].CnSC |= (TPM_CnSC_MSB(1) | TPM_CnSC_ELSB(1));
    BUZZER_TPM->CONTROLS[BUZZER_CH].CnV = 0;

    /* Pin to the TPM function */
    BUZZER_PORT->PCR[BUZZER_PIN] &= ~PORT_PCR_MUX_MASK;
    BUZZER_PORT->PCR[BUZZER_PIN] |= PORT_PCR_MUX(BUZZER_PIN_ALT);

    /* Start the counter (output stays low while duty = 0) */
    BUZZER_TPM->SC |= TPM_SC_CMOD(0b01);
}

static void lightInit(void) {
    SIM->SCGC5 |= LIGHT_PORT_CLOCK;

    LIGHT_PORT->PCR[LIGHT_PIN] &= ~PORT_PCR_MUX_MASK;
    LIGHT_PORT->PCR[LIGHT_PIN] |= PORT_PCR_MUX(1);          /* GPIO */

    (void)Feedback_SetLight(false);                         /* latch OFF first, so no flash */
    LIGHT_GPIO->PDDR |= (1u << LIGHT_PIN);                  /* then make it an output */
}

void Feedback_Init(void) {
    buzzerInit();
    lightInit();
}

void Feedback_StartCue(FeedbackCue cue, uint32_t now_ms) {
    switch (cue) {
    case FEEDBACK_CUE_ACK:        buzzerOn(now_ms, BEEP_ACK_MS);        break;
    case FEEDBACK_CUE_FOOD_READY: buzzerOn(now_ms, BEEP_FOOD_READY_MS); break;
    default:                      break;
    }
}

void Feedback_Update(uint32_t now_ms) {
    /* Wrap-safe: true once now_ms has reached cueEndMs */
    if (cueActive && (int32_t)(now_ms - cueEndMs) >= 0) {
        buzzerOff();
    }
}

bool Feedback_GetNextDeadlineMs(uint32_t *deadline_ms) {
    if (!cueActive) {
        return false;
    }
    *deadline_ms = cueEndMs;
    return true;
}

bool Feedback_LightIsOn(void) {
    bool pinHigh = (LIGHT_GPIO->PDOR & (1u << LIGHT_PIN)) != 0u;
#if LIGHT_ACTIVE_LOW
    return !pinHigh;
#else
    return pinHigh;
#endif
}

bool Feedback_SetLight(bool on) {
#if LIGHT_ACTIVE_LOW
    bool driveHigh = !on;
#else
    bool driveHigh = on;
#endif
    if (driveHigh) {
        LIGHT_GPIO->PSOR = (1u << LIGHT_PIN);
    } else {
        LIGHT_GPIO->PCOR = (1u << LIGHT_PIN);
    }
    return Feedback_LightIsOn();     /* applied = commanded output state (not measured light) */
}