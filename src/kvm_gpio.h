#pragma once
#include <Arduino.h>

void gpioInit();
// Each returns true on success; msg gets an error or an info/warning text.
bool gpioEnable(int pin, String &msg);
bool gpioDisable(int pin, String &msg);
// mode: "in", "in_pullup", "in_pulldown", "out"
bool gpioMode(int pin, const String &mode, String &msg);
bool gpioWrite(int pin, const String &value, String &msg);  // 0|1|high|low|toggle
bool gpioRead(int pin, int &level, String &msg);
String gpioDescribe(int pin);          // one line state description
void gpioListEnabled(String &out);     // all enabled pins
void gpioListUsable(String &out);      // pin classification table
