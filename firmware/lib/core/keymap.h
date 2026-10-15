#pragma once
#include <stdint.h>

// Key-name <-> USB HID usage mapping (keyboard page 0x07, consumer page 0x0C).
namespace core {

// Modifier bit flags used in Step::mods (same layout as the HID modifier byte).
enum : uint8_t { MOD_CTRL = 1, MOD_SHIFT = 2, MOD_ALT = 4, MOD_GUI = 8 };

// HID keyboard usage for a key name ("a", "f13", "enter", ...). 0 = unknown/empty.
uint8_t keyUsage(const char* name);
// Reverse lookup, nullptr if the usage has no name.
const char* keyName(uint8_t usage);

// Modifier name ("ctrl", "shift", "alt", "gui") -> MOD_* flag. 0 = unknown.
uint8_t modFlag(const char* name);
const char* modName(uint8_t flag);

// HID usage of the left-hand modifier key for a MOD_* flag (0xE0..0xE3).
uint8_t modUsage(uint8_t flag);

// Consumer-control usage for a media key name. 0 = unknown.
uint16_t mediaUsage(const char* name);
const char* mediaName(uint16_t usage);

}  // namespace core
