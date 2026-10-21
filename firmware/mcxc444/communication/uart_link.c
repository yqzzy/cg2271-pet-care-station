/*
 * File: uart_link.c
 * Provisional owner: Member 4
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: MCXC444 endpoint and snapshot/event transport.
 * Dependencies/consumers: UART RX buffer; protocol; shared state; telemetry queue; actuator API.
 * Template role: Implementation work outline; public declarations belong in matching header where present.
 *
 * Proposed functions/types (all intentionally commented):
 *   void UartLink_Init(void);
 *   void UartLink_Task(void *argument);
 *   bool UartLink_QueueEvent(const DeviceEvent *event);
 *
 * TODO:
 * - Bound event waits by telemetry/RX deadlines; drain buffered RX even without button traffic.
 * - Copy state under mutex, release before TX.
 * - Frame/validate/recover partial packets; dispatch light commands and return applied state.
 * - Never debounce buttons here..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
