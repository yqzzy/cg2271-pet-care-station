/*
 * File: food_calibration.c
 * Status: Lab tool. Built and started only when FOOD_CALIBRATION_MODE is 1 in
 *         food_calibration.h; it then replaces FoodSensor_Task (both would read the HX711).
 * Responsibility: Bench tool on the debug console: measure the load cell calibration for
 *                 food_sensor.c, and tune the dispenser gate (closed/open position, open
 *                 time) for dispenser.c without rebuilding.
 *
 * Terminal: the MCU-Link VCom port (J13 USB), 115200 8N1, e.g. PuTTY "Serial". The board
 * echoes what you type. Type a command, then press Enter:
 *   z          zero: empty bowl mounted, nothing in it
 *   c <grams>  calibrate: put a known weight in the bowl first (e.g. "c 200")
 *   m          measure once (averaged), shows grams once calibrated
 *   s          start/stop a live reading every 0.5 s
 *   gate <us>    hold the gate at a servo pulse width (500-2500 us) to find a position
 *   closed <us>  save the closed position     open <us>   save the open position (swings once)
 *   time <ms>    save how long the gate stays open (portion size)
 *   drop         one real dispense; weighs the bowl before and after
 *   p          print the #defines to paste into food_sensor.c and dispenser.c
 *   h          help
 * Keep still while a measurement runs (about 1.6 s); keys typed meanwhile may be lost.
 */
#include "food_calibration.h"

#if FOOD_CALIBRATION_MODE

#include "food_sensor.h"
#include "../actuators/dispenser.h"
#include "../app/rtos_types.h"

#include "fsl_lpuart.h"
#include "fsl_debug_console.h"   /* PRINTF, PUTCHAR */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define CAL_UART              LPUART0   /* debug console (BOARD_DEBUG_UART_BASEADDR) */
#define CAL_AVERAGE_SAMPLES   16u       /* about 1.6 s at the HX711's 10 samples/s     */
#define CAL_SAMPLE_TIMEOUT_MS 300u      /* longer than one 10 samples/s conversion      */
#define CAL_POLL_MS           20u
#define CAL_LIVE_PERIOD_MS    500u
#define CAL_MIN_SPAN_COUNTS   500       /* less: weight not on the cell, or far too light */
#define CAL_MAX_GRAMS         1000      /* 1 kg load cell                                */
#define CAL_LINE_MAX          24u
#define CAL_SETTLE_MS         1500u     /* same settle time as the feeding logic         */

static bool    hasZero  = false;
static bool    hasSpan  = false;
static int32_t emptyRaw = 0;
static int32_t refRaw   = 0;
static int32_t refMg    = 0;

/* Non-blocking key read. The SDK's DbgConsole_TryGetchar is not built in this project
 * (blocking console), so poll the console LPUART directly. */
static bool readChar(char *ch) {
    uint32_t flags = LPUART_GetStatusFlags(CAL_UART);
    if ((flags & (uint32_t)kLPUART_RxOverrunFlag) != 0u) {
        (void)LPUART_ClearStatusFlags(CAL_UART, (uint32_t)kLPUART_RxOverrunFlag);
    }
    if ((flags & (uint32_t)kLPUART_RxDataRegFullFlag) == 0u) {
        return false;
    }
    *ch = (char)LPUART_ReadByte(CAL_UART);
    return true;
}

/* Waits for the next HX711 conversion */
static bool readRawWait(int32_t *raw) {
    for (uint32_t waited = 0u; waited <= CAL_SAMPLE_TIMEOUT_MS; waited += CAL_POLL_MS) {
        if (FoodSensor_ReadRaw(raw)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(CAL_POLL_MS));
    }
    return false;
}

/* Average of CAL_AVERAGE_SAMPLES readings. spread = max - min: large means the cell or
 * bowl is moving or loose. */
static bool averageRaw(int32_t *avg, int32_t *spread) {
    int64_t sum = 0;
    int32_t lo = 0, hi = 0;

    for (uint32_t i = 0u; i < CAL_AVERAGE_SAMPLES; i++) {
        int32_t raw;
        if (!readRawWait(&raw)) {
            PRINTF("  no reading from the HX711: check DOUT (PTB1), SCK (PTB3), VCC and GND\r\n");
            return false;
        }
        if (i == 0u || raw < lo) { lo = raw; }
        if (i == 0u || raw > hi) { hi = raw; }
        sum += raw;
    }
    *avg    = (int32_t)(sum / (int64_t)CAL_AVERAGE_SAMPLES);
    *spread = hi - lo;
    return true;
}

