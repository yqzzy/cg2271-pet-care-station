#include "live_telemetry.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>

int main() {
  EnvironmentState state;
  state.Update(21.6f, 69.3f, 1000, 25);
  McuTelemetry mcu;
  mcu.available = true;
  mcu.food_milligrams = 125000;
  mcu.water_enough = true;
  mcu.received_ms = 2000;
  char payload[512];
  assert(BuildLiveTelemetry(payload, sizeof(payload), state.Reading(), mcu, 2500));
  assert(strstr(payload, "\"data_source\":\"live\""));
  assert(strstr(payload, "\"temperature_c\":21.6"));
  assert(strstr(payload, "\"food_weight_g\":125.000"));
  assert(strstr(payload, "\"water_enough\":true,\"water_low\":false"));
  mcu.water_enough = false;
  assert(BuildLiveTelemetry(payload, sizeof(payload), state.Reading(), mcu, 4999));
  assert(strstr(payload, "\"water_enough\":false,\"water_low\":true"));
  assert(BuildLiveTelemetry(payload, sizeof(payload), state.Reading(), mcu, 5000));
  assert(strstr(payload, "\"mcu_data_fresh\":false"));
  assert(!strstr(payload, "food_weight_g"));
  assert(!strstr(payload, "water_enough"));
  assert(!strstr(payload, "water_low"));
  assert(strstr(payload, "temperature_c"));
  state.Update(NAN, NAN, 6000, 25);
  mcu.received_ms = 6000;
  mcu.food_milligrams = -1500;
  assert(BuildLiveTelemetry(payload, sizeof(payload), state.Reading(), mcu, 6000));
  assert(strstr(payload, "\"environment_valid\":false"));
  assert(!strstr(payload, "temperature_c"));
  assert(!strstr(payload, "humidity_pct"));
  assert(!strstr(payload, "temperature_high"));
  assert(!strstr(payload, "humidity_high"));
  assert(strstr(payload, "\"food_weight_g\":-1.500"));
  assert(!strstr(payload, "nan"));
  mcu.available = false;
  assert(BuildLiveTelemetry(payload, sizeof(payload), EnvironmentReading{}, mcu, 9000));
  assert(!strstr(payload, "water_enough"));
  assert(!strstr(payload, "food_weight_g"));
  assert(!strstr(payload, "environment_age_ms"));
  assert(!BuildLiveTelemetry(payload, 8, state.Reading(), mcu, 9000));
  assert(payload[0] == '\0');
  assert(!BuildLiveTelemetry(nullptr, 0, state.Reading(), mcu, 9000));
  state.Update(25, 50, UINT32_MAX - 999, 20);
  mcu.available = true;
  mcu.received_ms = UINT32_MAX - 999;
  assert(BuildLiveTelemetry(payload, sizeof(payload), state.Reading(), mcu, 1000));
  assert(strstr(payload, "\"environment_valid\":true"));
  assert(strstr(payload, "\"mcu_data_fresh\":true"));
  assert(BuildLiveTelemetry(payload, sizeof(payload), state.Reading(), mcu, 14000));
  assert(strstr(payload, "\"environment_valid\":false"));
  PeriodicTelemetry timer;
  assert(timer.Due(0));
  timer.Attempted(0);
  assert(!timer.Due(4999));
  assert(timer.Due(5000));
  timer.Attempted(5000); // A failed publish also waits, rather than busy-retrying.
  assert(!timer.Due(5001));
  assert(timer.Due(10000));
  timer.Attempted(UINT32_MAX - 999);
  assert(!timer.Due(3999));
  assert(timer.Due(4000));
  timer.Reconnected();
  assert(timer.Due(4000)); // Reconnect immediately publishes a current snapshot.
  timer.Attempted(4000);
  assert(!timer.Due(4001));
  // Early publish: only a noticeable MCU change, and at most once a second
  McuTelemetry last, now;
  assert(!McuChangedNotably(last, now));                // both unavailable
  now.available = true;
  assert(McuChangedNotably(last, now));                 // data appeared
  last = now;
  now.food_milligrams = 1999;
  assert(!McuChangedNotably(last, now));                // under 2 g: wait for the period
  now.food_milligrams = 2000;
  assert(McuChangedNotably(last, now));
  now.food_milligrams = -2000;
  assert(McuChangedNotably(last, now));                 // removing food counts too
  now.food_milligrams = 0;
  now.water_enough = !last.water_enough;
  assert(McuChangedNotably(last, now));                 // water flipped
  now = last;
  now.available = false;
  assert(McuChangedNotably(last, now));                 // data lost
  last.food_milligrams = INT32_MIN;
  now = last;
  now.food_milligrams = INT32_MAX;
  assert(McuChangedNotably(last, now));                 // no overflow on extreme values
  PeriodicTelemetry gap;
  assert(gap.EarlyAllowed(0));
  gap.Attempted(10000);
  assert(!gap.EarlyAllowed(10999));
  assert(gap.EarlyAllowed(11000));
  gap.Attempted(UINT32_MAX - 499);
  assert(!gap.EarlyAllowed(499));
  assert(gap.EarlyAllowed(500));
  EnvironmentReading extreme = state.Reading();
  extreme.temperature_c = std::numeric_limits<float>::max();
  extreme.humidity_pct = std::numeric_limits<float>::max();
  assert(!BuildLiveTelemetry(payload, sizeof(payload), extreme, mcu, 1000));
  assert(payload[0] == '\0'); // Intermediate formatting cannot leak partial JSON.
  std::puts("PASS: combined payload, independent failures/staleness, units, unknown state, bounds, rollover, periodic timing, early publish on change");
}
