# Connected Pet Care Station

CG2271 group project: monitor food, water and environmental conditions, notify an owner when the pet presses a button, dispense food and control the station light remotely.

**Status: design scaffold only.** All firmware files contain comments, proposed function signatures and TODOs. There is no executable firmware, configured IDE project or completed hardware test yet.

## Start here

- [Team guide: ownership, Git workflow and integration](docs/team-guide.md)
- [System design: tasks, synchronization and module interfaces](docs/system-design.md)
- [Hardware: components and pin worksheet](docs/hardware.md)
- [UART protocol agreement](protocol/README.md)
- [Dashboard requirements](dashboard/README.md)
- [Integration order](docs/team-guide.md#integration-order) and [test checklist](tests/README.md)

## Board responsibilities

| Board | Responsibilities |
|---|---|
| MCXC444 | Button ISR, food/water sensing, local control, motor/buzzer/light and UART |
| ESP32 | Temperature/humidity sensing, environmental alerts, UART bridge, Wi-Fi and ThingsBoard MQTT |

Water is monitored only; automatic water replenishment needs an additional pump. The buzzer acknowledges button presses and provides a food-ready cue after successful dispensing. Remote light commands return applied-state confirmation.

## Work on a subsystem

| Provisional owner | Starting files and folders |
|---|---|
| Member 1 | [Dispenser interface](firmware/mcxc444/actuators/dispenser.h), [motor implementation template](firmware/mcxc444/actuators/dispenser.c), [actuator task](firmware/mcxc444/control/actuator_task.c), [mechanical files](hardware/dispenser/README.md) |
| Member 2 | [Food sensing](firmware/mcxc444/sensing/food_sensor.c), [water sensing](firmware/mcxc444/sensing/water_sensor.c) |
| Member 3 | [RTOS setup](firmware/mcxc444/app/rtos_setup.c), [shared state](firmware/mcxc444/app/shared_state.h), [button task and ISR](firmware/mcxc444/control/button.c), [buzzer and LED](firmware/mcxc444/actuators/feedback.c) |
| Member 4 | [MCXC444 UART](firmware/mcxc444/communication/uart_link.c), [ESP32 UART](firmware/esp32/pet_care_station/uart_bridge.cpp), [shared message template](protocol/messages.h) |
| Member 5 | [Environment sensing](firmware/esp32/pet_care_station/environment.cpp), [Cloud client](firmware/esp32/pet_care_station/cloud_client.cpp), [secrets template](firmware/esp32/pet_care_station/secrets.example.h), [dashboard guide](dashboard/README.md) |

## Folder map

```text
docs/                         Team guide, system design, hardware worksheet
firmware/mcxc444/              C templates for the local controller
  app/                        Entry point, RTOS setup and shared state
  sensing/                    Food and water acquisition and conditions
  actuators/                  Motor, buzzer and light drivers
  control/                    Button processing and actuator ownership
  communication/              Board UART endpoint
firmware/esp32/                Arduino C++ templates
  pet_care_station/            Sketch, environment, bridge and cloud modules
protocol/                     Cross-board message contract
dashboard/                    ThingsBoard setup and future exports
hardware/dispenser/           Future CAD, wiring photos and assembly notes
tests/                        Manual subsystem and integration checks
```

## Before implementation

Read the [MCXC444 setup notes](firmware/mcxc444/README.md) and [ESP32 setup notes](firmware/esp32/README.md). Agree on pins, sensor/motor models, timing constants and UART encoding before integrating. TODOs deliberately avoid guessing these hardware-dependent choices.

Work in a short-lived branch, keep changes within your module where possible, and ask affected owners to review interface changes. Do not commit Wi-Fi credentials, device tokens, private keys or local IDE/build output. No automated build is claimed for this comment-only scaffold.
