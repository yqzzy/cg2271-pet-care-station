/*
 * File: food_calibration.h
 * Responsibility: Lab tool to measure the load cell calibration for food_sensor.c.
 */
#ifndef FOOD_CALIBRATION_H
#define FOOD_CALIBRATION_H

/* 1 = run the interactive load cell calibration on the debug console INSTEAD of
 * FoodSensor_Task (see rtos_setup.c). Keep 0 for normal operation: the tool is then not built.
 * After calibrating, paste the printed #defines into food_sensor.c and set this back to 0. */
#define FOOD_CALIBRATION_MODE  0

#if FOOD_CALIBRATION_MODE
/* Call FoodSensor_Init() first. */
void FoodCalibration_Task(void *argument);
#endif

#endif /* FOOD_CALIBRATION_H */
