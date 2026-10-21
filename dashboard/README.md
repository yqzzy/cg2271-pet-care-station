# ThingsBoard dashboard

[Back to project README](../README.md)

Owner: Member 5. Create exports here after configuration; no dashboard export exists yet.

## Display

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
