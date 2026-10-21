/*
 * File: dispenser.c
 * Provisional owner: Member 1
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Control mechanical dispensing and local stop limit.
 * Dependencies/consumers: Motor driver; actuator task.
 * Template role: Implementation work outline; public declarations belong in matching header where present.
 *
 * Proposed functions/types (all intentionally commented):
 *   void Dispenser_Init(void);
 *   bool Dispenser_Start(uint32_t now_ms);
 *   void Dispenser_Update(uint32_t now_ms);
 *   void Dispenser_Stop(void);
 *   DispenseStatus Dispenser_GetStatus(void);
 *
 * TODO:
 * - Select actual motor/servo and driver.
 * - Non-blocking start/update/stop.
 * - Reject busy restart; enforce maximum runtime and report completion/failure.
 * - No sensor policy or cloud calls.
 * - Define DispenseStatus..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
