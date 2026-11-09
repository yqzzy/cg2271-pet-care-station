#ifndef PET_CARE_BRIDGE_CONTROL_H
#define PET_CARE_BRIDGE_CONTROL_H
#include <stddef.h>
#include <stdint.h>

struct ControlSnapshot {
  uint32_t revision = 0;
  uint32_t button_presses = 0;
  uint32_t last_button_ms = 0;
  bool light_known = false;
  bool light_on = false;
  bool light_pending = false;
  const char *light_status = "unknown";
};
struct LightOutcome {
  uint32_t token = 0;
  bool success = false;
  bool on = false;
};

// Pure state machine. The bridge holds its short critical section around access.
// No UART, allocation, logging, or network calls belong in this class.
class BridgeControl {
 public:
  ControlSnapshot Snapshot() const { return state_; }
  void Button(uint32_t now) {
    if (state_.button_presses != UINT32_MAX) ++state_.button_presses;
    state_.last_button_ms = now;
    ++state_.revision;
  }
  bool Request(bool on, uint32_t now, uint32_t *token) {
    if (!token || state_.light_pending || outcome_ready_) return false;
    if (++token_ == 0) ++token_;
    *token = token_;
    desired_ = on;
    queued_at_ = now;
    sent_ = false;
    claimed_ = false;
    cancel_claimed_ = false;
    state_.light_pending = true;
    state_.light_status = "queued";
    ++state_.revision;
    return true;
  }
  bool Queued(uint32_t now, bool *on) {
    if (!state_.light_pending || sent_ || claimed_) return false;
    if (uint32_t(now - queued_at_) >= 3000) {
      Finish(false, false, "queue_timeout");
      return false;
    }
    *on = desired_;
    return true;
  }
  void Claim() { claimed_ = true; }
  void Sent() {
    claimed_ = false;
    sent_ = true;
    state_.light_status = "waiting_ack";
    ++state_.revision;
  }
  bool WaitingAck() const { return state_.light_pending && sent_; }
  void Acknowledged(bool on) {
    if (WaitingAck()) Finish(true, on, "acknowledged");
  }
  void TimedOut() {
    if (WaitingAck()) Finish(false, false, "ack_timeout");
  }
  void Deferred() {
    claimed_ = false;
    if (cancel_claimed_) Finish(false, false, "cancelled");
  }
  void CancelQueued() {
    if (!state_.light_pending || sent_) return;
    // Poll may be outside the lock in sendLight(). Its return decides whether
    // the driver accepted ownership or cancellation can still prevent retry.
    if (claimed_) cancel_claimed_ = true;
    else Finish(false, false, "cancelled");
  }
  bool TakeOutcome(LightOutcome *out) {
    if (!out || !outcome_ready_) return false;
    *out = outcome_;
    outcome_ready_ = false;
    return true;
  }
 private:
  void Finish(bool success, bool on, const char *status) {
    state_.light_pending = false;
    state_.light_known = success;
    if (success) state_.light_on = on;
    state_.light_status = status;
    outcome_.token = token_;
    outcome_.success = success;
    outcome_.on = on;
    outcome_ready_ = true;
    ++state_.revision;
  }
  ControlSnapshot state_;
  LightOutcome outcome_;
  bool outcome_ready_ = false;
  bool desired_ = false;
  bool sent_ = false;
  bool claimed_ = false;
  bool cancel_claimed_ = false;
  uint32_t token_ = 0;
  uint32_t queued_at_ = 0;
};

// Wall time is seconds since Unix epoch; omit event wall time until synchronized.
bool BuildControlTelemetry(char *out, size_t size, const ControlSnapshot &state,
                           uint32_t now, int64_t unix_seconds);
#endif
