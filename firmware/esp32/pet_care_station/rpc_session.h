#ifndef PET_CARE_RPC_SESSION_H
#define PET_CARE_RPC_SESSION_H
#include <stdint.h>
#include <string.h>

// One MQTT session, one outstanding set, one most-recent response cache.
// The cloud task is the only owner. QoS 1 redelivery must not enqueue twice or
// send a busy error followed by success for the same request ID.
class RpcSession {
 public:
  bool Pending() const { return pending_; }
  bool InFlight(const char *id) const { return pending_ && strcmp(id_, id) == 0; }
  bool Cached(const char *id) const { return completed_id_[0] && strcmp(completed_id_, id) == 0; }
  const char *Id() const { return completed_id_; }
  const char *Response() const { return response_; }
  // id comes from ParseRpcRequest's validated, bounded output.
  void Begin(const char *id, uint32_t token) {
    strcpy(id_, id);
    token_ = token;
    pending_ = true;
  }
  bool Complete(uint32_t token, bool success, bool on) {
    if (!pending_ || token != token_) return false;
    pending_ = false;
    strcpy(completed_id_, id_);
    response_ = success ? (on ? "true" : "false")
                        : "{\"error\":\"light_command_failed\"}";
    return true;
  }
  void Reset() { pending_ = false; id_[0] = '\0'; completed_id_[0] = '\0'; response_ = nullptr; }
 private:
  char id_[11] = "";
  char completed_id_[11] = "";
  uint32_t token_ = 0;
  bool pending_ = false;
  const char *response_ = nullptr;
};
#endif
