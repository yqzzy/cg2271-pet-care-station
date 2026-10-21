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
