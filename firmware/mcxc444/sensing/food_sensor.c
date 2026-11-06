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

/* TODO (lab): Check the sampling period against the HX711's output rate. */
#define FOOD_SAMPLE_PERIOD_MS  250u

/* TODO (lab): Set to 1 temporarily to record raw calibration readings. */
#define FOOD_DEBUG_LOG  0u

#if FOOD_DEBUG_LOG
#include "fsl_debug_console.h"
#endif

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
 * Measured with the bowl mounted (80 g reference, 2026-11-06); redo after remounting.
 * The cell is mounted so counts fall as weight is added; the conversion handles the sign.
 */
#define FOOD_CALIBRATED          1u
#define FOOD_EMPTY_RAW           64436  // raw count with the empty bowl (measured 2026-11-06).
#define FOOD_REFERENCE_RAW       7132   // raw count with the empty bowl plus 80 g; counts fall as weight is added.
#define FOOD_REFERENCE_MASS_MG   80000

/*
 * Reject weights substantially below the empty-bowl baseline.
 * TODO (lab): Tune using empty-bowl noise and bowl-removal measurements.
 * -5000 mg is provisional, not a calibrated bowl-presence threshold.
 */
#define FOOD_MIN_VALID_MG  (-5000)

/*
 * TODO (Members 1 and 3): Agree on thresholds, confirmation count,
 * minimum interval and what happens when the dispenser is busy.
 * TODO (lab): Choose thresholds from measured bowl weights.
 */
#define FOOD_FEEDING_ENABLED          0u
#define FOOD_LOW_THRESHOLD_MG         0
#define FOOD_REARM_THRESHOLD_MG       0
#define FOOD_LOW_CONFIRM_SAMPLES      3u
#define FOOD_MIN_INTERVAL_MS          10000u

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

typedef struct {
    uint32_t low_count;
    bool waiting_for_rearm;
    bool has_requested;
    TickType_t last_request;
} FeedingState;

/* Request one portion after food has remained low for several readings. */
static void FoodSensor_CheckFeeding(FeedingState *state, bool valid, int32_t weight, TickType_t now)
{
    TickType_t min_interval = pdMS_TO_TICKS(FOOD_MIN_INTERVAL_MS);

    if (min_interval == 0u) {
        min_interval = 1u;
    }

    if (FOOD_FEEDING_ENABLED == 0u ||
        FOOD_LOW_CONFIRM_SAMPLES == 0u ||
        FOOD_LOW_THRESHOLD_MG >= FOOD_REARM_THRESHOLD_MG) {
        return;
    }

    if (!valid) {
        state->low_count = 0u;
        return;
    }

    if (state->waiting_for_rearm) {
        if (weight >= FOOD_REARM_THRESHOLD_MG) {
            state->waiting_for_rearm = false;
        } else {
            return;
        }
    }

    if (weight > FOOD_LOW_THRESHOLD_MG) {
        state->low_count = 0u;
        return;
    }

    if (state->low_count < FOOD_LOW_CONFIRM_SAMPLES) {
        state->low_count++;
    }

    if (state->low_count < FOOD_LOW_CONFIRM_SAMPLES) {
        return;
    }

    if (state->has_requested && (TickType_t)(now - state->last_request) < min_interval) {
        return;
    }

    /*
     * TODO (Member 1): Confirm the command value and handling when busy.
     * Value 1 currently means a request for one portion.
     */
    ActuatorCmd command = {
        .type = ACT_DISPENSE,
        .value = 1u, // request one portion
        .request_id = 0u
    };

    if (xQueueSend(actuatorQueue, &command, 0u) == pdPASS) {
        state->waiting_for_rearm = true;
        state->has_requested = true;
        state->last_request = now;
        state->low_count = 0u;
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
