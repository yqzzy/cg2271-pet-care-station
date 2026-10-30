# Hardware worksheet

[Back to project README](../README.md)

## Connections to agree before wiring

| Component | Board | Interface | Pin allocation | Notes |
|---|---|---|---|---|
| Pet button | MCXC444 | GPIO interrupt | TODO (final pin). Test default: on-board SW2 on PTC3 | Active low, internal pull-up, falling-edge interrupt, debounced in Button_Task. Must be on Port A, C or D. Edit the pin block at the top of button.c |
| Water sensor | MCXC444 | ADC | TODO | Calibrate low-water threshold; verify voltage |
| Load cell and HX711 | MCXC444 | Data/clock | TODO | Select capacity, amplifier and rigid bowl mounting |
| Temperature/humidity | ESP32 | Module-specific digital interface | TODO | Identify actual kit module and matching library |
| Dispenser motor and driver | MCXC444 | Driver-specific | TODO | Select motor/servo mechanism before choosing pins |
| Passive buzzer | MCXC444 | Timer/PWM | PTE21 (TPM1_CH1, ALT3), proposed. Whole TPM1 reserved for the buzzer | Confirm the real buzzer type and pin first (Member 4's test used an active buzzer on PTB0). Cue frequency 2 kHz default. PTE21 is also ADC0_SE4a, so do not use it for an ADC input |
| LED station light | MCXC444 | GPIO/driver | TODO (final pin). Test default: on-board red LED on PTE31 | Active low by default (LIGHT_ACTIVE_LOW in feedback.c). Current limiting and driver if required |
| Board link | Both | UART TX/RX + ground | MCXC444 UART2: TX PTE22, RX PTE23 (ALT4). ESP32: RX pin 2, TX pin 1 (esp32_test.ino) | 9600 baud, 8N1. MCXC444 TX to ESP32 RX, MCXC444 RX to ESP32 TX, common ground. PTE22 and PTE23 are not available for ADC or PWM |

Four input modules are proposed; temperature/humidity is one module. Confirm the pet button counts toward the course's sensor requirement. Never count a module's two measurements as two physical sensors without teaching-team agreement.

## Wiring and assembly

- Verify supply/logic levels and common ground before connecting boards. Do not power a motor directly from a GPIO.
- Select a driver and supply suitable for the actual actuator. Record final pin assignments here and in board-generated configuration.
- Keep weighing mechanics stable and electronics separate from water. The kit water board is for a supervised prototype; evaluate suitability before actual animal use.
- Add wiring photos and circuit diagrams after assembly. Preserve CAD in [the dispenser folder](../hardware/dispenser/README.md).
- Automatic water replenishment is outside the current hardware scope.
