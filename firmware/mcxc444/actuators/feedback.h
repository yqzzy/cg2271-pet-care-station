/*
 * File: feedback.h
 * Provisional owner: Member 3
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Own buzzer patterns and light output driver.
 * Dependencies/consumers: Timer/PWM and GPIO; actuator task.
 * Template role: Public interface agreement; do not duplicate implementation here.
 *
 * Proposed functions/types (all intentionally commented):
 *   void Feedback_Init(void);
 *   void Feedback_StartCue(CueType cue, uint32_t now_ms);
 *   void Feedback_Update(uint32_t now_ms);
 *   void Feedback_SetLight(bool on);
 *   bool Feedback_GetLight(void);
 *
 * TODO:
 * - Define short button-ack and food-ready cues; non-blocking deadlines.
 * - Light getter means applied output, not optical feedback.
 * - Agree cue overlap policy; no ISR sound generation..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
