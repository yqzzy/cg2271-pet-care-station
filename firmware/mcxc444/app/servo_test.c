/*
 * File: servo_test.c
 * Status: Bench tool. Built and run only when SERVO_TEST_ONLY is 1 in servo_test.h; it then
 *         replaces the whole application (bare metal, no FreeRTOS, no other devices).
 * Responsibility: Move the SG90 servo to any angle typed on the debug console, to check the
 *                 wiring and find the gate's closed and open positions.
 *
 * Wiring (same as the pet station): signal PTD2 (J2-4, D9), + 5 V (J3-10), - GND.
 * Terminal: MCU-Link VCom port (J13 USB), 115200 8N1. Type a command, then press Enter:
 *   <0-180>     move to that angle, e.g. 90
 *   us <n>      move to a raw pulse width, 500-2500 us
 *   sweep       0 -> 180 -> 0 degrees in 10 degree steps
 *   h           help
 */
#include "servo_test.h"

#if SERVO_TEST_ONLY

#include "fsl_device_registers.h"
#include "fsl_common.h"
#include "fsl_lpuart.h"
#include "fsl_debug_console.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Same timer and pin as dispenser.c: TPM0 channel 2 on PTD2 (ALT4) */
#define TEST_TPM            TPM0
#define TEST_CH             2u
#define TEST_PIN            2u
#define TEST_PIN_ALT        4u

/* 8 MHz MCGIRCLK / prescaler 64 = 125 kHz, 8 us per count; MOD 2499 = 20 ms (50 Hz) */
#define US_PER_COUNT        8u
#define PERIOD_US           20000u

/* SG90: about 0.5 ms = 0 degrees and 2.4 ms = 180 degrees */
#define MIN_US              500u
#define MAX_US              2400u

#define LINE_MAX            16u
#define SWEEP_STEP_DEG      10u
#define SWEEP_STEP_MS       300u

static void setPulseUs(uint32_t us) {
    TEST_TPM->CONTROLS[TEST_CH].CnV = us / US_PER_COUNT;
}

static uint32_t angleToUs(uint32_t deg) {
    return MIN_US + deg * (MAX_US - MIN_US) / 180u;
}

static void moveToAngle(uint32_t deg) {
    uint32_t us = angleToUs(deg);
    setPulseUs(us);
    PRINTF("  %u deg -> %u us\r\n", (unsigned)deg, (unsigned)us);
}

static void servoInit(void) {
    /* MCGIRCLK on at 8 MHz (fast IRC, no divider); the core clock is not changed */
    MCG->C1 |= MCG_C1_IRCLKEN_MASK;
    MCG->C2 |= MCG_C2_IRCS_MASK;
    MCG->SC &= ~MCG_SC_FCRDIV_MASK;
    MCG->MC &= ~MCG_MC_LIRC_DIV2_MASK;

    SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;
    SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK;
    SIM->SOPT2 = (SIM->SOPT2 & ~SIM_SOPT2_TPMSRC_MASK) | SIM_SOPT2_TPMSRC(0b11);

    /* Timer off, prescaler 64, edge-aligned, 20 ms period */
    TEST_TPM->SC = 0;
    TEST_TPM->CNT = 0;
    TEST_TPM->MOD = PERIOD_US / US_PER_COUNT - 1u;
    TEST_TPM->SC = TPM_SC_PS(0b110);

    /* Edge-aligned PWM, high-true pulses, start at 90 degrees */
    TEST_TPM->CONTROLS[TEST_CH].CnSC = TPM_CnSC_MSB(1) | TPM_CnSC_ELSB(1);
    setPulseUs(angleToUs(90u));

    PORTD->PCR[TEST_PIN] = PORT_PCR_MUX(TEST_PIN_ALT);
    TEST_TPM->SC |= TPM_SC_CMOD(0b01);   /* start counting */
}

static void printHelp(void) {
    PRINTF("\r\n=== Servo test (PTD2 / J2-4, TPM0 ch2, 50 Hz) ===\r\n"
           "  <0-180>   move to that angle, e.g. 90\r\n"
           "  us <n>    move to a pulse width, 500-2500 us\r\n"
           "  sweep     0 -> 180 -> 0 degrees in 10 degree steps\r\n"
           "  h         this help\r\n"
           "Started at 90 deg (%u us).\r\n\r\n", (unsigned)angleToUs(90u));
}

static void sweep(void) {
    for (uint32_t deg = 0u; deg <= 180u; deg += SWEEP_STEP_DEG) {
        moveToAngle(deg);
        SDK_DelayAtLeastUs(SWEEP_STEP_MS * 1000u, SystemCoreClock);
    }
    for (int32_t deg = 180 - (int32_t)SWEEP_STEP_DEG; deg >= 0; deg -= (int32_t)SWEEP_STEP_DEG) {
        moveToAngle((uint32_t)deg);
        SDK_DelayAtLeastUs(SWEEP_STEP_MS * 1000u, SystemCoreClock);
    }
}

static void runCommand(const char *line) {
    char *end;
    if (strcmp(line, "sweep") == 0) {
        sweep();
    } else if (strcmp(line, "h") == 0 || strcmp(line, "?") == 0) {
        printHelp();
    } else if (strncmp(line, "us ", 3) == 0) {
        unsigned long us = strtoul(line + 3, &end, 10);
        if (end == line + 3 || us < 500u || us > 2500u) {
            PRINTF("  usage: us <500-2500>\r\n");
            return;
        }
        setPulseUs((uint32_t)us);
        PRINTF("  %u us\r\n", (unsigned)us);
    } else {
        unsigned long deg = strtoul(line, &end, 10);
        if (end == line || *end != '\0' || deg > 180u) {
            PRINTF("  type an angle 0-180, us <n>, sweep or h\r\n");
            return;
        }
        moveToAngle((uint32_t)deg);
    }
}

void ServoTest_Run(void) {
    char     line[LINE_MAX];
    uint32_t len = 0u;

    servoInit();
    printHelp();
    PRINTF("> ");

    for (;;) {
        uint32_t flags = LPUART_GetStatusFlags(LPUART0);
        if ((flags & (uint32_t)kLPUART_RxOverrunFlag) != 0u) {
            (void)LPUART_ClearStatusFlags(LPUART0, (uint32_t)kLPUART_RxOverrunFlag);
        }
        if ((flags & (uint32_t)kLPUART_RxDataRegFullFlag) == 0u) {
            continue;
        }
        char ch = (char)LPUART_ReadByte(LPUART0);

        if (ch == '\r' || ch == '\n') {
            if (len == 0u) {
                continue;
            }
            line[len] = '\0';
            len = 0u;
            PRINTF("\r\n");
            runCommand(line);
            PRINTF("> ");
        } else if (ch == '\b' || ch == 0x7F) {
            if (len > 0u) {
                len--;
                PRINTF("\b \b");
            }
        } else if (ch >= ' ' && len < LINE_MAX - 1u) {
            line[len++] = ch;
            PUTCHAR(ch);
        }
    }
}

#endif /* SERVO_TEST_ONLY */
