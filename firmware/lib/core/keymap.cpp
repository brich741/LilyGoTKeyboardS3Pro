#include "keymap.h"

#include <string.h>

namespace core {

namespace {

struct NamedKey {
  const char* name;
  uint8_t usage;
};

const NamedKey kKeys[] = {
    {"enter", 0x28},     {"esc", 0x29},        {"backspace", 0x2A}, {"tab", 0x2B},
    {"space", 0x2C},     {"-", 0x2D},          {"=", 0x2E},         {"[", 0x2F},
    {"]", 0x30},         {"\\", 0x31},         {";", 0x33},         {"'", 0x34},
    {"`", 0x35},         {",", 0x36},          {".", 0x37},         {"/", 0x38},
    {"capslock", 0x39},  {"printscreen", 0x46}, {"scrolllock", 0x47}, {"pause", 0x48},
    {"insert", 0x49},    {"home", 0x4A},       {"pageup", 0x4B},    {"delete", 0x4C},
    {"end", 0x4D},       {"pagedown", 0x4E},   {"right", 0x4F},     {"left", 0x50},
    {"down", 0x51},      {"up", 0x52},         {"menu", 0x65},
};

struct NamedMedia {
  const char* name;
  uint16_t usage;
};

const NamedMedia kMedia[] = {
    {"volup", 0xE9}, {"voldown", 0xEA}, {"mute", 0xE2}, {"playpause", 0xCD},
    {"next", 0xB5},  {"prev", 0xB6},    {"stop", 0xB7},
};

struct NamedMod {
  const char* name;
  uint8_t flag;
};

const NamedMod kMods[] = {
    {"ctrl", MOD_CTRL}, {"shift", MOD_SHIFT}, {"alt", MOD_ALT}, {"gui", MOD_GUI},
};

// "f1".."f24" -> 0x3A..0x45 (F1-F12), 0x68..0x73 (F13-F24).
uint8_t functionKey(const char* name) {
  if (name[0] != 'f' || name[1] == 0) return 0;
  int n = 0;
  for (const char* p = name + 1; *p; ++p) {
    if (*p < '0' || *p > '9') return 0;
    n = n * 10 + (*p - '0');
    if (n > 24) return 0;
  }
  if (n >= 1 && n <= 12) return (uint8_t)(0x3A + n - 1);
  if (n >= 13 && n <= 24) return (uint8_t)(0x68 + n - 13);
  return 0;
}

}  // namespace

uint8_t keyUsage(const char* name) {
  if (!name || !*name) return 0;
  if (name[1] == 0) {
    char c = name[0];
    if (c >= 'a' && c <= 'z') return (uint8_t)(0x04 + (c - 'a'));
    if (c >= '1' && c <= '9') return (uint8_t)(0x1E + (c - '1'));
    if (c == '0') return 0x27;
  }
  if (uint8_t f = functionKey(name)) return f;
  for (const auto& k : kKeys)
    if (strcmp(k.name, name) == 0) return k.usage;
  return 0;
}

const char* keyName(uint8_t usage) {
  static char buf[4][4];
  static uint8_t slot = 0;
  if (usage >= 0x04 && usage <= 0x1D) {
    char* b = buf[slot++ & 3];
    b[0] = (char)('a' + (usage - 0x04)), b[1] = 0;
    return b;
  }
  if (usage >= 0x1E && usage <= 0x26) {
    char* b = buf[slot++ & 3];
    b[0] = (char)('1' + (usage - 0x1E)), b[1] = 0;
    return b;
  }
  if (usage == 0x27) return "0";
  for (const auto& k : kKeys)
    if (k.usage == usage) return k.name;
  for (int n = 1; n <= 24; ++n) {
    char name[4] = {'f', (char)('0' + n / 10), (char)('0' + n % 10), 0};
    if (n < 10) name[1] = (char)('0' + n), name[2] = 0;
    if (functionKey(name) == usage) {
      char* b = buf[slot++ & 3];
      strcpy(b, name);
      return b;
    }
  }
  return nullptr;
}

uint8_t modFlag(const char* name) {
  if (!name) return 0;
  for (const auto& m : kMods)
    if (strcmp(m.name, name) == 0) return m.flag;
  return 0;
}

const char* modName(uint8_t flag) {
  for (const auto& m : kMods)
    if (m.flag == flag) return m.name;
  return nullptr;
}

uint8_t modUsage(uint8_t flag) {
  switch (flag) {
    case MOD_CTRL: return 0xE0;
    case MOD_SHIFT: return 0xE1;
    case MOD_ALT: return 0xE2;
    case MOD_GUI: return 0xE3;
    default: return 0;
  }
}

uint16_t mediaUsage(const char* name) {
  if (!name) return 0;
  for (const auto& m : kMedia)
    if (strcmp(m.name, name) == 0) return m.usage;
  return 0;
}

const char* mediaName(uint16_t usage) {
  for (const auto& m : kMedia)
    if (m.usage == usage) return m.name;
  return nullptr;
}

}  // namespace core
