/*
 * File: servo_test.h
 * Responsibility: Stand-alone servo test, used to check the SG90 wiring and find the gate angles.
 */
#ifndef SERVO_TEST_H
#define SERVO_TEST_H

/* 1 = main() runs ONLY the servo test console (no FreeRTOS, no other devices) instead of the
 * pet station. Keep 0 for normal operation. */
#define SERVO_TEST_ONLY  0

#if SERVO_TEST_ONLY
/* Never returns. Call after the board, clock and debug console init. */
void ServoTest_Run(void);
#endif

#endif /* SERVO_TEST_H */
