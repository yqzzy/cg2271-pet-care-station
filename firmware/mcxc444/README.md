# MCXC444 firmware

[Back to project README](../../README.md)

C comment-only templates; not a buildable MCUXpresso/SDK project.

Member 3 will import/create the course-approved board project, add FreeRTOS and board-generated startup/pin/clock files, then include these modules. Do not invent or replace course startup configuration. Record toolchain/SDK versions and actual build steps here once verified.

The proposed ISR name is deliberately not a real vector handler: select the correct port/vector after pin assignment. Use only ISR-safe FreeRTOS calls and the actual port's interrupt-priority rules.

Member 2 owns `sensing/`, Member 1 owns `actuators/dispenser.*`, Member 3 owns `app/`, `control/` and feedback, and Member 4 owns `communication/`. See [system design](../../docs/system-design.md) for interface agreements.
