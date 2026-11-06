# ThingsBoard dashboard

[Back to project README](../README.md)

Owner: Member 5, also responsible for [environmental sensing](../firmware/esp32/pet_care_station/environment.cpp) and its task. Create exports here after configuration; no dashboard export exists yet.

## Display

The integrated ESP32 now publishes periodic live keys: `temperature_c`, `humidity_pct`, `food_weight_g`, `water_enough`, and `water_low`. Gate environment widgets/alarms with `environment_valid` and food/water displays with `mcu_data_fresh`. These sources fail independently; omitted measurement keys leave historical values in ThingsBoard, so status flags must determine whether a value is shown as current. See the [payload and timing](../firmware/esp32/README.md#payload-and-timing). The water value is Boolean enough/low, not a percentage.

Also gate current displays with device connectivity / time since the latest telemetry (for example, unavailable after 15 seconds without a new five-second update). Source flags and published age values freeze during an internet outage; they must not keep an offline device looking current. Source expiry is reflected at the next publication, rather than exactly at its local threshold.

- Food weight, water level/low-water status and temperature/humidity, with validity and freshness.
- Confirmed pet-button events and dispensing status/history.
- Light control switch plus separately displayed confirmed device output state.
- Low water, low food and environmental threshold alerts; thresholds are configurable TODOs.

## Cloud contract

ESP32 uses Wi-Fi and MQTT over TLS. Select and record the ThingsBoard endpoint, MQTT library and supported telemetry/command APIs during implementation. Keep access tokens and Wi-Fi credentials in ignored local configuration.

Route light commands through the UART bridge and show pending until the matching device result arrives. Do not display a requested state as confirmed. During disconnection, mark stale data; local control continues. Choose bounded event buffering and reconnect backoff before implementation; do not replay old actuator actions on reconnect.

## Bring-up

1. Connect a test device and publish clearly labelled sample readings.
2. Add live-value widgets, history, alerts and the light switch.
3. Test pending/result handling with simulated replies.
4. Connect real board data and run the [test checklist](../tests/README.md).
5. Export dashboard configuration without device credentials; record setup steps here.

Dashboard alerts are the initial notification channel. Email/push notifications are not yet promised.
