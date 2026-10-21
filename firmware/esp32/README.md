# ESP32 firmware

[Back to project README](../../README.md)

Arduino-style C++ comment-only templates; the sketch folder and sketch share the name `pet_care_station`. There is no working sketch yet.

Members 4 and 5 will agree on the actual ESP32 board, Arduino core version, temperature/humidity module library and MQTT library. Record pinned versions and setup/build instructions here after verification. The module may not use I2C; identify it before wiring.

Member 4 owns `environment.*` and `uart_bridge.*`; Member 5 owns `cloud_client.*` and local credential setup. Both review sketch startup/task creation. Copy `secrets.example.h` to ignored `secrets.h` when adding configuration; never commit credentials.

Keep network reconnection separate from environmental sampling and UART handling. See [dashboard requirements](../../dashboard/README.md).
