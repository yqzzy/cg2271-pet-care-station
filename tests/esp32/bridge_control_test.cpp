#include "bridge_control.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  BridgeControl control;
  assert(!control.Snapshot().light_known);
  control.Button(20);
  control.Button(40);
  assert(control.Snapshot().button_presses == 2);
  assert(control.Snapshot().last_button_ms == 40);
  uint32_t token = 0;
  assert(control.Request(true, 100, &token));
  assert(token != 0 && control.Snapshot().light_pending);
  uint32_t other = 0;
  assert(!control.Request(false, 101, &other));
  bool on = false;
  assert(control.Queued(101, &on) && on);
  control.Claim();
  control.Deferred(); // Transient UART mutex/queue contention retains request.
  assert(control.Queued(102, &on) && on);
  control.Claim();
  control.Sent();
  assert(!control.Queued(102, &on));
  control.Acknowledged(true);
  auto state = control.Snapshot();
  assert(state.light_known && state.light_on && !state.light_pending);
  LightOutcome result;
  assert(control.TakeOutcome(&result));
  assert(result.token == token && result.success && result.on);
  assert(!control.TakeOutcome(&result));
  assert(control.Request(false, 200, &other));
  control.Sent();
  control.TimedOut();
  assert(!control.Snapshot().light_known);
  assert(control.TakeOutcome(&result) && !result.success && result.token == other);
  assert(control.Request(false, UINT32_MAX - 999, &other));
  assert(control.Queued(1999, &on));
  assert(!control.Queued(2000, &on));  // Queue expires at 3000 ms across rollover.
  assert(control.TakeOutcome(&result) && !result.success);
  assert(control.Request(true, 4000, &other));
  control.Claim();
  control.CancelQueued();
  control.Deferred(); // Disconnect races a rejected dispatch: no stale replay.
  assert(!control.Snapshot().light_pending);
  assert(control.TakeOutcome(&result) && !result.success);
  assert(control.Request(true, 5000, &other));
  control.Claim();
  control.CancelQueued(); // An accepted in-flight dispatch can still finish.
  control.Sent();
  control.CancelQueued();  // Already transmitted commands finish their bounded retry.
  assert(control.Snapshot().light_pending);
  control.Acknowledged(true);
  assert(control.TakeOutcome(&result) && result.success);
  char payload[384];
  assert(BuildControlTelemetry(payload, sizeof(payload), control.Snapshot(), 100, 1800000000));
  assert(strstr(payload, "\"button_presses\":2"));
  assert(strstr(payload, "\"last_button_ts\":1799999999940"));
  assert(strstr(payload, "\"light_on\":true"));
  assert(!BuildControlTelemetry(payload, 5, control.Snapshot(), 100, 0));
  BridgeControl unknown;
  assert(BuildControlTelemetry(payload, sizeof(payload), unknown.Snapshot(), 0, 0));
  assert(!strstr(payload, "\"light_on\"") && !strstr(payload, "\"last_button_ts\""));
  puts("PASS: bridge events, light ACK/timeout, queue expiry, rollover and control JSON");
}
