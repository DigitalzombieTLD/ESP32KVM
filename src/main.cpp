#include <Arduino.h>
#include <WiFi.h>

#include "kvm_gpio.h"
#include "kvm_keyboard.h"
#include "settings.h"
#include "ssh_server.h"

static void wifiStart() {
  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (cfg.dhcp) {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  } else if (!WiFi.config(cfg.ip, cfg.gateway, cfg.subnet, cfg.dns)) {
    Serial.println("[wifi] fixed IP configuration failed, falling back to DHCP");
  }
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.length() ? cfg.wifiPass.c_str() : nullptr);
  Serial.printf("[wifi] connecting to '%s' as '%s' (%s)\n", cfg.wifiSsid.c_str(),
                cfg.hostname.c_str(), cfg.dhcp ? "DHCP" : "fixed IP");
}

void setup() {
  Serial.begin(115200);  // UART0 (GPIO43/44); native USB is the HID keyboard
  delay(200);
  Serial.println("\nESP32KVM starting");
  settingsLoad();
  gpioInit();
  kbdBegin();
  wifiStart();
  sshServerStart();
}

void loop() {
  static bool wasUp = false;
  static unsigned long downSince = 0;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wasUp) Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  if (!up) {
    if (wasUp || downSince == 0) downSince = millis();
    if (millis() - downSince > 30000) {  // nudge the driver if auto-reconnect stalls
      WiFi.reconnect();
      downSince = millis();
    }
  }
  wasUp = up;
  delay(1000);
}
