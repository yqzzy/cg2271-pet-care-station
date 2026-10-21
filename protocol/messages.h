/*
 * File: messages.h
 * Provisional owner: Member 4; review by 3 and 5
 * Status: COMMENT-ONLY TEMPLATE - not implemented or buildable.
 * Responsibility: Shared message vocabulary only.
 * Dependencies/consumers: Both UART endpoints and dashboard command adapter.
 * Template role: Public interface agreement; do not duplicate implementation here.
 *
 * Proposed functions/types (all intentionally commented):
 *   Proposed types: MessageType, SensorSnapshotMessage, ButtonEventMessage, LightCommandMessage, CommandResultMessage, DispenseStatusMessage;
 *
 * TODO:
 * - Define explicit units, validity, IDs and limits.
 * - Set wire format in protocol README before implementation.
 * - No raw struct serialization or assumption that board clocks match..
 * - Agree concrete types, units, errors and timing with consumers before coding.
 * - Verify relevant scenarios in tests/README.md; record evidence.
 */
