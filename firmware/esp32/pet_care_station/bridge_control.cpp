#include "bridge_control.h"
#include <stdio.h>

bool BuildControlTelemetry(char *out, size_t size, const ControlSnapshot &s,
                           uint32_t now, int64_t unix_seconds) {
  if (!out || !size) return false;
  char button[112] = "";
  char light[32] = "";
  if (s.button_presses) {
    const uint32_t age = uint32_t(now - s.last_button_ms);
    if (unix_seconds >= 1735689600) {
      snprintf(button, sizeof(button), ",\"last_button_uptime_ms\":%lu,\"last_button_ts\":%lld",
               static_cast<unsigned long>(s.last_button_ms),
               static_cast<long long>(unix_seconds * 1000 - age));
    } else {
      snprintf(button, sizeof(button), ",\"last_button_uptime_ms\":%lu",
               static_cast<unsigned long>(s.last_button_ms));
    }
  }
  if (s.light_known) snprintf(light, sizeof(light), ",\"light_on\":%s", s.light_on ? "true" : "false");
  const int n = snprintf(out, size,
      "{\"button_presses\":%lu%s,\"light_known\":%s,\"light_pending\":%s,\"light_status\":\"%s\"%s}",
      static_cast<unsigned long>(s.button_presses), button,
      s.light_known ? "true" : "false", s.light_pending ? "true" : "false", s.light_status, light);
  if (n < 0 || static_cast<size_t>(n) >= size) { out[0] = '\0'; return false; }
  return true;
}
