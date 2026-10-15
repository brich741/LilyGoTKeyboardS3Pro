#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

#include <string>
#include <vector>

// In-memory model of config.json (see docs/CONFIG.md). Hardware independent.
namespace core {

constexpr int kKeyCount = 4;       // keys with a display
constexpr int kMaxPages = 8;
constexpr int kLedCount = 14;
constexpr int kConfigVersion = 1;
constexpr uint16_t kStickyMs = 65535;  // "ms" value meaning: stay until next press

struct Step {
  enum Type : uint8_t { Key, Text, Media, Delay };
  enum Mode : uint8_t { Tap, Down, Up };
  Type type = Key;
  Mode mode = Tap;
  uint8_t mods = 0;       // MOD_* flags
  uint8_t key = 0;        // HID usage, 0 = none (modifier-only)
  uint16_t media = 0;     // consumer usage
  uint16_t ms = 0;        // delay
  std::string text;
};
using Steps = std::vector<Step>;

struct Visual {
  std::string icon;       // "" = none / inherit
  bool hasColor = false;
  uint32_t color = 0;     // 0xRRGGBB
};

struct Gesture {
  bool present = false;
  Steps steps;
  Visual vis;             // override while active (empty = keep idle visuals)
  uint16_t ms = 0;        // how long the override is shown (0 = settings.revertMs)
};

struct HoldGesture {
  bool present = false;
  Steps start, end;
  Visual vis;             // while held
  bool hasAfter = false;
  Visual after;           // after release
  uint16_t afterMs = 0;
};

struct KeyCfg {
  Visual idle;
  Gesture single, dbl;
  HoldGesture hold;
};

enum class Effect : uint8_t { Off, Solid, Breathe, Rainbow, Chase, Sparkle };

struct LedCfg {
  Effect effect = Effect::Off;
  uint32_t color = 0x3355FF;
  uint8_t speed = 50;     // 1..100
};

struct PageCfg {
  std::string name;
  LedCfg led;
  KeyCfg keys[kKeyCount];
  KeyCfg encoder;         // push button; visuals ignored, hold ignored
};

struct IconInfo {
  std::string id, name;
};

struct Settings {
  std::string apSsid = "ClaudePad";
  std::string apPass = "claudepad1";
  uint8_t brightness = 80;       // screens, %
  uint8_t ledBrightness = 40;    // LEDs, %
  uint16_t dimMinutes = 10;
  uint16_t doubleMs = 250;
  uint16_t holdMs = 400;
  uint16_t revertMs = 1500;
  uint8_t activePage = 0;
  uint16_t ledZones[kKeyCount] = {0x0003, 0x0018, 0x00C0, 0x0600};
};

struct Config {
  int version = kConfigVersion;
  Settings settings;
  std::vector<IconInfo> icons;
  std::vector<PageCfg> pages;
};

// Effect <-> name.
const char* effectName(Effect e);
bool effectFromName(const char* s, Effect& out);

// "#rrggbb" <-> 0xRRGGBB.
bool parseColor(const char* s, uint32_t& out);
std::string formatColor(uint32_t rgb);

// Is `id` a legal icon id ([a-z0-9_-]{1,24})?
bool validIconId(const std::string& id);

// Parse + validate. Clamps ranges, ignores unknown fields. Returns false with a
// message in `err` for structural problems (wrong types, bad key names, ...).
bool parseConfig(JsonVariantConst src, Config& out, std::string& err);
bool parseSteps(JsonVariantConst src, Steps& out, std::string& err);

// Serialize into `dst` (a JsonDocument).
void writeConfig(const Config& cfg, JsonDocument& dst);

// Factory configuration: "Claude Code" page + "Windows" page.
Config defaultConfig();

// True if the key has any action or `hold` etc. (used by gesture engine setup).
inline bool hasDouble(const KeyCfg& k) { return k.dbl.present; }
inline bool hasHold(const KeyCfg& k) { return k.hold.present; }

}  // namespace core
