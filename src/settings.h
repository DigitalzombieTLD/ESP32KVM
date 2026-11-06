#pragma once
#include <Arduino.h>
#include <IPAddress.h>

struct Settings {
  String hostname;
  bool dhcp;
  IPAddress ip, gateway, subnet, dns;
  String wifiSsid, wifiPass;
  String sshUser, sshPass;
  uint16_t sshPort;
  uint16_t keyDelayMs;
};

extern Settings cfg;       // runtime settings (RAM)
extern bool cfgDirty;      // true if RAM differs from NVS

void settingsLoad();       // build-time defaults, overridden by NVS
void settingsSave();
void settingsFactoryReset();  // erase NVS namespace

bool isValidHostname(const String &h);
bool usingDefaultSshPassword();
