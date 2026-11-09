#include "cloud_client.h"

#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>

#include "cloud_policy.h"
#include "live_telemetry.h"
#include "environment.h"
#include "uart_bridge.h"
#include "rpc_protocol.h"
#include "rpc_session.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {
constexpr uint16_t kMqttPort = 8883;
constexpr uint32_t kWifiAttemptMs = 10000;
constexpr uint32_t kTimeAttemptMs = 10000;
constexpr uint32_t kTcpTimeoutMs = 3000;
constexpr time_t kMinimumTime = 1735689600;  // 2025-01-01; reject an unset clock.
constexpr char kTelemetryTopic[] = "v1/devices/me/telemetry";
constexpr char kRpcRequests[] = "v1/devices/me/rpc/request/+";
NetworkClientSecure tls;
PubSubClient mqtt(tls);
CloudPolicy policy;
PeriodicTelemetry telemetry_timer;
McuTelemetry published_mcu;  // MCU readings in the last successful telemetry publish
PeriodicTelemetry control_timer;
bool control_published = false;
uint32_t published_revision = 0;
RpcSession rpc_session;
bool initialized = false;
char client_id[40];

void ForgetSession() {
  rpc_session.Reset();
  UartBridge_CancelQueuedLight();
  LightOutcome discarded;
  UartBridge_TakeLightOutcome(&discarded);
}

// Wi-Fi events run on the network event task. Only print event metadata here;
// do not change connection state or access application globals from the callback.
void LogWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[cloud] Wi-Fi associated with hotspot; waiting for IP address");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("[cloud] Wi-Fi obtained IP address");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[cloud] Wi-Fi disconnected: reason=%u\n",
                    static_cast<unsigned>(info.wifi_sta_disconnected.reason));
      break;
    default:
      break;
  }
}

void Retry(const char *stage) {
  tls.stop();
  ForgetSession();
  policy.Failed(millis());  // Measure backoff AFTER the failed blocking operation.
  Serial.printf("[cloud] %s failed; retry scheduled (passwords/tokens hidden)\n", stage);
}

bool Reply(const char *id, const char *body) {
  // Own storage: PubSubClient's callback topic/payload share its TX buffer.
  char topic[64];
  snprintf(topic, sizeof(topic), "v1/devices/me/rpc/response/%s", id);
  if (mqtt.publish(topic, body, false)) return true;
  Retry("RPC response publish");
  return false;
}

void OnMqttMessage(char *topic, byte *payload, unsigned int length) {
  RpcRequest request;
  const RpcParseResult result = ParseRpcRequest(topic, payload, length, &request);
  if (result == kRpcBadTopic) return;
  if (result != kRpcValid) {
    Reply(request.id, result == kRpcUnsupportedMethod
          ? "{\"error\":\"unsupported_method\"}" : "{\"error\":\"invalid_request\"}");
    return;
  }
  if (!request.set) {
    const ControlSnapshot state = UartBridge_ControlSnapshot();
    Reply(request.id, state.light_known ? (state.light_on ? "true" : "false")
                                      : "{\"error\":\"light_state_unknown\"}");
    return;
  }
  if (rpc_session.InFlight(request.id)) return; // Wait for original outcome.
  if (rpc_session.Cached(request.id)) {
    Reply(request.id, rpc_session.Response());
    return;
  }
  uint32_t token;
  if (rpc_session.Pending() || !UartBridge_RequestLight(request.value, &token)) {
    Reply(request.id, "{\"error\":\"bridge_unavailable_or_busy\"}");
    return;
  }
  rpc_session.Begin(request.id, token);
  // Reply only after the UART owner consumes LIGHT_ACK (or reports failure).
}

bool ServiceControl() {
  LightOutcome outcome;
  if (UartBridge_TakeLightOutcome(&outcome) &&
      rpc_session.Complete(outcome.token, outcome.success, outcome.on)) {
    if (!Reply(rpc_session.Id(), rpc_session.Response())) return false;
  }
  const ControlSnapshot state = UartBridge_ControlSnapshot();
  const uint32_t now = millis();
  if (!control_published || state.revision != published_revision || control_timer.Due(now)) {
    char payload[384];
    if (!BuildControlTelemetry(payload, sizeof(payload), state, now, time(nullptr))) {
      Serial.println("[cloud] Control telemetry serialization failed");
      return true;
    }
    if (!mqtt.publish(kTelemetryTopic, payload, false)) {
      Retry("control telemetry publish");
      return false;
    }
    // Absolute count stays in the bridge. Failed sends never consume presses.
    published_revision = state.revision;
    control_published = true;
    control_timer.Attempted(now);
    Serial.printf("[cloud] Control telemetry sent (QoS 0): %s\n", payload);
  }
  return true;
}
}  // namespace

