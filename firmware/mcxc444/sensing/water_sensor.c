/*
 * File: water_sensor.c
 * Provisional owner: Member 2
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Acquire water level and low-water state.
 * Dependencies/consumers: ADC; shared state.
 * Template role: Implementation work outline; public declarations belong in matching header where present.
 *
 * Proposed functions/types (all intentionally commented):
 *   void WaterSensor_Init(void);
 *   bool WaterSensor_Read(WaterReading *out);
 *   void WaterSensor_Task(void *argument);
 *
 * TODO:
 * - Calibrate level/threshold; periodic delay; mark invalid samples.
 * - Update state under mutex.
 * - Low water is alert only, not a food dispense trigger..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */

#include "water_sensor.h"
#include "fsl_device_registers.h"
#include <stdint.h>
#include <stddef.h>
#include "../app/shared_state.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * Proposed water sensor input:
 * PTB2 is ADC0 channel 12, exposed at J4 pin 6.
 */
#define WATER_PORT          PORTB
#define WATER_PORT_CLOCK    SIM_SCGC5_PORTB_MASK
#define WATER_PIN           2u
#define WATER_ADC_CHANNEL   12u
/*
 * TODO (lab): Measure sampling and conversion timing on the board.
 */
#define WATER_SAMPLE_PERIOD_MS       250u 
#define WATER_CONVERSION_TIMEOUT_MS   20u

/*
 * TODO (lab): Enter measured thresholds after lab calibration.
 * Enable only after confirming that raw readings rise with water level.
 */
#define WATER_THRESHOLDS_CONFIGURED  0u
#define WATER_LOW_THRESHOLD          0u
#define WATER_CLEAR_THRESHOLD        0u

#define WATER_ADC_CALIBRATION_LIMIT  1000000u

/* TODO (lab): Set to 1 temporarily to collect calibration readings. */
#define WATER_DEBUG_LOG  0u
#if WATER_DEBUG_LOG
#include "fsl_debug_console.h"
#endif

/* Calibrate ADC0 and store its gain correction values. */
static bool WaterSensor_CalibrateAdc(void)
{
    uint32_t remaining = WATER_ADC_CALIBRATION_LIMIT;
    uint32_t gain;

    /* Clear any previous failure and use 32-sample averaging. */
    ADC0->SC3 = ADC_SC3_CALF_MASK
              | ADC_SC3_AVGE_MASK
              | ADC_SC3_AVGS(3u);

    /* Start calibration using the existing ADC clock and reference. */
    ADC0->SC3 = ADC_SC3_CAL_MASK
              | ADC_SC3_AVGE_MASK
              | ADC_SC3_AVGS(3u);

    while ((ADC0->SC3 & ADC_SC3_CAL_MASK) != 0u && remaining > 0u) {
        remaining--;
    }

    if ((ADC0->SC3 & ADC_SC3_CAL_MASK) != 0u) {
        return false;
    }

    if ((ADC0->SC3 & ADC_SC3_CALF_MASK) != 0u) {
        return false;
    }

    gain = ADC0->CLP0 + ADC0->CLP1 + ADC0->CLP2
         + ADC0->CLP3 + ADC0->CLP4 + ADC0->CLPS;
    ADC0->PG = (gain / 2u) | 0x8000u;

    gain = ADC0->CLM0 + ADC0->CLM1 + ADC0->CLM2
         + ADC0->CLM3 + ADC0->CLM4 + ADC0->CLMS;
    ADC0->MG = (gain / 2u) | 0x8000u;

    return true;
}

bool WaterSensor_Init(void)
{
    /* Enable the port and ADC peripherals before configuring them. */
    SIM->SCGC5 |= WATER_PORT_CLOCK;
    SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;

    /* MUX 0 selects the analog function for PTB2. */
    WATER_PORT->PCR[WATER_PIN] = PORT_PCR_MUX(0u);

    /*
     * Configure ADC0 for 12-bit, single-ended conversions.
     * Use the ADC's asynchronous clock, divided by four.
     */
    ADC0->CFG1 = ADC_CFG1_ADICLK(3u)
               | ADC_CFG1_MODE(1u) // raw values range from 0 to 4095
               | ADC_CFG1_ADIV(2u);

    /* Use the default input selection and software-triggered conversions. */
    ADC0->CFG2 = 0u;
    ADC0->SC2 = 0u; 

    /* Disable normal conversions while preparing calibration. */
    ADC0->SC1[0] = ADC_SC1_ADCH(31u);

    if (!WaterSensor_CalibrateAdc()) {
        return false;
    }

    /* Use 16-sample averaging for normal water readings. */
    ADC0->SC3 = ADC_SC3_AVGE_MASK | ADC_SC3_AVGS(2u);

    return true;
}

