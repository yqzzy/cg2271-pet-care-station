# Manual test checklist

[Back to project README](../README.md)

These are future manual acceptance scenarios, not automated tests or claims of passing firmware. Record date, hardware/version, expected result, actual result and evidence for each run. Start with labelled sample data where hardware is unavailable.

Use a logging-only actuator queue receiver to test feeding requests without a motor. Run only one receiver for that queue.

| Subsystem or scenario | Expected result |
|---|---|
| Dispenser request | Motor runs for bounded operation and stops; busy requests do not restart the deadline |
| Motor timeout | Stops at configured maximum even with no cloud/UART response |
| Food calibration/tare | Known masses are reasonable; empty bowl is excluded without zeroing food at boot |
| Low food/noisy weight | Stable-low condition produces one request, not a request every sample; rearm/interval follows agreed policy |
| Food rearm and interval | No repeat while food stays low; after recovery, a new confirmed-low request respects the interval since the last accepted queue send |
| Food queue full | Failed enqueue does not latch the request or advance its timestamp; later valid low readings can retry |
| Bowl removal or implausible negative weight | Values below the configured plausibility limit are invalid; measure and tune the provisional -5000 mg limit on the actual fixture |
| HX711 DOUT stuck low | Missing DOUT-high completion is rejected; a genuine zero sample with valid completion is still accepted by the raw-read function |
| HX711 timing | Measure clock pulses under concurrent load against the HX711 datasheet limits |
| Dispense rejected while busy or failed | Record the agreed acceptance/result policy; queue success alone must not be reported as food delivered |
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

## Software review status

Desktop checks of the extracted food conversion, filter and feeding functions passed for calibration in both raw-count directions, filter warm-up/rolling average, low confirmation, rearm, queue-full retry and interval arithmetic across a simulated 32-bit tick wrap. These used simulated calibration settings and queue/GPIO substitutes, not the target SDK or hardware. The temporary review harness is not a committed regression suite.

Both review fixes are implemented. Follow-up desktop checks passed for the -5000 mg boundary and values below it, both calibration directions, unchanged output on rejection, valid zero/positive/negative HX711 transfers, stuck-low DOUT, missing completion, not-ready/null handling and balanced critical sections. Filter and queue behaviour were also checked, including an invalid negative sample clearing a pending low-food confirmation. GPIO transitions and timing were simulated; neither a full target firmware build nor physical acceptance tests have been demonstrated by these checks.
