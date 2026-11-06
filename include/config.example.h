// Copy this file to include/config.h and edit. config.h is git-ignored.
// Everything here is only a DEFAULT: values changed at runtime with the shell
// ("set ..." + "save") are stored in NVS and take precedence.
#pragma once

// ---- WiFi (fixed network) ----
#define CFG_WIFI_SSID "YourWiFiSSID"
#define CFG_WIFI_PASSWORD "YourWiFiPassword"

// ---- Network ----
#define CFG_HOSTNAME "esp32kvm"
#define CFG_DHCP true  // false = use the fixed address below
#define CFG_IP "192.168.1.50"
#define CFG_GATEWAY "192.168.1.1"
#define CFG_SUBNET "255.255.255.0"
#define CFG_DNS "192.168.1.1"

// ---- SSH server (password authentication only) ----
#define CFG_SSH_PORT 22
#define CFG_SSH_USER "admin"
#define CFG_SSH_PASSWORD "change-me-please"  // CHANGE THIS

// ---- Keyboard ----
#define CFG_KEY_DELAY_MS 10  // delay between key press/release events
