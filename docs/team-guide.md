# Team guide

[Back to project README](../README.md)

## Ownership

Member numbers are provisional; replace them with names when agreed. Module owners maintain their own documentation and test evidence.

| Member | Owns | Independent demonstration |
|---|---|---|
| 1 | Physical dispenser, motor driver and actuator task | Bounded dispensing stops on time while buzzer/light commands remain responsive |
| 2 | Load-cell calibration/filtering, water sensing, low-food decision | Stable readings, low-water status and one feeding request |
| 3 | MCXC444 startup, FreeRTOS setup, queue definitions, mutex, button ISR/task and buzzer/light functions | Button event/beep and light control while sensing continues |
| 4 | Both UART tasks and protocol | Readings, commands and results cross boards reliably |
| 5 | Environmental sensing/task, Wi-Fi/MQTT, dashboard, alerts and reconnection | Environment works independently; telemetry appears and light commands have confirmed results |

Member 2 decides **when to request** food. Member 3 defines and creates the actuator command queue with Member 1. Member 1 owns the actuator task that consumes requests and controls **how the motor moves**, including deadlines. Member 3 supplies the buzzer/light functions called by that task. No other task should directly operate actuator outputs or duplicate feeding policy.

Each member implements and tests their own task entry points. Member 3 owns central MCXC444 task creation, priorities and shared synchronization design, not everyone's task code or debugging. Members 4 and 5 jointly review ESP32 startup/task creation. Queue/interface changes are agreed with their producers and consumers.

Member 3 coordinates local integration; Member 4 coordinates inter-board integration; Member 5 coordinates cloud integration. Everyone fixes their own module and contributes report/demo evidence.

## Working together

1. Pull the latest `main`, then create a branch such as `feature/food-sensing` or `docs/pin-map`.
2. Change your subsystem and its relevant documentation; keep commits focused.
3. Before changing an interface, agree with its consumers and update the system design/protocol in the same change.
4. Run the applicable manual checks and describe results or hardware limitations in the pull request.
5. Ask one teammate to review; shared-interface changes need the affected owner. Merge without force-pushing shared history.

Do not commit real credentials. Copy `secrets.example.h` to ignored `secrets.h` when implementation begins. Member numbers are guidance, not GitHub permission restrictions. Collaborator invitations and branch protection must be configured separately by the repository owner.

## Integration order

- [ ] Agree on pins, motor model, sensor module, proposed API signatures and UART format.
- [ ] Member 1 tests dispenser stopping; Member 2 tests calibrated food/water readings separately.
- [ ] Members 1 and 2 connect feeding requests to the actuator task; Member 1 verifies motor deadlines, with Member 3 providing queue/setup support.
- [ ] Members 1 and 3 integrate button processing, acknowledgement, food-ready cue and light control.
- [ ] Member 4 tests framed UART with sample readings, partial packets and command results.
- [ ] Member 5 integrates ESP32 environmental monitoring independently of MCXC444.
- [ ] Member 5 tests the dashboard with clearly labelled sample data, then connects real telemetry.
- [ ] Run concurrent-operation, broken-link and reconnect scenarios in the [test checklist](../tests/README.md).

Each stage is complete only when the relevant owner records expected and actual results. Integration order belongs here; detailed test procedures belong in the test checklist.
