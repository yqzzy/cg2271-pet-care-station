/*
 * File: uart_bridge.h
 * Provisional owner: Member 4
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: ESP32 board link and command routing.
 * Dependencies/consumers: Protocol; local snapshots; cloud callbacks.
 * Template role: Public interface agreement; do not duplicate implementation here.
 *
 * Proposed functions/types (all intentionally commented):
 *   void UartBridge_Init(void);
 *   void UartBridge_Task(void *argument);
 *   bool UartBridge_RequestLight(bool on, uint32_t request_id);
 *
 * TODO:
 * - Match wire format; bounded parser; forward events/results to cloud queue.
 * - Track link freshness.
 * - Never claim applied light state from transmission alone.
 * - Define bridge-to-cloud event types jointly with Member 5..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
