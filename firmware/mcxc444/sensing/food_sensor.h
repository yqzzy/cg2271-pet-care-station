/*
 * File: food_sensor.h
 * Provisional owner: Member 2
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Acquire food weight and decide when to request feeding.
 * Dependencies/consumers: HX711; shared state; actuator request queue.
 * Template role: Public interface agreement; do not duplicate implementation here.
 *
 * Proposed functions/types (all intentionally commented):
 *   void FoodSensor_Init(void);
 *   bool FoodSensor_Read(FoodReading *out);
 *   void FoodSensor_Task(void *argument);
 *
 * TODO:
 * - Implement bounded readiness checks, calibration and known-empty tare; filter readings.
 * - Confirm low food; queue a request, never drive motor.
 * - Agree rearm and minimum-attempt interval; avoid repeated requests while low/busy.
 * - No dietary schedule..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
#ifndef FOOD_SENSOR_H
#define FOOD_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

/* Configure the HX711 connection before starting the food sensor task. */
bool FoodSensor_Init(void);

/* Periodically measure food weight and request feeding when appropriate. */
void FoodSensor_Task(void *argument);

/* Read one raw HX711 sample. False if no conversion is ready or the transfer failed.
 * For the calibration tool only: never call while FoodSensor_Task is running. */
bool FoodSensor_ReadRaw(int32_t *raw);

/* The compiled calibration (for the calibration tool). False if not calibrated. */
bool FoodSensor_GetCalibration(int32_t *empty_raw, int32_t *reference_raw, int32_t *reference_mg);

#endif