/* Same formula as FoodSensor_ConvertToMilligrams, without its plausibility limits */
static int32_t toMilligrams(int32_t raw) {
    return (int32_t)(((int64_t)raw - emptyRaw) * refMg / ((int64_t)refRaw - emptyRaw));
}

/* Prints e.g. " 123.4 g" or "-0.6 g" */
static void printGrams(int32_t mg) {
    uint32_t a = (mg < 0) ? (uint32_t)(-(int64_t)mg) : (uint32_t)mg;
    PRINTF("%s%u.%u g", (mg < 0) ? "-" : " ", (unsigned)(a / 1000u), (unsigned)((a % 1000u) / 100u));
}

static void printHelp(void) {
    PRINTF("\r\n=== Food load cell calibration ===\r\n"
           "  z          zero: empty bowl mounted, nothing in it\r\n"
           "  c <grams>  calibrate: known weight in the bowl first, e.g. c 200\r\n"
           "  m          measure once (averaged)\r\n"
           "  s          start/stop live readings\r\n"
           "  gate <us>     hold the gate at a pulse width, 500-2500 us (about 0-180 deg)\r\n"
           "  closed <us>   save the closed position\r\n"
           "  open <us>     save the open position (the gate swings open once)\r\n"
           "  time <ms>     save how long the gate stays open (portion size)\r\n"
           "  drop          one real dispense, weighing the bowl before and after\r\n"
           "  p          print the #defines for food_sensor.c and dispenser.c\r\n"
           "  h          this help\r\n"
           "Load cell: z (empty) -> add weight -> c <grams> -> m to check\r\n"
           "Gate: gate <us> to find positions -> closed / open -> time -> drop, repeat\r\n\r\n");
}

static void printDefines(void) {
    uint32_t closedUs, openUs, openMs;
    Dispenser_GetTuning(&closedUs, &openUs, &openMs);
    PRINTF("\r\n/* Paste into firmware/mcxc444/actuators/dispenser.c */\r\n"
           "#define SERVO_CLOSED_US     %uu\r\n"
           "#define SERVO_OPEN_US       %uu\r\n"
           "#define DISPENSE_OPEN_MS    %uu\r\n",
           (unsigned)closedUs, (unsigned)openUs, (unsigned)openMs);
    if (!hasZero || !hasSpan) {
        PRINTF("  load cell not calibrated yet: run z, then c <grams>\r\n");
        return;
    }
    PRINTF("\r\n/* Paste into firmware/mcxc444/sensing/food_sensor.c */\r\n"
           "#define FOOD_CALIBRATED          1u\r\n"
           "#define FOOD_EMPTY_RAW           %ld\r\n"
           "#define FOOD_REFERENCE_RAW       %ld\r\n"
           "#define FOOD_REFERENCE_MASS_MG   %ld\r\n\r\n",
           (long)emptyRaw, (long)refRaw, (long)refMg);
}

static void cmdZero(void) {
    int32_t avg, spread;
    PRINTF("  zeroing, keep the bowl empty and still...\r\n");
    if (!averageRaw(&avg, &spread)) {
        return;
    }
    if (hasSpan) {
        refRaw += avg - emptyRaw;   /* keep the measured scale across a re-zero */
    }
    emptyRaw = avg;
    hasZero  = true;
    PRINTF("  zero = %ld counts (spread %ld)\r\n", (long)emptyRaw, (long)spread);
    PRINTF(hasSpan ? "  re-zeroed, calibration kept\r\n"
                   : "  now put a known weight in the bowl and type c <grams>\r\n");
}

