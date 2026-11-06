#include "settings.h"

#include <Preferences.h>

#include "config.h"

Settings cfg;
bool cfgDirty = false;

static const char *NS = "kvm";

static IPAddress parseIp(const char *s) {
  IPAddress a;
  a.fromString(s);
  return a;
}

void settingsLoad() {
  Preferences p;
  p.begin(NS, true);
  cfg.hostname = p.getString("host", CFG_HOSTNAME);
  cfg.dhcp = p.getBool("dhcp", CFG_DHCP);
  cfg.ip = IPAddress(p.getUInt("ip", (uint32_t)parseIp(CFG_IP)));
  cfg.gateway = IPAddress(p.getUInt("gw", (uint32_t)parseIp(CFG_GATEWAY)));
  cfg.subnet = IPAddress(p.getUInt("mask", (uint32_t)parseIp(CFG_SUBNET)));
  cfg.dns = IPAddress(p.getUInt("dns", (uint32_t)parseIp(CFG_DNS)));
  cfg.wifiSsid = p.getString("ssid", CFG_WIFI_SSID);
  cfg.wifiPass = p.getString("wpass", CFG_WIFI_PASSWORD);
  cfg.sshUser = p.getString("suser", CFG_SSH_USER);
  cfg.sshPass = p.getString("spass", CFG_SSH_PASSWORD);
  cfg.sshPort = p.getUShort("sport", CFG_SSH_PORT);
  cfg.keyDelayMs = p.getUShort("kdelay", CFG_KEY_DELAY_MS);
  p.end();
  cfgDirty = false;
}

void settingsSave() {
  Preferences p;
  p.begin(NS, false);
  p.putString("host", cfg.hostname);
  p.putBool("dhcp", cfg.dhcp);
  p.putUInt("ip", (uint32_t)cfg.ip);
  p.putUInt("gw", (uint32_t)cfg.gateway);
  p.putUInt("mask", (uint32_t)cfg.subnet);
  p.putUInt("dns", (uint32_t)cfg.dns);
  p.putString("ssid", cfg.wifiSsid);
  p.putString("wpass", cfg.wifiPass);
  p.putString("suser", cfg.sshUser);
  p.putString("spass", cfg.sshPass);
  p.putUShort("sport", cfg.sshPort);
  p.putUShort("kdelay", cfg.keyDelayMs);
  p.end();
  cfgDirty = false;
}

void settingsFactoryReset() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

bool isValidHostname(const String &h) {
  if (h.length() < 1 || h.length() > 32) return false;
  if (h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (size_t i = 0; i < h.length(); i++) {
    char c = h[i];
    if (!isalnum((unsigned char)c) && c != '-') return false;
  }
  return true;
}

bool usingDefaultSshPassword() { return cfg.sshPass == CFG_SSH_PASSWORD; }