static bool conversion_pending = false;

/*
 * Start one conversion on the configured water sensor channel.
 * Returns false if a previous conversion is still pending.
 */
static bool WaterSensor_StartConversion(void)
{
    if (conversion_pending) {
        return false;
    }

    conversion_pending = true;

    /* Writing the channel starts a software-triggered conversion. */
    ADC0->SC1[0] = ADC_SC1_ADCH(WATER_ADC_CHANNEL);

    return true;
}

/*
 * Check the conversion once without waiting.
 * Returns true and writes the raw result only when conversion is complete.
 */
static bool WaterSensor_TryRead(uint16_t *raw)
{
    if (!conversion_pending || raw == NULL) {
        return false;
    }

    if ((ADC0->SC1[0] & ADC_SC1_COCO_MASK) == 0u) {
        return false;
    }

    /* Reading R[0] retrieves the result and clears the completion flag. */
    *raw = (uint16_t)ADC0->R[0];
    conversion_pending = false;

    return true;
}

/*
 * Stop a pending conversion after a timeout.
 * Channel 31 disables the conversion channel.
 */
static void WaterSensor_CancelConversion(void)
{
    ADC0->SC1[0] = ADC_SC1_ADCH(31u);
    conversion_pending = false;
}

/* Determine low-water status from a raw reading and calibrated thresholds. */
static bool WaterSensor_DetermineLow(uint16_t raw, bool *low)
{
    static bool status_known = false;
    static bool is_low = false;

    if (low == NULL || WATER_THRESHOLDS_CONFIGURED == 0u) {
        return false;
    }

    /* Reject unusable thresholds. */
    if (WATER_LOW_THRESHOLD >= WATER_CLEAR_THRESHOLD ||
        WATER_CLEAR_THRESHOLD > 4095u) {
        return false;
    }

    if (raw <= WATER_LOW_THRESHOLD) {
        is_low = true;
        status_known = true;
    } else if (raw >= WATER_CLEAR_THRESHOLD) {
        is_low = false;
        status_known = true;
    }

    if (status_known) {
        *low = is_low;
    }

    return status_known;
}

/* Periodically acquire a water reading and publish it to shared state. */
void WaterSensor_Task(void *argument)
{
    (void)argument;

    TickType_t sample_period = pdMS_TO_TICKS(WATER_SAMPLE_PERIOD_MS);
    TickType_t timeout = pdMS_TO_TICKS(WATER_CONVERSION_TIMEOUT_MS);

    /* Ensure delays remain nonzero if the RTOS tick rate is low. */
    if (sample_period == 0u) {
        sample_period = 1u;
    }

    if (timeout == 0u) {
        timeout = 1u;
    }

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        WaterReading reading = {0};
        uint16_t raw = 0u;
        bool acquired = false;

        if (WaterSensor_StartConversion()) {
            TickType_t start = xTaskGetTickCount();

            do {
                if (WaterSensor_TryRead(&raw)) { // check if result is ready
                    acquired = true;
                    break;
                }

                vTaskDelay(1u); // if not ready, task sleep for one tick before checking again
            } while ((xTaskGetTickCount() - start) < timeout);
        }

        if (acquired) {
            reading.raw = raw;
            reading.tick = xTaskGetTickCount();
            reading.valid = WaterSensor_DetermineLow(raw, &reading.low);
        } else {
            WaterSensor_CancelConversion(); // stop attempt if it takes too long
            reading.tick = xTaskGetTickCount();
        }

        SharedState_UpdateWater(&reading);

        // print result for debugging
        #if WATER_DEBUG_LOG
            if (acquired) {
                PRINTF("water raw=%u, status_valid=%u, low=%u\r\n",
                    (unsigned int)reading.raw,
                    (unsigned int)reading.valid,
                    (unsigned int)reading.low);
            } else {
                PRINTF("water conversion failed or timed out\r\n");
            }
        #endif

        vTaskDelayUntil(&last_wake, sample_period);
    }
}