static void cmdCalibrate(const char *arg) {
    char *end;
    long grams = strtol(arg, &end, 10);
    int32_t avg, spread;

    if (!hasZero) {
        PRINTF("  zero first: empty the bowl and type z\r\n");
        return;
    }
    if (end == arg || grams <= 0 || grams > CAL_MAX_GRAMS) {
        PRINTF("  give the weight in whole grams, 1..%d, e.g. c 200\r\n", CAL_MAX_GRAMS);
        return;
    }
    PRINTF("  measuring %ld g, keep still...\r\n", grams);
    if (!averageRaw(&avg, &spread)) {
        return;
    }
    int32_t span = avg - emptyRaw;
    if (span > -CAL_MIN_SPAN_COUNTS && span < CAL_MIN_SPAN_COUNTS) {
        PRINTF("  reading moved only %ld counts from zero: is the weight on the cell? not saved\r\n",
               (long)span);
        return;
    }
    refRaw  = avg;
    refMg   = (int32_t)(grams * 1000);
    hasSpan = true;
    PRINTF("  reference = %ld counts (spread %ld), %ld counts per gram%s\r\n",
           (long)refRaw, (long)spread, (long)(span / grams),
           (span < 0) ? " (cell mounted upside down: fine, sign is handled)" : "");
    printDefines();
}

static void cmdMeasure(void) {
    int32_t avg, spread;
    if (!averageRaw(&avg, &spread)) {
        return;
    }
    PRINTF("  raw %ld (spread %ld)", (long)avg, (long)spread);
    if (hasZero && hasSpan) {
        PRINTF("  weight ");
        printGrams(toMilligrams(avg));
    }
    PRINTF("\r\n");
}

/* Degrees for an SG90 (0.5 ms = 0 deg, 2.4 ms = 180 deg), for display only */
static unsigned approxDeg(uint32_t us) {
    return (us <= 500u) ? 0u : (unsigned)((us - 500u) * 180u / 1900u);
}

