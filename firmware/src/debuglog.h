#pragma once
#include <Arduino.h>

// USB-CDC debug port ("Claude Macro Pad" exposes it next to the HID keyboard) plus a tiny
// command console for bring-up. Type `help` in a serial terminal at any baud rate.
namespace debuglog {

void begin();                              // register the CDC port; call BEFORE USB.begin()
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void poll();                               // call from loop(): reads and runs console commands

}  // namespace debuglog
