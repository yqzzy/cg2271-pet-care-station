/*
 * File: food_sensor.c
 * Provisional owner: Member 2
 * Status: Implemented for lab bring-up; target build and hardware tests pending.
 * Responsibility: Acquire food weight and decide when to request feeding.
 * Dependencies/consumers: HX711; shared state; actuator request queue.
 * TODO (lab): Measure calibration, plausibility limit, thresholds and pulse timing.
 * TODO (Members 1 and 3): Agree request acceptance, busy handling and task settings.
 */

#include "food_sensor.h"
#include "fsl_device_registers.h"
#include <stdint.h>
#include <stddef.h>
#include "fsl_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../app/shared_state.h"
#include "../app/rtos_types.h"
#include "queue.h"
#include "semphr.h"

/* TODO (lab): Check the sampling period against the HX711's output rate. */
#define FOOD_SAMPLE_PERIOD_MS  250u

/* TODO (lab): Set to 1 temporarily to record raw calibration readings. */
#define FOOD_DEBUG_LOG  0u

/*
 * TODO (team): Confirm PTB1 and PTB3 are available for the HX711.
 * PTB1 / J4 pin 4: DOUT
 * PTB3 / J4 pin 8: PD_SCK
 */
#define HX711_PORT        PORTB
#define HX711_GPIO        GPIOB
#define HX711_PORT_CLOCK  SIM_SCGC5_PORTB_MASK
#define HX711_DATA_PIN    1u
#define HX711_CLOCK_PIN   3u

/*
 * Measured with the bench tool (food_calibration.c) on 2026-11-11 after the mechanical rework:
 * 47 g reference, zero taken with the 47 g bowl mounted, so an empty bowl reads 0 g.
 * Redo after remounting the bowl or load cell. Counts fall as weight is added (about 717
 * counts per gram); the conversion handles the sign.
 */
#define FOOD_CALIBRATED          1u
#define FOOD_EMPTY_RAW           64806  // raw count with the empty bowl mounted
#define FOOD_REFERENCE_RAW       31127  // raw count with 47 g more on the cell
#define FOOD_REFERENCE_MASS_MG   47000

/*
 * Reject weights substantially below the empty-bowl baseline.
 * TODO (lab): Tune using empty-bowl noise and bowl-removal measurements.
 * -5000 mg is provisional, not a calibrated bowl-presence threshold.
 */
#define FOOD_MIN_VALID_MG  (-5000)

/*
 * Feeding policy: top the bowl up in portions until it is above the full threshold.
 *   WATCHING    weight <= FOOD_LOW_THRESHOLD_MG for FOOD_LOW_CONFIRM_SAMPLES readings
 *               -> request one portion
 *   DISPENSING  wait for the actuator task to give dispenseDoneSem (gate cycle ended)
 *   SETTLING    wait FOOD_SETTLE_MS so the food lands and the filter catches up, then
 *               weight > FOOD_FULL_THRESHOLD_MG -> LOCKOUT, otherwise request another portion
 *   LOCKOUT     no top-up for FOOD_EATING_TIME_MS (time for the pet to eat) -> WATCHING
 * The portion size (DISPENSE_OPEN_MS in dispenser.c) is tuned so that a top-up from an empty
 * bowl needs at most three portions. An invalid reading (bowl removed, sensor fault) never
 * starts or continues a top-up.
 */
#define FOOD_FEEDING_ENABLED          1u
#define FOOD_LOW_THRESHOLD_MG         10000   /* 10 g or less: start a top-up             */
#define FOOD_FULL_THRESHOLD_MG        20000   /* top up until the bowl is above 20 g      */
#define FOOD_LOW_CONFIRM_SAMPLES      3u
#define FOOD_SETTLE_MS                1500u   /* food lands, 4-sample filter catches up   */
#define FOOD_EATING_TIME_MS           10000u  /* no top-up for 10 s after a full bowl     */
#define FOOD_DISPENSE_TIMEOUT_MS      5000u   /* longest wait for the gate cycle to end   */

/* 1 = print each portion and the end of each top-up (used to tune DISPENSE_OPEN_MS). */
#define FOOD_PORTION_LOG  1u

#if FOOD_DEBUG_LOG || FOOD_PORTION_LOG
#include "fsl_debug_console.h"
#endif

