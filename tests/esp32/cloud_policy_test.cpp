#include "cloud_policy.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  CloudPolicy policy;
  assert(policy.Ready(0));
  const uint32_t delays[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
  uint32_t now = 0;
  for (uint32_t delay : delays) {
    policy.Failed(now);
    assert(!policy.Ready(now + delay - 1));
    assert(policy.Ready(now + delay));
    now += delay;
  }
  policy.Connected();
  assert(policy.Ready(now));
  policy.Failed(now);
  assert(policy.Ready(now + 1000));  // Backoff resets after connection.
  policy.Failed(now + 1000);
  policy.Connected();

  CloudPolicy rollover;
  rollover.Failed(UINT32_MAX - 499);
  assert(!rollover.Ready(499));
  assert(rollover.Ready(500));
  std::puts("PASS: reconnect backoff boundaries/cap/reset and rollover");
}
