/*
 * File: cloud_client.h
 * Provisional owner: Member 5
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Wi-Fi/MQTT and dashboard communication.
 * Dependencies/consumers: Local environment/state; UART bridge; ignored secrets.
 * Template role: Public interface agreement; do not duplicate implementation here.
 *
 * Proposed functions/types (all intentionally commented):
 *   void Cloud_Init(void);
 *   void Cloud_Task(void *argument);
 *   bool Cloud_QueueEvent(const CloudEvent *event);
 *   void Cloud_HandleLightCommand(bool on, uint32_t request_id);
 *
 * TODO:
 * - Select MQTT library and ThingsBoard APIs; TLS certificate validation.
 * - Publish valid readings/status/events; route light requests to bridge.
 * - Correlate confirmation; bounded reconnect and buffering.
 * - No direct actuator writes.
 * - Define CloudEvent with Member 4..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
