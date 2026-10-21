/*
 * File: button.c
 * Provisional owner: Member 3
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Defer button processing and split local/cloud paths.
 * Dependencies/consumers: ISR queue; actuator queue; telemetry queue.
 * Template role: Implementation work outline; public declarations belong in matching header where present.
 *
 * Proposed functions/types (all intentionally commented):
 *   void Button_Init(void);
 *   void Button_GPIO_ISR(void);
 *   void Button_Task(void *argument);
 *
 * TODO:
 * - Replace placeholder ISR name with actual vector after pin selection.
 * - Clear flag, timestamp, xQueueSendFromISR and port-appropriate yield; check queue-full.
 * - Task debounces without stalling UART; queue cue and notification separately..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
