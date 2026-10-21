# Dispenser mechanical files

[Back to project README](../../README.md)

Owner: Member 1, including the [actuator task](../../firmware/mcxc444/control/actuator_task.c). Add original editable CAD, export files and assembly photos here when available. No CAD is supplied by this scaffold.

Document the mechanism, motor/servo model, driver, power source, mounting, bowl/load-cell clearance, and how the mechanism stops/closes. Coordinate supply and pins with [hardware worksheet](../../docs/hardware.md).

Test a bounded dispense request independently before integrating the low-food decision. Report finished/failed honestly; a motor cycle without weight feedback does not prove food delivery. Agree queue interfaces with Member 3; the actuator task consumes commands and calls Member 3's buzzer/light functions. Keep feeding policy in Member 2's module and motor movement in [dispenser.c](../../firmware/mcxc444/actuators/dispenser.c).