/* Configure the HX711 data and clock pins. */
bool FoodSensor_Init(void)
{
    SIM->SCGC5 |= HX711_PORT_CLOCK;

    /* Select GPIO mode for DOUT and PD_SCK. */
    HX711_PORT->PCR[HX711_DATA_PIN] = PORT_PCR_MUX(1u);
    HX711_PORT->PCR[HX711_CLOCK_PIN] = PORT_PCR_MUX(1u);

    /* Keep the clock low, then configure its pin as an output. */
    HX711_GPIO->PCOR = (1u << HX711_CLOCK_PIN);
    HX711_GPIO->PDDR |= (1u << HX711_CLOCK_PIN);

    /* Configure DOUT as an input. */
    HX711_GPIO->PDDR &= ~(1u << HX711_DATA_PIN);

    /*
     * TODO (lab): Verify wiring and communication with the actual HX711.
     * Pin configuration alone cannot confirm that the module is connected.
     */
    return true;
}

/* Read one raw HX711 sample if a conversion is ready. */
static bool FoodSensor_TryReadRaw(int32_t *raw)
{
    uint32_t bits = 0u;

    if (raw == NULL) {
        return false;
    }

    /* DOUT stays high while the HX711 is preparing a reading. */
    if ((HX711_GPIO->PDIR & (1u << HX711_DATA_PIN)) != 0u) {
        return false;
    }

    /*
     * HX711 pulls DOUT low when a 24-bit signed value is ready to read.
     * microcontroller sends clock pulses on PD_SCK through GPIO
     * and reads the data bit on DOUT for each pulse.
    */
    for (uint32_t i = 0u; i < 24u; i++) {
        /*
         * TODO (lab): Measure the clock pulse width on the actual board.
         * Keep the high pulse short even when other tasks are running.
         */
        taskENTER_CRITICAL();
        HX711_GPIO->PSOR = (1u << HX711_CLOCK_PIN);
        SDK_DelayAtLeastUs(1u, SystemCoreClock);

        bits = (bits << 1u) | ((HX711_GPIO->PDIR >> HX711_DATA_PIN) & 1u);

        HX711_GPIO->PCOR = (1u << HX711_CLOCK_PIN);
        taskEXIT_CRITICAL();

        SDK_DelayAtLeastUs(1u, SystemCoreClock);
    }

    /* Select channel A, gain 128 and check that the transfer finished. */
    bool transfer_complete;

    taskENTER_CRITICAL();
    HX711_GPIO->PSOR = (1u << HX711_CLOCK_PIN);
    SDK_DelayAtLeastUs(1u, SystemCoreClock);
    HX711_GPIO->PCOR = (1u << HX711_CLOCK_PIN);
    SDK_DelayAtLeastUs(1u, SystemCoreClock);

    /* Check promptly, before a task switch could delay us into the next sample. */
    transfer_complete = (HX711_GPIO->PDIR & (1u << HX711_DATA_PIN)) != 0u;
    taskEXIT_CRITICAL();

    if (!transfer_complete) {
        return false;
    }

    /* Convert the HX711's signed 24-bit value into a signed 32-bit value. */
    if ((bits & 0x800000u) != 0u) {
        *raw = (int32_t)bits - 0x1000000;
    } else {
        *raw = (int32_t)bits;
    }

    return true;
}

/* Raw HX711 access for the calibration tool (food_calibration.c). */
bool FoodSensor_ReadRaw(int32_t *raw)
{
    return FoodSensor_TryReadRaw(raw);
}

/* The compiled calibration, so the calibration tool can start from it. False if uncalibrated. */
bool FoodSensor_GetCalibration(int32_t *empty_raw, int32_t *reference_raw, int32_t *reference_mg)
{
    *empty_raw = FOOD_EMPTY_RAW;
    *reference_raw = FOOD_REFERENCE_RAW;
    *reference_mg = FOOD_REFERENCE_MASS_MG;
    return FOOD_CALIBRATED != 0u && FOOD_REFERENCE_RAW != FOOD_EMPTY_RAW;
}

