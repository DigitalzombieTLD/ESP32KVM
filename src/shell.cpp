#include "shell.h"

#include <WiFi.h>

#include "kvm_gpio.h"
#include "kvm_keyboard.h"
#include "settings.h"

#define FW_VERSION "1.0.0"

static const char *HELP_TEXT =
    "Available commands:\n"
    "\n"
    "Keyboard (USB HID to the host):\n"
    "  type <text>            type text (no Enter)\n"
    "  typeln <text>          type text followed by Enter\n"
    "  key <combo> [count]    press a key/combo, e.g. key ctrl+alt+del, key shift+f10, key a\n"
    "  enter esc backspace tab space delete insert home end pgup pgdn\n"
    "  up down left right     single keys (optional repeat count, e.g. 'down 3')\n"
    "  f1 .. f12              function keys\n"
    "  ctrlaltdel             send Ctrl+Alt+Del\n"
    "  ctrlc                  send Ctrl+C\n"
    "\n"
    "GPIO:\n"
    "  gpio list              show all usable pins and their safety class\n"
    "  gpio enable <pin>      enable a pin (starts as plain input)\n"
    "  gpio disable <pin>     disable a pin and reset it\n"
    "  gpio mode <pin> <in|in_pullup|in_pulldown|out>\n"
    "  gpio write <pin> <0|1|high|low|toggle>\n"
    "  gpio read <pin|all>    read one pin or all enabled pins\n"
    "\n"
    "Network / configuration:\n"
    "  status | ifconfig      show network, USB and system status\n"
    "  show config            show settings (RAM vs. saved note, no secrets)\n"
    "  set hostname <name>\n"
    "  set dhcp <on|off>\n"
    "  set ip <ip> [gateway] [subnet] [dns]   fixed address (used when dhcp is off)\n"
    "  set gateway|subnet|dns <addr>\n"
    "  set wifi <ssid> [password]             password is prompted if omitted\n"
    "  set user <name>        SSH login name\n"
    "  set password [new]     SSH password (prompted, hidden, if omitted)\n"
    "  set port <1-65535>     SSH port (applies after reboot)\n"
    "  set keydelay <ms>      delay between key press/release (0-500)\n"
    "  save                   store the settings in NVS (flash)\n"
    "  factoryreset           erase saved settings (back to build-time defaults)\n"
    "  reboot [force]         restart the ESP32 (applies network changes)\n"
    "\n"
    "Misc:\n"
    "  help | ?               this list\n"
    "  exit | quit | logout   close the SSH session\n";

void shellBanner(Term &t) {
  t.print(
      "\n"
      "  _____ ____  ____ _________  _  ____     ____  __\n"
      " | ____/ ___||  _ \\___ /___ \\| |/ /\\ \\   / /  \\/  |\n"
      " |  _| \\___ \\| |_) ||_ \\ __) | ' /  \\ \\ / /| |\\/| |\n"
      " | |___ ___) |  __/___) / __/| . \\   \\ V / | |  | |\n"
      " |_____|____/|_|  |____/_____|_|\\_\\   \\_/  |_|  |_|\n"
      "\n");
  t.printf(" ESP32KVM v%s - remote USB keyboard over SSH\n", FW_VERSION);
  t.printf(" Host: %s   IP: %s   USB HID: %s\n", cfg.hostname.c_str(),
           WiFi.localIP().toString().c_str(), kbdReady() ? "ready" : "not connected");
  if (usingDefaultSshPassword())
    t.print(" WARNING: the default SSH password is still in use! Run: set password, then save\n");
  if (cfgDirty) t.print(" Note: there are unsaved settings. Run: save\n");
  t.print(" Type 'help' for a list of commands.\n\n");
}

// ---- helpers ----------------------------------------------------------------

static int tokenize(const String &line, String *tok, int max) {
  int n = 0;
  size_t i = 0, len = line.length();
  while (i < len && n < max) {
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (i >= len) break;
    String s;
    if (line[i] == '"') {
      i++;
      while (i < len && line[i] != '"') s += line[i++];
      i++;
    } else {
      while (i < len && !isspace((unsigned char)line[i])) s += line[i++];
    }
    tok[n++] = s;
  }
  return n;
}