/* Parses a whole number argument within [lo, hi]; prints the usage on failure */
static bool parseArg(const char *arg, uint32_t lo, uint32_t hi, const char *usage, uint32_t *out) {
    char *end;
    unsigned long v = strtoul(arg, &end, 10);
    if (end == arg || v < lo || v > hi) {
        PRINTF("  usage: %s (%u to %u)\r\n", usage, (unsigned)lo, (unsigned)hi);
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static void cmdGate(const char *arg) {
    uint32_t us;
    if (!parseArg(arg, 500u, 2500u, "gate <us>", &us)) {
        return;
    }
    if (!Dispenser_HoldPulse(us)) {
        PRINTF("  gate busy, try again\r\n");
        return;
    }
    PRINTF("  gate held at %u us (about %u deg). Save it with closed %u or open %u\r\n",
           (unsigned)us, approxDeg(us), (unsigned)us, (unsigned)us);
}

static void cmdSetGate(const char *arg, bool open) {
    uint32_t us, closedUs, openUs, openMs;
    if (!parseArg(arg, 500u, 2500u, open ? "open <us>" : "closed <us>", &us)) {
        return;
    }
    Dispenser_GetTuning(&closedUs, &openUs, &openMs);
    if (open) {
        openUs = us;
    } else {
        closedUs = us;
    }
    Dispenser_SetTuning(closedUs, openUs, openMs);   /* also moves the gate to closed */
    PRINTF("  %s position = %u us (about %u deg)\r\n", open ? "open" : "closed",
           (unsigned)us, approxDeg(us));
    if (open && Dispenser_HoldPulse(openUs)) {        /* show the open position once */
        vTaskDelay(pdMS_TO_TICKS(1000u));
        (void)Dispenser_HoldPulse(closedUs);
    }
}

static void cmdTime(const char *arg) {
    uint32_t ms, closedUs, openUs, openMs;
    if (!parseArg(arg, 50u, 5000u, "time <ms>", &ms)) {
        return;
    }
    Dispenser_GetTuning(&closedUs, &openUs, &openMs);
    Dispenser_SetTuning(closedUs, openUs, ms);
    PRINTF("  gate stays open %u ms per portion\r\n", (unsigned)ms);
}

/* One real dispense through the actuator task, as the feeding logic does it */
static void cmdDrop(void) {
    int32_t before, after, spread;
    ActuatorCmd cmd = { .type = ACT_DISPENSE, .value = 1u, .request_id = 0u };

    PRINTF("  weighing before...\r\n");
    if (!averageRaw(&before, &spread)) {
        return;
    }
    (void)xSemaphoreTake(dispenseDoneSem, 0);         /* clear a stale signal */
    if (xQueueSend(actuatorQueue, &cmd, 0) != pdPASS) {
        PRINTF("  actuator queue full, try again\r\n");
        return;
    }
    if (xSemaphoreTake(dispenseDoneSem, pdMS_TO_TICKS(5000u)) != pdTRUE) {
        PRINTF("  gate cycle did not finish\r\n");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(CAL_SETTLE_MS));
    if (!averageRaw(&after, &spread)) {
        return;
    }
    PRINTF("  portion: %ld counts", (long)(after - before));
    if (hasZero && hasSpan) {
        PRINTF(" =");
        printGrams(toMilligrams(after) - toMilligrams(before));
        PRINTF(", bowl now");
        printGrams(toMilligrams(after));
    }
    PRINTF("\r\n");
}

/* True if line is the word w, alone or followed by a space; *arg points past it */
static bool isWord(const char *line, const char *w, const char **arg) {
    size_t n = strlen(w);
    if (strncmp(line, w, n) != 0 || (line[n] != '\0' && line[n] != ' ')) {
        return false;
    }
    *arg = line + n;
    while (**arg == ' ') {
        (*arg)++;
    }
    return true;
}

static void runCommand(char *line) {
    const char *word_arg;
    if (isWord(line, "gate", &word_arg))   { cmdGate(word_arg);           return; }
    if (isWord(line, "closed", &word_arg)) { cmdSetGate(word_arg, false); return; }
    if (isWord(line, "open", &word_arg))   { cmdSetGate(word_arg, true);  return; }
    if (isWord(line, "time", &word_arg))   { cmdTime(word_arg);           return; }
    if (isWord(line, "drop", &word_arg))   { cmdDrop();                   return; }

    char *arg = line + 1;
    while (*arg == ' ') {
        arg++;
    }
    switch (line[0]) {
    case 'z': case 'Z': cmdZero();           break;
    case 'c': case 'C': cmdCalibrate(arg);   break;
    case 'm': case 'M': cmdMeasure();        break;
    case 'p': case 'P': printDefines();      break;
    case 'h': case 'H': case '?': printHelp(); break;
    default:  PRINTF("  unknown command, type h for help\r\n"); break;
    }
}

void FoodCalibration_Task(void *argument) {
    (void)argument;

    char       line[CAL_LINE_MAX];
    uint32_t   len  = 0u;
    bool       live = false;
    TickType_t lastLive = xTaskGetTickCount();

    vTaskDelay(pdMS_TO_TICKS(500u));   /* let the start-up messages finish */
    printHelp();
    /* Start from the calibration in food_sensor.c, so grams work at once (z re-zeroes) */
    if (FoodSensor_GetCalibration(&emptyRaw, &refRaw, &refMg)) {
        hasZero = true;
        hasSpan = true;
        PRINTF("Using the saved load cell calibration (type z with an empty bowl to re-zero).\r\n");
    }
    PRINTF("> ");

    for (;;) {
        char ch;
        while (readChar(&ch)) {
            if (ch == '\r' || ch == '\n') {
                if (len == 0u) {
                    continue;                      /* second half of CR LF, or empty line */
                }
                line[len] = '\0';
                len = 0u;
                PRINTF("\r\n");
                if (line[0] == 's' || line[0] == 'S') {
                    live = !live;
                    PRINTF(live ? "  live readings on (type s + Enter to stop)\r\n"
                                : "  live readings off\r\n");
                } else {
                    runCommand(line);
                }
                PRINTF("> ");
            } else if (ch == '\b' || ch == 0x7F) {     /* backspace */
                if (len > 0u) {
                    len--;
                    PRINTF("\b \b");
                }
            } else if (ch >= ' ' && len < CAL_LINE_MAX - 1u) {
                line[len++] = ch;
                PUTCHAR(ch);                            /* echo */
            }
        }

        if (live && (TickType_t)(xTaskGetTickCount() - lastLive) >= pdMS_TO_TICKS(CAL_LIVE_PERIOD_MS)) {
            int32_t raw;
            lastLive = xTaskGetTickCount();
            if (readRawWait(&raw)) {
                PRINTF("  live raw %ld", (long)raw);
                if (hasZero && hasSpan) {
                    PRINTF("  ");
                    printGrams(toMilligrams(raw));
                }
                PRINTF("\r\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CAL_POLL_MS));
    }
}

#endif /* FOOD_CALIBRATION_MODE */