bool Cloud_Init(void) {
  if (initialized) return true;
  if (!WIFI_SSID[0] || !THINGSBOARD_HOST[0] ||
      !THINGSBOARD_DEVICE_TOKEN[0] ||
      strstr(THINGSBOARD_HOST, "://") || strchr(THINGSBOARD_HOST, '/') ||
      !strstr(TLS_CA_CERT, "-----BEGIN CERTIFICATE-----") ||
      !strstr(TLS_CA_CERT, "-----END CERTIFICATE-----")) {
    Serial.println("[cloud] Disabled: copy secrets.example.h to secrets.h and set SSID, host, device token and root CA");
    return false;
  }
  tls.setCACert(TLS_CA_CERT);
  tls.setHandshakeTimeout(5);  // Seconds; TCP connect below uses milliseconds.
  mqtt.setServer(THINGSBOARD_HOST, kMqttPort);
  mqtt.setCallback(OnMqttMessage);
  mqtt.setSocketTimeout(2);   // Seconds waiting for MQTT protocol data.
  mqtt.setKeepAlive(30);
  if (!mqtt.setBufferSize(1024)) {
    Serial.println("[cloud] Disabled: MQTT buffer allocation failed");
    return false;
  }
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(client_id, sizeof(client_id), "pet-care-%08lx%08lx",
           static_cast<unsigned long>(mac >> 32),
           static_cast<unsigned long>(mac & 0xffffffff));
  initialized = true;
  return true;
}

void Cloud_Task(void *argument) {
  (void)argument;
  if (!initialized) {
    vTaskDelete(nullptr);
    return;
  }
  WiFi.onEvent(LogWifiEvent);
  Serial.printf("[cloud] Wi-Fi diagnostics enabled; firmware built %s %s\n", __DATE__, __TIME__);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // This task owns the timed retry policy.
  bool wifi_attempt = false;
  bool time_attempt = false;
  uint32_t wifi_started_ms = 0;
  uint32_t time_started_ms = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));  // Yield even when config/server is broken.
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      tls.stop();
      ForgetSession();
      time_attempt = false;
      if (!wifi_attempt && policy.Ready(now)) {
        Serial.println("[cloud] Starting Wi-Fi attempt");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifi_started_ms = millis();
        wifi_attempt = true;
      } else if (wifi_attempt && uint32_t(now - wifi_started_ms) >= kWifiAttemptMs) {
        // Log BEFORE the deliberate disconnect, which may generate its own
        // disconnect event. The preceding events reveal the original failure.
        Serial.printf("[cloud] Wi-Fi attempt timed out: status=%d elapsed_ms=%lu; requesting disconnect\n",
                      static_cast<int>(WiFi.status()),
                      static_cast<unsigned long>(now - wifi_started_ms));
        WiFi.disconnect();
        wifi_attempt = false;
        Retry("Wi-Fi");
      }
      continue;
    }
    wifi_attempt = false;

    // TLS certificate date validation requires real time. No blocking NTP wait.
    if (time(nullptr) < kMinimumTime) {
      if (!time_attempt && policy.Ready(now)) {
        Serial.println("[cloud] Waiting for SNTP time");
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        time_started_ms = millis();
        time_attempt = true;
      } else if (time_attempt && uint32_t(now - time_started_ms) >= kTimeAttemptMs) {
        time_attempt = false;
        Retry("time synchronization");
      }
      continue;
    }
    time_attempt = false;

    if (!mqtt.connected()) {
      ForgetSession();
      if (!policy.Ready(now)) continue;
      // Preconnect with a finite TCP timeout. PubSubClient reuses this socket.
      // Hostname is preserved for SNI/hostname certificate verification.
      tls.stop();
      if (!tls.connect(THINGSBOARD_HOST, kMqttPort, kTcpTimeoutMs) ||
          !mqtt.connect(client_id, THINGSBOARD_DEVICE_TOKEN, nullptr)) {
        Serial.printf("[cloud] MQTT connection state=%d\n", mqtt.state());
        Retry("TLS/MQTT");
        continue;
      }
      if (!mqtt.subscribe(kRpcRequests, 1)) {
        Retry("RPC subscription");
        continue;
      }
      policy.Connected();
      telemetry_timer.Reconnected();
      control_published = false;
      control_timer.Reconnected();
      Serial.println("[cloud] MQTT connected with certificate validation");
      Serial.println("[cloud] Subscribed to light RPC requests");
    }

    if (!mqtt.loop()) {
      Retry("MQTT service");
      continue;
    }
    // Publish every 5 s, and early (at most once a second) when food or water changes
    // noticeably, so the dashboard follows the bowl within about 1-2 s.
    McuTelemetry mcu;
    UartBridge_CopySensors(&mcu);
    const bool early = telemetry_timer.EarlyAllowed(millis()) && McuChangedNotably(published_mcu, mcu);
    if (telemetry_timer.Due(millis()) || early) {
      EnvironmentReading environment;
      Environment_CopySnapshot(&environment);
      const uint32_t sampled_at = millis();
      telemetry_timer.Attempted(sampled_at);
      char payload[512];
      if (!BuildLiveTelemetry(payload, sizeof(payload), environment, mcu, sampled_at)) {
        Serial.println("[cloud] Live telemetry serialization failed; no partial JSON sent");
      } else if (mqtt.publish(kTelemetryTopic, payload, false)) {
        published_mcu = mcu;
        Serial.printf("[cloud] Live telemetry sent%s (QoS 0): %s\n", early ? " early (MCU change)" : "", payload);
      } else {
        Retry("live telemetry publish");
        continue;
      }
    }
    // Refresh source validity first on reconnect, before control-only telemetry
    // can cause profile alarm evaluation against cached sensor values.
    if (!ServiceControl()) continue;
    // Reconnection sends current snapshots, not buffered historical readings.
  }
}