// Raw text after the first word (not tokenized, spaces preserved).
static String restOfLine(const String &line) {
  int i = 0, len = line.length();
  while (i < len && isspace((unsigned char)line[i])) i++;
  while (i < len && !isspace((unsigned char)line[i])) i++;
  if (i < len) i++;  // single separator
  return line.substring(i);
}

static bool parseInt(const String &s, long &v, long lo, long hi) {
  if (s.length() == 0) return false;
  char *end;
  v = strtol(s.c_str(), &end, 0);
  return *end == '\0' && v >= lo && v <= hi;
}

static bool parseIp(const String &s, IPAddress &ip) {
  return ip.fromString(s) && s.indexOf('.') > 0;
}

static void markDirty(Term &t) {
  cfgDirty = true;
  t.print("OK (not saved yet - use 'save'; network changes apply after reboot)\n");
}

static bool readSecret(Term &t, const char *prompt, String &out) {
  bool eof;
  return t.readLine(out, prompt, false, false, eof);
}

// ---- command groups ---------------------------------------------------------

static void cmdStatus(Term &t) {
  bool up = WiFi.status() == WL_CONNECTED;
  t.printf("Hostname   : %s\n", cfg.hostname.c_str());
  t.printf("WiFi       : %s", up ? "connected" : "DISCONNECTED");
  if (up) t.printf(" to '%s' (RSSI %d dBm, ch %d)", WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.channel());
  t.print("\n");
  t.printf("MAC        : %s\n", WiFi.macAddress().c_str());
  t.printf("Addressing : %s (configured)\n", cfg.dhcp ? "DHCP" : "fixed IP");
  t.printf("IP address : %s\n", WiFi.localIP().toString().c_str());
  t.printf("Netmask    : %s\n", WiFi.subnetMask().toString().c_str());
  t.printf("Gateway    : %s\n", WiFi.gatewayIP().toString().c_str());
  t.printf("DNS        : %s\n", WiFi.dnsIP().toString().c_str());
  t.printf("SSH        : port %u, user '%s'\n", cfg.sshPort, cfg.sshUser.c_str());
  t.printf("USB HID    : %s\n", kbdReady() ? "ready (host connected)" : "not ready (no host / not enumerated)");
  t.printf("Uptime     : %lu s\n", millis() / 1000UL);
  t.printf("Heap free  : %u bytes, PSRAM free: %u bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());
  t.printf("Config     : %s\n", cfgDirty ? "UNSAVED changes pending" : "saved");
}

static void cmdShowConfig(Term &t) {
  t.printf("hostname : %s\n", cfg.hostname.c_str());
  t.printf("dhcp     : %s\n", cfg.dhcp ? "on" : "off");
  t.printf("ip       : %s\n", cfg.ip.toString().c_str());
  t.printf("gateway  : %s\n", cfg.gateway.toString().c_str());
  t.printf("subnet   : %s\n", cfg.subnet.toString().c_str());
  t.printf("dns      : %s\n", cfg.dns.toString().c_str());
  t.printf("wifi ssid: %s\n", cfg.wifiSsid.c_str());
  t.printf("ssh user : %s\n", cfg.sshUser.c_str());
  t.printf("ssh port : %u\n", cfg.sshPort);
  t.printf("keydelay : %u ms\n", cfg.keyDelayMs);
  t.printf("(%s)\n", cfgDirty ? "unsaved changes pending" : "matches saved settings");
}

static void cmdSet(Term &t, const String &line) {
  String tok[8];
  int n = tokenize(line, tok, 8);
  if (n < 2) { t.print("usage: set <hostname|dhcp|ip|gateway|subnet|dns|wifi|user|password|port|keydelay> ...\n"); return; }
  String what = tok[1];
  what.toLowerCase();

  if (what == "hostname" && n == 3) {
    if (!isValidHostname(tok[2])) { t.print("invalid hostname (1-32 chars: letters, digits, '-')\n"); return; }
    cfg.hostname = tok[2];
    markDirty(t);
  } else if (what == "dhcp" && n == 3) {
    String v = tok[2];
    v.toLowerCase();
    if (v == "on" || v == "1" || v == "true") cfg.dhcp = true;
    else if (v == "off" || v == "0" || v == "false") cfg.dhcp = false;
    else { t.print("usage: set dhcp on|off\n"); return; }
    markDirty(t);
    if (!cfg.dhcp)
      t.printf("Fixed IP %s / %s gw %s dns %s will be used after reboot.\n", cfg.ip.toString().c_str(),
               cfg.subnet.toString().c_str(), cfg.gateway.toString().c_str(), cfg.dns.toString().c_str());
  } else if (what == "ip" && n >= 3 && n <= 6) {
    IPAddress a[4];
    for (int i = 2; i < n; i++)
      if (!parseIp(tok[i], a[i - 2])) { t.printf("invalid address '%s'\n", tok[i].c_str()); return; }
    cfg.ip = a[0];
    if (n >= 4) cfg.gateway = a[1];
    if (n >= 5) cfg.subnet = a[2];
    if (n >= 6) cfg.dns = a[3];
    markDirty(t);
    if (cfg.dhcp) t.print("Note: DHCP is still on; run 'set dhcp off' to use the fixed address.\n");
  } else if ((what == "gateway" || what == "subnet" || what == "dns") && n == 3) {
    IPAddress a;
    if (!parseIp(tok[2], a)) { t.printf("invalid address '%s'\n", tok[2].c_str()); return; }
    if (what == "gateway") cfg.gateway = a;
    else if (what == "subnet") cfg.subnet = a;
    else cfg.dns = a;
    markDirty(t);
  } else if (what == "wifi" && (n == 3 || n == 4)) {
    if (tok[2].length() < 1 || tok[2].length() > 32) { t.print("SSID must be 1-32 characters\n"); return; }
    String pw;
    if (n == 4) pw = tok[3];
    else if (!readSecret(t, "WiFi password (empty = open network): ", pw)) return;
    if (pw.length() != 0 && (pw.length() < 8 || pw.length() > 63)) { t.print("WiFi password must be 8-63 characters\n"); return; }
    cfg.wifiSsid = tok[2];
    cfg.wifiPass = pw;
    markDirty(t);
  } else if (what == "user" && n == 3) {
    if (tok[2].length() < 1 || tok[2].length() > 32) { t.print("user name must be 1-32 characters\n"); return; }
    cfg.sshUser = tok[2];
    markDirty(t);
  } else if (what == "password" && n <= 3) {
    String pw, pw2;
    if (n == 3) {
      pw = tok[2];
    } else {
      if (!readSecret(t, "New SSH password: ", pw)) return;
      if (!readSecret(t, "Repeat password: ", pw2)) return;
      if (pw != pw2) { t.print("passwords do not match\n"); return; }
    }
    if (pw.length() < 8 || pw.length() > 64) { t.print("password must be 8-64 characters\n"); return; }
    cfg.sshPass = pw;
    markDirty(t);
  } else if (what == "port" && n == 3) {
    long v;
    if (!parseInt(tok[2], v, 1, 65535)) { t.print("port must be 1-65535\n"); return; }
    cfg.sshPort = (uint16_t)v;
    markDirty(t);
  } else if (what == "keydelay" && n == 3) {
    long v;
    if (!parseInt(tok[2], v, 0, 500)) { t.print("keydelay must be 0-500 ms\n"); return; }
    cfg.keyDelayMs = (uint16_t)v;
    markDirty(t);
  } else {
    t.print("invalid 'set' command, see 'help'\n");
  }
}

static void cmdGpio(Term &t, const String &line) {
  String tok[5];
  int n = tokenize(line, tok, 5);
  if (n < 2) { t.print("usage: gpio <list|enable|disable|mode|write|read> ...\n"); return; }
  String sub = tok[1];
  sub.toLowerCase();
  String msg;
  long pin;

  if (sub == "list") {
    gpioListUsable(msg);
    t.print(msg.c_str());
    t.print("\nEnabled pins:\n");
    gpioListEnabled(msg);
    t.print(msg.c_str());
    return;
  }
  if (sub == "read" && n == 3 && (tok[2] == "all")) {
    gpioListEnabled(msg);
    t.print(msg.c_str());
    return;
  }
  if (n < 3 || !parseInt(tok[2], pin, 0, 255)) { t.print("usage: gpio <enable|disable|read> <pin> | gpio mode <pin> <mode> | gpio write <pin> <value>\n"); return; }

  bool ok;
  if (sub == "enable" && n == 3) ok = gpioEnable(pin, msg);
  else if (sub == "disable" && n == 3) ok = gpioDisable(pin, msg);
  else if (sub == "mode" && n == 4) ok = gpioMode(pin, tok[3], msg);
  else if (sub == "write" && n == 4) ok = gpioWrite(pin, tok[3], msg);
  else if (sub == "read" && n == 3) {
    int level;
    ok = gpioRead(pin, level, msg);
    if (ok) msg = gpioDescribe(pin);
  } else {
    t.print("invalid 'gpio' command, see 'help'\n");
    return;
  }
  t.printf("%s%s\n", ok ? "" : "ERROR: ", msg.c_str());
}

static bool kbdWarn(Term &t) {
  if (!kbdReady()) t.print("Warning: USB HID not ready (host not connected/enumerated?); keys may be dropped.\n");
  return true;
}

static void keyCmd(Term &t, const String &combo, long count) {
  String err;
  kbdWarn(t);
  for (long i = 0; i < count; i++)
    if (!kbdCombo(combo.c_str(), err)) { t.printf("ERROR: %s\n", err.c_str()); return; }
}

// ---- dispatcher -------------------------------------------------------------

ShellResult shellExecute(Term &t, const String &lineIn) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return SHELL_CONTINUE;

  String tok[4];
  int n = tokenize(line, tok, 4);
  String cmd = tok[0];
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") {
    t.print(HELP_TEXT);
  } else if (cmd == "exit" || cmd == "quit" || cmd == "logout") {
    t.print("Bye.\n");
    return SHELL_EXIT;
  } else if (cmd == "status" || cmd == "ifconfig") {
    cmdStatus(t);
  } else if (cmd == "show") {
    if (n == 2 && (tok[1] == "config")) cmdShowConfig(t);
    else t.print("usage: show config\n");
  } else if (cmd == "set") {
    cmdSet(t, line);
  } else if (cmd == "save") {
    settingsSave();
    t.print("Settings saved to NVS.\n");
  } else if (cmd == "factoryreset") {
    settingsFactoryReset();
    t.print("Saved settings erased. Reboot to load build-time defaults.\n");
  } else if (cmd == "reboot") {
    if (cfgDirty && !(n == 2 && tok[1] == "force")) {
      t.print("Unsaved changes would be lost. Run 'save' first, or 'reboot force'.\n");
      return SHELL_CONTINUE;
    }
    t.print("Rebooting...\n");
    delay(300);
    ESP.restart();
  } else if (cmd == "gpio") {
    cmdGpio(t, line);
  } else if (cmd == "type" || cmd == "typeln") {
    String text = restOfLine(line);
    if (cmd == "type" && text.length() == 0) { t.print("usage: type <text>\n"); return SHELL_CONTINUE; }
    kbdWarn(t);
    kbdType(text.c_str(), cmd == "typeln");
  } else if (cmd == "key") {
    long count = 1;
    if (n < 2 || (n == 3 && !parseInt(tok[2], count, 1, 100)) || n > 3) {
      t.print("usage: key <combo> [count]   e.g. key ctrl+alt+del\n");
      return SHELL_CONTINUE;
    }
    keyCmd(t, tok[1], count);
  } else if (cmd == "ctrlaltdel") {
    kbdWarn(t);
    kbdCtrlAltDel();
  } else if (cmd == "ctrlc") {
    kbdWarn(t);
    kbdCtrlC();
  } else {
    static const char *SIMPLE[] = {"enter", "esc", "backspace", "tab", "space", "delete", "insert", "home", "end",
                                   "pgup", "pgdn", "up", "down", "left", "right", "f1", "f2", "f3", "f4",
                                   "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12"};
    for (const char *name : SIMPLE) {
      if (cmd == name) {
        long count = 1;
        if (n == 2 && !parseInt(tok[1], count, 1, 100)) { t.printf("usage: %s [count 1-100]\n", name); return SHELL_CONTINUE; }
        if (n > 2) { t.printf("usage: %s [count 1-100]\n", name); return SHELL_CONTINUE; }
        keyCmd(t, cmd, count);
        return SHELL_CONTINUE;
      }
    }
    t.printf("Unknown command '%s'. Type 'help'.\n", cmd.c_str());
  }
  return SHELL_CONTINUE;
}

void shellRun(Term &t) {
  shellBanner(t);
  while (true) {
    String line;
    bool eof;
    if (!t.readLine(line, "kvm> ", true, true, eof)) return;
    if (shellExecute(t, line) == SHELL_EXIT) return;
  }
}
