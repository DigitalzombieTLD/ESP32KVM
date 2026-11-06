#include "kvm_gpio.h"

#include <driver/gpio.h>

enum PinClass { PIN_OK, PIN_CAUTION, PIN_BLOCKED, PIN_INVALID };
enum PinMode_ { M_NONE, M_IN, M_IN_PULLUP, M_IN_PULLDOWN, M_OUT };

static const int MAX_PIN = 48;
static bool enabled[MAX_PIN + 1];
static PinMode_ modes[MAX_PIN + 1];

static PinClass classify(int pin, const char **note) {
  *note = "";
  if (pin < 0 || pin > MAX_PIN || (pin >= 22 && pin <= 25)) {
    *note = "does not exist on ESP32-S3";
    return PIN_INVALID;
  }
  if (pin == 19 || pin == 20) { *note = "USB D-/D+ (HID link to host)"; return PIN_BLOCKED; }
  if (pin >= 26 && pin <= 32) { *note = "SPI flash / PSRAM"; return PIN_BLOCKED; }
  if (pin >= 33 && pin <= 37) { *note = "octal PSRAM / flash (N16R8 modules)"; return PIN_BLOCKED; }
  if (pin == 43 || pin == 44) { *note = "UART0 console TX/RX"; return PIN_BLOCKED; }
  if (pin == 0) { *note = "strapping pin (boot mode, BOOT button)"; return PIN_CAUTION; }
  if (pin == 3) { *note = "strapping pin (JTAG source)"; return PIN_CAUTION; }
  if (pin == 45) { *note = "strapping pin (VDD_SPI voltage)"; return PIN_CAUTION; }
  if (pin == 46) { *note = "strapping pin (boot log / ROM messages)"; return PIN_CAUTION; }
  if (pin == 48) { *note = "onboard RGB LED on some devkits"; return PIN_CAUTION; }
  return PIN_OK;
}

static const char *modeName(PinMode_ m) {
  switch (m) {
    case M_IN: return "input";
    case M_IN_PULLUP: return "input pullup";
    case M_IN_PULLDOWN: return "input pulldown";
    case M_OUT: return "output";
    default: return "-";
  }
}

void gpioInit() {
  memset(enabled, 0, sizeof(enabled));
  memset(modes, 0, sizeof(modes));
}

static bool check(int pin, String &msg, bool needEnabled) {
  const char *note;
  PinClass c = classify(pin, &note);
  if (c == PIN_INVALID || c == PIN_BLOCKED) {
    msg = "GPIO" + String(pin) + " refused: " + note;
    return false;
  }
  if (needEnabled && !enabled[pin]) {
    msg = "GPIO" + String(pin) + " is not enabled (use: gpio enable " + String(pin) + ")";
    return false;
  }
  return true;
}

bool gpioEnable(int pin, String &msg) {
  if (!check(pin, msg, false)) return false;
  const char *note;
  PinClass c = classify(pin, &note);
  if (!enabled[pin]) {
    enabled[pin] = true;
    modes[pin] = M_IN;
    pinMode(pin, INPUT);
  }
  msg = "GPIO" + String(pin) + " enabled as input";
  if (c == PIN_CAUTION) msg += String(" (caution: ") + note + ")";
  return true;
}

bool gpioDisable(int pin, String &msg) {
  const char *note;
  PinClass c = classify(pin, &note);
  if (c == PIN_INVALID || c == PIN_BLOCKED) {
    msg = "GPIO" + String(pin) + " refused: " + note;
    return false;
  }
  if (enabled[pin]) gpio_reset_pin((gpio_num_t)pin);
  enabled[pin] = false;
  modes[pin] = M_NONE;
  msg = "GPIO" + String(pin) + " disabled (reset to default state)";
  return true;
}

bool gpioMode(int pin, const String &mode, String &msg) {
  if (!check(pin, msg, true)) return false;
  String m = mode;
  m.toLowerCase();
  if (m == "in" || m == "input") { pinMode(pin, INPUT); modes[pin] = M_IN; }
  else if (m == "in_pullup" || m == "pullup") { pinMode(pin, INPUT_PULLUP); modes[pin] = M_IN_PULLUP; }
  else if (m == "in_pulldown" || m == "pulldown") { pinMode(pin, INPUT_PULLDOWN); modes[pin] = M_IN_PULLDOWN; }
  else if (m == "out" || m == "output") {
    digitalWrite(pin, LOW);  // defined level before the driver is enabled
    pinMode(pin, OUTPUT);
    modes[pin] = M_OUT;
  } else {
    msg = "unknown mode '" + mode + "' (in, in_pullup, in_pulldown, out)";
    return false;
  }
  msg = "GPIO" + String(pin) + " set to " + modeName(modes[pin]);
  return true;
}

bool gpioWrite(int pin, const String &value, String &msg) {
  if (!check(pin, msg, true)) return false;
  if (modes[pin] != M_OUT) {
    msg = "GPIO" + String(pin) + " is not an output (use: gpio mode " + String(pin) + " out)";
    return false;
  }
  String v = value;
  v.toLowerCase();
  int level;
  if (v == "1" || v == "high" || v == "on") level = HIGH;
  else if (v == "0" || v == "low" || v == "off") level = LOW;
  else if (v == "toggle") level = !digitalRead(pin);
  else { msg = "value must be 0|1|high|low|toggle"; return false; }
  digitalWrite(pin, level);
  msg = "GPIO" + String(pin) + " = " + (level ? "1" : "0");
  return true;
}

bool gpioRead(int pin, int &level, String &msg) {
  if (!check(pin, msg, true)) return false;
  level = digitalRead(pin);
  return true;
}

String gpioDescribe(int pin) {
  const char *note;
  classify(pin, &note);
  String s = "GPIO" + String(pin) + ": ";
  if (!enabled[pin]) return s + "disabled";
  s += String(modeName(modes[pin])) + ", level " + String(digitalRead(pin));
  if (*note) s += String("  [") + note + "]";
  return s;
}

void gpioListEnabled(String &out) {
  out = "";
  for (int p = 0; p <= MAX_PIN; p++)
    if (enabled[p]) out += gpioDescribe(p) + "\n";
  if (out.length() == 0) out = "no GPIO enabled\n";
}

void gpioListUsable(String &out) {
  out = "";
  for (int p = 0; p <= MAX_PIN; p++) {
    const char *note;
    PinClass c = classify(p, &note);
    if (c == PIN_INVALID) continue;
    const char *tag = c == PIN_OK ? "ok     " : c == PIN_CAUTION ? "caution" : "BLOCKED";
    out += "GPIO" + String(p) + (p < 10 ? "  " : " ") + tag + " " + note + "\n";
  }
}
