#ifndef PET_CARE_SECRETS_H
#define PET_CARE_SECRETS_H

// Copy to ignored secrets.h and fill locally. Never commit real credentials.
// Use a ThingsBoard TEST device; host only, without https:// or a path.
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";  // Empty is allowed for an open network.
constexpr char THINGSBOARD_HOST[] = "thingsboard.cloud";  // Match your account region.
constexpr char THINGSBOARD_DEVICE_TOKEN[] = "";

// Paste the trusted root CA PEM from the endpoint's Check connectivity dialog.
// Leave certificate validation enabled. Empty CA disables cloud startup.
constexpr char TLS_CA_CERT[] = R"PEM(
)PEM";

#endif