/* Convert a raw HX711 reading to food weight in milligrams. */
static bool FoodSensor_ConvertToMilligrams(int32_t raw, int32_t *milligrams)
{
    int64_t raw_difference;
    int64_t calibration_difference;
    int64_t result;

    if (milligrams == NULL || FOOD_CALIBRATED == 0u) {
        return false;
    }

    /* Reject HX711 readings at its positive or negative limit. */
    if (raw <= -8388608 || raw >= 8388607) {
        return false;
    }

    calibration_difference = (int64_t)FOOD_REFERENCE_RAW - FOOD_EMPTY_RAW;

    if (calibration_difference == 0 ||
        FOOD_REFERENCE_MASS_MG <= 0 ||
        FOOD_REFERENCE_MASS_MG > 1000000) {
        return false;
    }

    raw_difference = (int64_t)raw - FOOD_EMPTY_RAW;
    result = (raw_difference * FOOD_REFERENCE_MASS_MG)
           / calibration_difference;

    /* Reject implausible negative weights before filtering or feeding decisions.
     * The cast is needed: Redlib defines INT32_MIN as (-0x80000000), an unsigned int, which
     * compares as +2147483648 against an int64_t and rejected every reading. */
    if (result < FOOD_MIN_VALID_MG || result < (int32_t)INT32_MIN || result > INT32_MAX) {
        return false;
    }

    *milligrams = (int32_t)result;
    return true;
}

/* TODO (lab): Tune the number of samples after observing sensor noise. */
#define FOOD_FILTER_SAMPLES  4u

typedef struct {
    int32_t samples[FOOD_FILTER_SAMPLES];
    int64_t sum;
    uint32_t next;
    uint32_t count;
} FoodFilter;

/* Add a weight and return an average once the filter is full. */
static bool FoodSensor_Filter(FoodFilter *filter, int32_t weight, int32_t *filtered_weight)
{
    if (filter == NULL || filtered_weight == NULL) {
        return false;
    }

    if (filter->count == FOOD_FILTER_SAMPLES) {
        filter->sum -= filter->samples[filter->next];
    } else {
        filter->count++;
    }

    filter->samples[filter->next] = weight;
    filter->sum += weight;
    filter->next = (filter->next + 1u) % FOOD_FILTER_SAMPLES;

    if (filter->count < FOOD_FILTER_SAMPLES) {
        return false;
    }

    *filtered_weight = (int32_t)(filter->sum / FOOD_FILTER_SAMPLES);
    return true;
}

typedef enum {
    FEED_WATCHING,
    FEED_DISPENSING,
    FEED_SETTLING,
    FEED_LOCKOUT
} FeedPhase;

typedef struct {
    FeedPhase  phase;
    TickType_t since;       /* when the current phase started            */
    uint32_t   low_count;   /* consecutive low readings while WATCHING   */
    uint32_t   portions;    /* portions requested in this top-up         */
    int32_t    before_mg;   /* filtered weight before the latest portion */
} FeedingState;

static void FoodSensor_Enter(FeedingState *state, FeedPhase phase, TickType_t now)
{
    state->phase = phase;
    state->since = now;
}

/* Queue one portion (value 1) for the actuator task. False if the queue is full. */
static bool FoodSensor_RequestPortion(void)
{
    ActuatorCmd command = {
        .type = ACT_DISPENSE,
        .value = 1u,
        .request_id = 0u
    };

    /* Drop a stale "done" left by a dispense this task did not request (bench test). */
    (void)xSemaphoreTake(dispenseDoneSem, 0);
    return xQueueSend(actuatorQueue, &command, 0) == pdPASS;
}

#if FOOD_PORTION_LOG
/* Splits milligrams into sign, whole grams and tenths for PRINTF (no float printing). */
static const char *FoodSensor_Grams(int32_t mg, unsigned *whole, unsigned *tenth)
{
    uint32_t a = (mg < 0) ? (uint32_t)(-(int64_t)mg) : (uint32_t)mg;
    *whole = (unsigned)(a / 1000u);
    *tenth = (unsigned)((a % 1000u) / 100u);
    return (mg < 0) ? "-" : "";
}

static void FoodSensor_LogPortion(const FeedingState *state, int32_t weight, bool full)
{
    unsigned aw, at, bw, bt;
    const char *as = FoodSensor_Grams(weight - state->before_mg, &aw, &at);
    const char *bs = FoodSensor_Grams(weight, &bw, &bt);
    PRINTF("[feed] portion %u added %s%u.%u g, bowl %s%u.%u g%s\r\n",
           (unsigned)state->portions, as, aw, at, bs, bw, bt,
           full ? " -> full, eating time" : " -> another portion");
}
#endif

