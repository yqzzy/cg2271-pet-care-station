# Manual test checklist

[Back to project README](../README.md)

These are future manual acceptance scenarios, not automated tests or claims of passing firmware. Record date, hardware/version, expected result, actual result and evidence for each run. Start with labelled sample data where hardware is unavailable.

| Subsystem or scenario | Expected result |
|---|---|
| Dispenser request | Motor runs for bounded operation and stops; busy requests do not restart the deadline |
| Motor timeout | Stops at configured maximum even with no cloud/UART response |
| Food calibration/tare | Known masses are reasonable; empty bowl is excluded without zeroing food at boot |
| Low food/noisy weight | Stable-low condition produces one request, not a request every sample; rearm/interval follows agreed policy |
| Low water | Updates status and dashboard alert; no food motor activation due solely to water |
| Button bounce | One deliberate press gives one confirmed event and acknowledgement |
| Food-ready cue | Short cue follows appropriate successful dispensing result; no blocking delay |
| Shared snapshots | Concurrent updates/transmission preserve a consistent snapshot; no lock held during I/O |
| UART partial/corrupt/oversize frame | Rejected or recovered safely, no unintended output change |
| Remote light command | Desired state travels to MCXC444 and matching applied-state result returns |
| Duplicate light command | Harmless repeated set; results remain correlated |
| Failed environment read | Invalid status, not a fabricated zero reading |
| MCXC444/link failure | Environmental sampling continues on powered ESP32; local-board data is stale |
| Wi-Fi loss/reconnect | Local button/control continues; reconnect does not replay stale actuator requests |
| Concurrent operation | During dispensing, press button and send light command; measure response and motor-stop timing |
| Full queues | Failures handled without ISR blocking or unbounded waiting |

## Evidence record template

- Scenario / tester / date:
- Commit and hardware setup:
- Configured periods, thresholds and deadlines:
- Expected result:
- Actual result and measured latency:
- Pass/fail, evidence link and follow-up:

Integration sequence and responsible members are in [the team guide](../docs/team-guide.md#integration-order).
