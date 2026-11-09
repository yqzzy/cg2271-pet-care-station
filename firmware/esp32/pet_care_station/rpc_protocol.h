#ifndef PET_CARE_RPC_PROTOCOL_H
#define PET_CARE_RPC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

struct RpcRequest {
  char id[11];
  bool set;
  bool value;
};

enum RpcParseResult {
  kRpcValid,
  kRpcBadTopic,
  kRpcInvalidPayload,
  kRpcUnsupportedMethod
};

// Accepts at most 256 payload bytes and four nested JSON containers.
// A valid topic ID is preserved even when its payload is invalid. Bad topics
// clear the ID, so the caller can silently discard them without responding.
// Only kRpcValid authorizes reading set/value and changing device state.
RpcParseResult ParseRpcRequest(const char* topic, const uint8_t* payload,
                               size_t length, RpcRequest* out);

#endif
