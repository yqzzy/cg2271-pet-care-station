# Team guide

[Back to project README](../README.md)

## Ownership

Member numbers are provisional; replace them with names when agreed. Module owners maintain their own documentation and test evidence.

| Member | Owns | Independent demonstration |
|---|---|---|
| 1 | Physical dispenser, motor driver and movement | Test request completes one bounded dispense and stops |
| 2 | Load-cell calibration/filtering, water sensing, low-food decision | Stable readings, low-water status and one feeding request |
| 3 | MCXC444 startup, FreeRTOS, queues, mutex, button, feedback and coordination | Button event/beep and light control while sensing continues |
| 4 | Both UART endpoints, protocol, ESP32 environment | Readings and commands cross boards; environment works independently |
| 5 | Wi-Fi/MQTT, dashboard, cloud commands and reconnection | Sample telemetry appears; light command has confirmed result |

Member 2 decides **when to request** food. Member 3 coordinates the request. Member 1 controls **how the motor moves**. No other module should duplicate feeding policy or write motor outputs.

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
- [ ] Member 3 connects feeding requests to actuator control and checks motor deadline enforcement.
- [ ] Integrate button processing, acknowledgement, food-ready cue and light control.
- [ ] Member 4 tests framed UART with sample readings, partial packets and command results.
- [ ] Integrate ESP32 environmental monitoring independently of MCXC444.
- [ ] Member 5 tests the dashboard with clearly labelled sample data, then connects real telemetry.
- [ ] Run concurrent-operation, broken-link and reconnect scenarios in the [test checklist](../tests/README.md).

Each stage is complete only when the relevant owner records expected and actual results. Integration order belongs here; detailed test procedures belong in the test checklist.
