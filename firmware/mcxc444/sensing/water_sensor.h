/*
 * File: water_sensor.h
 * Provisional owner: Member 2
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Acquire water level and low-water state.
 * Dependencies/consumers: ADC; shared state.
 * Template role: Public interface agreement; do not duplicate implementation here.
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
#ifndef WATER_SENSOR_H
#define WATER_SENSOR_H

#include <stdbool.h>

/*
 * Configure the water sensor hardware. 
 * Returns false if initialization fails.
 *
 * Call once during startup, before creating WaterSensor_Task.
 */
bool WaterSensor_Init(void);

/*
 * Periodically read the sensor and publish its latest state.
 * Intended to run as a FreeRTOS task.
 */
void WaterSensor_Task(void *argument);

#endif