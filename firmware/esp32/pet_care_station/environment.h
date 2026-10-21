/*
 * File: environment.h
 * Provisional owner: Member 5
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Read and interpret environment independently.
 * Dependencies/consumers: Sensor library; local cloud snapshot.
 * Template role: Public interface agreement; do not duplicate implementation here.
 *
 * Proposed functions/types (all intentionally commented):
 *   void Environment_Init(void);
 *   bool Environment_Read(EnvironmentReading *out);
 *   void Environment_Task(void *argument);
 *
 * TODO:
 * - Choose module/library, sampling period and thresholds.
 * - Reject failed reads, mark invalid; do not publish zero as failure.
 * - Include temperature/humidity/alert flags and freshness.
 * - Keep sampling independent of network retries..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
