#pragma once
#include <Arduino.h>

void kbdBegin();
bool kbdReady();  // USB enumerated and HID ready

void kbdType(const char *text, bool enter);
// Press + release a single named key, e.g. "esc", "f5", "a".
bool kbdKey(const char *name, String &err);
// Press a combo, e.g. "ctrl+alt+del", "shift+f10". Returns false on parse error.
bool kbdCombo(const char *combo, String &err);
void kbdCtrlAltDel();
void kbdCtrlC();