/* Feeding state machine; see the policy comment at the top of the file. Runs once per sample. */
static void FoodSensor_CheckFeeding(FeedingState *state, bool valid, int32_t weight, TickType_t now)
{
    if (FOOD_FEEDING_ENABLED == 0u ||
        FOOD_LOW_CONFIRM_SAMPLES == 0u ||
        FOOD_LOW_THRESHOLD_MG >= FOOD_FULL_THRESHOLD_MG) {
        return;
    }

    TickType_t elapsed = (TickType_t)(now - state->since);

    switch (state->phase) {
    case FEED_WATCHING:
        if (!valid || weight > FOOD_LOW_THRESHOLD_MG) {
            state->low_count = 0u;
            break;
        }
        if (state->low_count < FOOD_LOW_CONFIRM_SAMPLES) {
            state->low_count++;
        }
        /* A full queue keeps the count, so the request is retried on the next sample. */
        if (state->low_count >= FOOD_LOW_CONFIRM_SAMPLES && FoodSensor_RequestPortion()) {
            state->low_count = 0u;
            state->portions = 1u;
            state->before_mg = weight;
            FoodSensor_Enter(state, FEED_DISPENSING, now);
        }
        break;

    case FEED_DISPENSING:
        /* Given by the actuator task when the gate cycle ends or the request is rejected.
         * The timeout only guards against a lost signal. */
        if (xSemaphoreTake(dispenseDoneSem, 0) == pdTRUE ||
            elapsed >= pdMS_TO_TICKS(FOOD_DISPENSE_TIMEOUT_MS)) {
            FoodSensor_Enter(state, FEED_SETTLING, now);
        }
        break;

    case FEED_SETTLING:
        if (elapsed < pdMS_TO_TICKS(FOOD_SETTLE_MS) || !valid) {
            break;          /* still landing, or no valid weight yet (bowl lifted) */
        }
        if (weight > FOOD_FULL_THRESHOLD_MG) {
#if FOOD_PORTION_LOG
            FoodSensor_LogPortion(state, weight, true);
#endif
            FoodSensor_Enter(state, FEED_LOCKOUT, now);
        } else if (FoodSensor_RequestPortion()) {
#if FOOD_PORTION_LOG
            FoodSensor_LogPortion(state, weight, false);
#endif
            state->portions++;
            state->before_mg = weight;
            FoodSensor_Enter(state, FEED_DISPENSING, now);
        }
        break;

    case FEED_LOCKOUT:
    default:
        if (elapsed >= pdMS_TO_TICKS(FOOD_EATING_TIME_MS)) {
            state->low_count = 0u;
            FoodSensor_Enter(state, FEED_WATCHING, now);
        }
        break;
    }
}

/* Periodically collect a raw HX711 sample for food-weight calibration. */
void FoodSensor_Task(void *argument)
{
    (void)argument;

    TickType_t period = pdMS_TO_TICKS(FOOD_SAMPLE_PERIOD_MS);

    if (period == 0u) {
        period = 1u;
    }

    TickType_t last_wake = xTaskGetTickCount();
    FoodFilter filter = {0};
    FeedingState feeding = {0};

    for (;;) {
        int32_t raw = 0;
        bool acquired = FoodSensor_TryReadRaw(&raw);

        FoodReading reading = {0};
        reading.tick = xTaskGetTickCount();

        int32_t weight = 0;

        if (acquired && FoodSensor_ConvertToMilligrams(raw, &weight)) {
            reading.valid = FoodSensor_Filter(&filter, weight, &reading.value);
        } else {
            /* Discard old samples after a failed reading. */
            filter = (FoodFilter){0};
        }

        FoodSensor_CheckFeeding(&feeding, reading.valid, reading.value, reading.tick);
        SharedState_UpdateFood(&reading);

    #if FOOD_DEBUG_LOG
            if (acquired) {
                PRINTF("food raw=%ld\r\n", (long)raw);
            } else {
                PRINTF("food reading not ready or transfer failed\r\n");
            }
    #endif

        vTaskDelayUntil(&last_wake, period);
    }
}
