#include "kvm_keyboard.h"

#include <USB.h>
#include <USBHIDKeyboard.h>
#include "tusb.h"

#include "settings.h"

static USBHIDKeyboard Keyboard;

struct KeyName {
  const char *name;
  uint8_t code;
};

static const KeyName MODIFIERS[] = {
    {"ctrl", KEY_LEFT_CTRL},   {"control", KEY_LEFT_CTRL}, {"shift", KEY_LEFT_SHIFT},
    {"alt", KEY_LEFT_ALT},     {"altgr", KEY_RIGHT_ALT},   {"gui", KEY_LEFT_GUI},
    {"win", KEY_LEFT_GUI},     {"super", KEY_LEFT_GUI},    {"meta", KEY_LEFT_GUI},
    {"cmd", KEY_LEFT_GUI},     {"rctrl", KEY_RIGHT_CTRL},  {"rshift", KEY_RIGHT_SHIFT},
};

static const KeyName SPECIAL[] = {
    {"esc", KEY_ESC},          {"escape", KEY_ESC},
    {"enter", KEY_RETURN},     {"return", KEY_RETURN},
    {"backspace", KEY_BACKSPACE}, {"bs", KEY_BACKSPACE},
    {"tab", KEY_TAB},          {"space", ' '},
    {"delete", KEY_DELETE},    {"del", KEY_DELETE},
    {"insert", KEY_INSERT},    {"ins", KEY_INSERT},
    {"home", KEY_HOME},        {"end", KEY_END},
    {"pageup", KEY_PAGE_UP},   {"pgup", KEY_PAGE_UP},
    {"pagedown", KEY_PAGE_DOWN}, {"pgdn", KEY_PAGE_DOWN},
    {"up", KEY_UP_ARROW},      {"down", KEY_DOWN_ARROW},
    {"left", KEY_LEFT_ARROW},  {"right", KEY_RIGHT_ARROW},
    {"capslock", KEY_CAPS_LOCK}, {"printscreen", KEY_PRINT_SCREEN},
    {"prtsc", KEY_PRINT_SCREEN}, {"scrolllock", KEY_SCROLL_LOCK},
    {"pause", KEY_PAUSE},
};

static const uint8_t FKEYS[12] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4,  KEY_F5,  KEY_F6,
                                  KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12};

void kbdBegin() {
  Keyboard.begin();
  USB.productName("ESP32KVM Keyboard");
  USB.manufacturerName("ESP32KVM");
  USB.begin();
}

bool kbdReady() { return tud_mounted() && !tud_suspended(); }

static bool lookup(const char *tok, uint8_t &code, bool &isModifier) {
  String t(tok);
  t.toLowerCase();
  for (auto &m : MODIFIERS)
    if (t == m.name) { code = m.code; isModifier = true; return true; }
  isModifier = false;
  for (auto &s : SPECIAL)
    if (t == s.name) { code = s.code; return true; }
  if (t.length() >= 2 && t[0] == 'f') {
    int n = t.substring(1).toInt();
    if (n >= 1 && n <= 12 && String(n) == t.substring(1)) { code = FKEYS[n - 1]; return true; }
  }
  if (t.length() == 1 && (unsigned char)t[0] >= 0x20 && (unsigned char)t[0] < 0x7f) {
    code = (uint8_t)t[0];
    return true;
  }
  return false;
}

static void tap(uint8_t code) {
  Keyboard.press(code);
  delay(cfg.keyDelayMs);
  Keyboard.release(code);
  delay(cfg.keyDelayMs);
}

void kbdType(const char *text, bool enter) {
  for (const char *p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c >= 0x7f) continue;  // printable ASCII only
    tap(c);
  }
  if (enter) tap(KEY_RETURN);
}

bool kbdKey(const char *name, String &err) {
  uint8_t code;
  bool mod;
  if (!lookup(name, code, mod)) {
    err = String("unknown key '") + name + "'";
    return false;
  }
  tap(code);
  return true;
}

bool kbdCombo(const char *combo, String &err) {
  uint8_t codes[6];
  int n = 0;
  String s(combo);
  int start = 0;
  while (start <= (int)s.length()) {
    int plus = s.indexOf('+', start);
    String tok;
    if (plus < 0) {
      tok = s.substring(start);
      start = s.length() + 1;
    } else if (plus == start && start < (int)s.length()) {
      tok = "+";  // literal plus key, e.g. "ctrl++"
      plus = start;
      start = plus + 2;
    } else {
      tok = s.substring(start, plus);
      start = plus + 1;
    }
    if (tok.length() == 0) { err = "empty key in combo"; return false; }
    uint8_t c;
    bool mod;
    if (!lookup(tok.c_str(), c, mod)) { err = "unknown key '" + tok + "'"; return false; }
    if (n >= 6) { err = "too many keys in combo (max 6)"; return false; }
    codes[n++] = c;
  }
  for (int i = 0; i < n; i++) {
    Keyboard.press(codes[i]);
    delay(cfg.keyDelayMs);
  }
  delay(cfg.keyDelayMs);
  Keyboard.releaseAll();
  delay(cfg.keyDelayMs);
  return true;
}

void kbdCtrlAltDel() {
  String e;
  kbdCombo("ctrl+alt+del", e);
}

void kbdCtrlC() {
  String e;
  kbdCombo("ctrl+c", e);
}
