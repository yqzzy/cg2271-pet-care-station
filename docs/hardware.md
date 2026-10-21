# Hardware worksheet

[Back to project README](../README.md)

## Connections to agree before wiring

| Component | Board | Interface | Pin allocation | Notes |
|---|---|---|---|---|
| Pet button | MCXC444 | GPIO interrupt | TODO | Confirm polarity, pull resistor and debounce |
| Water sensor | MCXC444 | ADC | TODO | Calibrate low-water threshold; verify voltage |
| Load cell and HX711 | MCXC444 | Data/clock | TODO | Select capacity, amplifier and rigid bowl mounting |
| Temperature/humidity | ESP32 | Module-specific digital interface | TODO | Identify actual kit module and matching library |
| Dispenser motor and driver | MCXC444 | Driver-specific | TODO | Select motor/servo mechanism before choosing pins |
| Passive buzzer | MCXC444 | Timer/PWM | TODO | Confirm drive requirements and cue frequency |
| LED station light | MCXC444 | GPIO/driver | TODO | Current limiting and driver if required |
| Board link | Both | UART TX/RX + ground | TODO | Agree voltage, baud rate and frame format |

Four input modules are proposed; temperature/humidity is one module. Confirm the pet button counts toward the course's sensor requirement. Never count a module's two measurements as two physical sensors without teaching-team agreement.

## Wiring and assembly

- Verify supply/logic levels and common ground before connecting boards. Do not power a motor directly from a GPIO.
- Select a driver and supply suitable for the actual actuator. Record final pin assignments here and in board-generated configuration.
- Keep weighing mechanics stable and electronics separate from water. The kit water board is for a supervised prototype; evaluate suitability before actual animal use.
- Add wiring photos and circuit diagrams after assembly. Preserve CAD in [the dispenser folder](../hardware/dispenser/README.md).
- Automatic water replenishment is outside the current hardware scope.
