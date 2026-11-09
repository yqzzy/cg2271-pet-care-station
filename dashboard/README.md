# ThingsBoard dashboard

[Back to project README](../README.md)

Owner: Member 5, also responsible for [environmental sensing](../firmware/esp32/pet_care_station/environment.cpp) and its task.

Configured on 9 Nov 2026 through the ThingsBoard Cloud UI:
[pet-care-test Dashboard](https://eu.thingsboard.cloud/dashboards/all/2b3024d0-dc17-11f1-bab1-0dcd09e3d48b).
The saved UI export is [pet-care-test-dashboard.json](pet-care-test-dashboard.json).
The Device alias points to the existing `pet-care-test` device; remap it after importing into another tenant.

## Display

The integrated ESP32 now publishes periodic live keys: `temperature_c`, `humidity_pct`, `food_weight_g`, `water_enough`, and `water_low`. Gate environment widgets/alarms with `environment_valid` and food/water displays with `mcu_data_fresh`. These sources fail independently; omitted measurement keys leave historical values in ThingsBoard, so status flags must determine whether a value is shown as current. See the [payload and timing](../firmware/esp32/README.md#payload-and-timing). The water value is Boolean enough/low, not a percentage.

Also gate current displays with device connectivity / time since the latest telemetry (for example, unavailable after 15 seconds without a new five-second update). Source flags and published age values freeze during an internet outage; they must not keep an offline device looking current. Source expiry is reflected at the next publication, rather than exactly at its local threshold.

- Food weight, water level/low-water status and temperature/humidity, with validity and freshness.
- `button_presses` is cumulative since ESP32 boot and remains labelled historical when offline. `last_button_ts` is Unix milliseconds, displayed as local date/time only when a clock-confirmed timestamp exists. It is reconstructed from the boot-relative event time after the ESP32 clock is known.
- Light control switch plus separately displayed confirmed device output state.
- Low water, low food and environmental threshold alerts, with a separate source-unavailable alarm.

The existing environment and food history charts, Boolean water state-history chart, temperature alarm history, theme and alarm tab were preserved. Current cards suppress food/water when `mcu_data_fresh` is false and temperature/humidity when `environment_valid` is false. The dashboard checks telemetry age every second and treats telemetry older than 15 seconds, or a false server `active` attribute, as offline/stale. Historical charts intentionally remain visible. Browser clock accuracy therefore matters. The unavailable banner retains the original green styling, but explicitly says unavailable and hides current measurements.

The separate confirmed-light card displays `light_on` only when online and `light_known` is true; otherwise it says Unknown. `light_pending` displays an acknowledgement-pending message. The legacy **Switch Control** in Monitoring & Trends is labelled as a request, uses `getValue`/`setValue`, sends Boolean parameters and has a 5,000 ms request timeout. Persistent RPC is disabled. A non-Boolean get reply is rejected by the parsing function. An OFF switch or RPC timeout does not establish that the physical light is off; use the confirmed-light card.

Alarm rules were saved under the existing `default` device profile (the only assigned device was `pet-care-test`). ThingsBoard 4.4 exports these rules separately from the dashboard:

| Rule / export | Trigger | Clear |
|---|---|---|
| [PetStationLowFood](alarm-rules/petstationlowfood.json) | `mcu_data_fresh == true && food_weight_g <= 10` | Fresh MCU data and weight `> 10` |
| [PetStationLowWater](alarm-rules/petstationlowwater.json) | Fresh MCU data and `water_low == true` | Fresh MCU data and `water_low == false` |
| [PetStationHighTemperature](alarm-rules/petstationhightemperature.json) | `environment_valid == true && temperature_high == true` | Valid environment data and flag false |
| [PetStationHighHumidity](alarm-rules/petstationhighhumidity.json) | Valid environment data and `humidity_high == true` | Valid environment data and flag false |
| [PetStationSourceUnavailable](alarm-rules/petstationsourceunavailable.json) | `mcu_data_fresh == false` or `environment_valid == false` | Both source flags true |

All five use Critical severity. Invalid source data does not clear an existing threshold alarm. Server alarm evaluation depends on incoming telemetry: the 15-second browser availability gate does not create a server inactivity alarm, and source flags freeze during a network outage. The dashboard shows the outage separately; threshold alarms await valid recovery telemetry. The profile contains shared rules, so review assignment before adding unrelated devices to `default`.

Verification: the saved dashboard was reopened and showed Offline/stale, blank current numeric readings, water Unknown, historical button count and confirmed light Unknown for the disconnected device. UI exports were parsed as JSON and checked for credential keys. The ThingsBoard TBEL test dialog returned `true` for a valid/high humidity input and `false` for the same high input with invalid environment data. Native screenshots were captured of the saved dashboard and five profile rules. No light set command, synthetic telemetry, or physical hardware test was performed. Real button timestamp, pending-to-confirmed transitions and end-to-end alarm firing/recovery still require the board online.

## Cloud contract

ESP32 uses Wi-Fi and MQTT over TLS. Select and record the ThingsBoard endpoint, MQTT library and supported telemetry/command APIs during implementation. Keep access tokens and Wi-Fi credentials in ignored local configuration.

Route light commands through the UART bridge and show pending until the matching device result arrives. Do not display a requested state as confirmed. During disconnection, mark stale data; local control continues. Do not replay old actuator actions on reconnect.

## Bring-up

1. Connect a test device and publish clearly labelled sample readings.
2. Add live-value widgets, history, alerts and the light switch.
3. Test pending/result handling with simulated replies.
4. Connect real board data and run the [test checklist](../tests/README.md).
5. Export dashboard configuration without device credentials; record setup steps here.

Dashboard alerts are the initial notification channel. Email/push notifications are not yet promised.
