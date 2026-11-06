#ifndef PET_CARE_CLOUD_POLICY_H
#define PET_CARE_CLOUD_POLICY_H

#include <stdint.h>

// Shared by the real cloud task and native tests. All times are local millis().
class CloudPolicy {
 public:
  bool Ready(uint32_t now_ms) const {
    return !waiting_ || uint32_t(now_ms - failed_ms_) >= wait_ms_;
  }
  void Failed(uint32_t now_ms) {
    failed_ms_ = now_ms;
    wait_ms_ = next_wait_ms_;
    next_wait_ms_ = next_wait_ms_ < 16000 ? next_wait_ms_ * 2 : 30000;
    waiting_ = true;
  }
  void Connected() { waiting_ = false; next_wait_ms_ = 1000; }

 private:
  uint32_t failed_ms_ = 0;
  uint32_t wait_ms_ = 0;
  uint32_t next_wait_ms_ = 1000;
  bool waiting_ = false;
};

#endif
