#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keymap.h"

namespace core {

// ---------------------------------------------------------------- helpers

const char* effectName(Effect e) {
  switch (e) {
    case Effect::Off: return "off";
    case Effect::Solid: return "solid";
    case Effect::Breathe: return "breathe";
    case Effect::Rainbow: return "rainbow";
    case Effect::Chase: return "chase";
    case Effect::Sparkle: return "sparkle";
  }
  return "off";
}

bool effectFromName(const char* s, Effect& out) {
  if (!s) return false;
  static const Effect all[] = {Effect::Off,     Effect::Solid, Effect::Breathe,
                               Effect::Rainbow, Effect::Chase, Effect::Sparkle};
  for (Effect e : all)
    if (strcmp(effectName(e), s) == 0) {
      out = e;
      return true;
    }
  return false;
}

bool parseColor(const char* s, uint32_t& out) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  uint32_t v = 0;
  for (int i = 1; i < 7; ++i) {
    char c = s[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | (uint32_t)d;
  }
  out = v;
  return true;
}

std::string formatColor(uint32_t rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06x", (unsigned)(rgb & 0xFFFFFF));
  return buf;
}

bool validIconId(const std::string& id) {
  if (id.empty() || id.size() > 24) return false;
  for (char c : id) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

namespace {

int clampInt(long v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : (int)v); }

int getInt(JsonVariantConst o, const char* key, int def, int lo, int hi) {
  JsonVariantConst v = o[key];
  if (!v.is<long>()) return def;
  return clampInt(v.as<long>(), lo, hi);
}

std::string getStr(JsonVariantConst o, const char* key, const char* def = "") {
  JsonVariantConst v = o[key];
  if (!v.is<const char*>()) return def;
  return v.as<const char*>();
}

bool parseVisual(JsonVariantConst src, Visual& v, std::string& err) {
  v = Visual();
  v.icon = getStr(src, "icon");
  if (!v.icon.empty() && !validIconId(v.icon)) {
    err = "bad icon id";
    return false;
  }
  std::string c = getStr(src, "color");
  if (!c.empty()) {
    if (!parseColor(c.c_str(), v.color)) {
      err = "bad color";
      return false;
    }
    v.hasColor = true;
  }
  return true;
}

bool parseStep(JsonVariantConst s, Step& out, std::string& err) {
  std::string t = getStr(s, "t");
  out = Step();
  if (t == "key") {
    out.type = Step::Key;
    std::string mode = getStr(s, "mode", "tap");
    if (mode == "tap") out.mode = Step::Tap;
    else if (mode == "down") out.mode = Step::Down;
    else if (mode == "up") out.mode = Step::Up;
    else { err = "bad key mode"; return false; }
    JsonVariantConst mods = s["mods"];
    if (mods.is<JsonArrayConst>()) {
      for (JsonVariantConst m : mods.as<JsonArrayConst>()) {
        uint8_t f = modFlag(m.is<const char*>() ? m.as<const char*>() : "");
        if (!f) { err = "bad modifier"; return false; }
        out.mods |= f;
      }
    }
    std::string key = getStr(s, "key");
    if (!key.empty()) {
      out.key = keyUsage(key.c_str());
      if (!out.key) { err = "unknown key '" + key + "'"; return false; }
    }
    if (!out.key && !out.mods) { err = "key step needs a key or modifier"; return false; }
    return true;
  }
  if (t == "text") {
    out.type = Step::Text;
    out.text = getStr(s, "text");
    if (out.text.size() > 256) { err = "text too long"; return false; }
    return true;
  }
  if (t == "media") {
    out.type = Step::Media;
    out.media = mediaUsage(getStr(s, "key").c_str());
    if (!out.media) { err = "unknown media key"; return false; }
    return true;
  }
  if (t == "delay") {
    out.type = Step::Delay;
    out.ms = (uint16_t)getInt(s, "ms", 0, 0, 5000);
    return true;
  }
  err = "unknown step type";
  return false;
}

bool parseGesture(JsonVariantConst src, Gesture& g, std::string& err) {
  g = Gesture();
  if (!src.is<JsonObjectConst>()) return true;  // absent
  g.present = true;
  if (!parseSteps(src["steps"], g.steps, err)) return false;
  if (!parseVisual(src, g.vis, err)) return false;
  g.ms = (uint16_t)getInt(src, "ms", 0, 0, 65535);
  return true;
}

bool parseHold(JsonVariantConst src, HoldGesture& h, std::string& err) {
  h = HoldGesture();
  if (!src.is<JsonObjectConst>()) return true;
  h.present = true;
  if (!parseSteps(src["start"], h.start, err)) return false;
  if (!parseSteps(src["end"], h.end, err)) return false;
  if (!parseVisual(src, h.vis, err)) return false;
  JsonVariantConst after = src["after"];
  if (after.is<JsonObjectConst>()) {
    h.hasAfter = true;
    if (!parseVisual(after, h.after, err)) return false;
    h.afterMs = (uint16_t)getInt(after, "ms", 0, 0, 65535);
  }
  return true;
}

bool parseKey(JsonVariantConst src, KeyCfg& k, std::string& err) {
  k = KeyCfg();
  if (!src.is<JsonObjectConst>()) return true;
  if (!parseVisual(src["idle"], k.idle, err)) return false;
  if (!parseGesture(src["single"], k.single, err)) return false;
  if (!parseGesture(src["double"], k.dbl, err)) return false;
  if (!parseHold(src["hold"], k.hold, err)) return false;
  return true;
}

void writeSteps(const Steps& steps, JsonArray arr) {
  for (const Step& s : steps) {
    JsonObject o = arr.add<JsonObject>();
    switch (s.type) {
      case Step::Key: {
        o["t"] = "key";
        o["mode"] = s.mode == Step::Tap ? "tap" : (s.mode == Step::Down ? "down" : "up");
        JsonArray mods = o["mods"].to<JsonArray>();
        for (uint8_t f : {MOD_CTRL, MOD_SHIFT, MOD_ALT, MOD_GUI})
          if (s.mods & f) mods.add(modName(f));
        const char* kn = s.key ? keyName(s.key) : "";
        o["key"] = kn ? kn : "";
        break;
      }
      case Step::Text:
        o["t"] = "text";
        o["text"] = s.text;
        break;
      case Step::Media: {
        o["t"] = "media";
        const char* mn = mediaName(s.media);
        o["key"] = mn ? mn : "";
        break;
      }
      case Step::Delay:
        o["t"] = "delay";
        o["ms"] = s.ms;
        break;
    }
  }
}

void writeVisual(const Visual& v, JsonObject o) {
  if (!v.icon.empty()) o["icon"] = v.icon;
  if (v.hasColor) o["color"] = formatColor(v.color);
}

void writeKey(const KeyCfg& k, JsonObject o) {
  writeVisual(k.idle, o["idle"].to<JsonObject>());
  if (k.single.present) {
    JsonObject g = o["single"].to<JsonObject>();
    writeSteps(k.single.steps, g["steps"].to<JsonArray>());
    writeVisual(k.single.vis, g);
    if (k.single.ms) g["ms"] = k.single.ms;
  }
  if (k.dbl.present) {
    JsonObject g = o["double"].to<JsonObject>();
    writeSteps(k.dbl.steps, g["steps"].to<JsonArray>());
    writeVisual(k.dbl.vis, g);
    if (k.dbl.ms) g["ms"] = k.dbl.ms;
  }
  if (k.hold.present) {
    JsonObject h = o["hold"].to<JsonObject>();
    writeSteps(k.hold.start, h["start"].to<JsonArray>());
    writeSteps(k.hold.end, h["end"].to<JsonArray>());
    writeVisual(k.hold.vis, h);
    if (k.hold.hasAfter) {
      JsonObject a = h["after"].to<JsonObject>();
      writeVisual(k.hold.after, a);
      if (k.hold.afterMs) a["ms"] = k.hold.afterMs;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------- public

bool parseSteps(JsonVariantConst src, Steps& out, std::string& err) {
  out.clear();
  if (src.isNull()) return true;
  if (!src.is<JsonArrayConst>()) {
    err = "steps must be an array";
    return false;
  }
  JsonArrayConst arr = src.as<JsonArrayConst>();
  if (arr.size() > 32) {
    err = "too many steps";
    return false;
  }
  for (JsonVariantConst s : arr) {
    Step st;
    if (!parseStep(s, st, err)) return false;
    out.push_back(std::move(st));
  }
  return true;
}

bool parseConfig(JsonVariantConst src, Config& out, std::string& err) {
  if (!src.is<JsonObjectConst>()) {
    err = "config must be an object";
    return false;
  }
  Config c;
  c.version = getInt(src, "version", kConfigVersion, 1, 1000);
  if (c.version > kConfigVersion) {
    err = "config version is newer than firmware";
    return false;
  }

  JsonVariantConst s = src["settings"];
  Settings& st = c.settings;
  if (s.is<JsonObjectConst>()) {
    st.apSsid = getStr(s, "apSsid", st.apSsid.c_str());
    st.apPass = getStr(s, "apPass", st.apPass.c_str());
    if (st.apSsid.empty() || st.apSsid.size() > 31) { err = "apSsid must be 1-31 chars"; return false; }
    if (st.apPass.size() < 8 || st.apPass.size() > 63) { err = "apPass must be 8-63 chars"; return false; }
    st.brightness = (uint8_t)getInt(s, "brightness", st.brightness, 5, 100);
    st.ledBrightness = (uint8_t)getInt(s, "ledBrightness", st.ledBrightness, 0, 100);
    st.dimMinutes = (uint16_t)getInt(s, "dimMinutes", st.dimMinutes, 0, 600);
    st.doubleMs = (uint16_t)getInt(s, "doubleMs", st.doubleMs, 100, 1000);
    st.holdMs = (uint16_t)getInt(s, "holdMs", st.holdMs, 150, 3000);
    st.revertMs = (uint16_t)getInt(s, "revertMs", st.revertMs, 100, 30000);
    st.activePage = (uint8_t)getInt(s, "activePage", 0, 0, kMaxPages - 1);
    if (s["ledHue"].is<bool>()) st.ledHue = s["ledHue"].as<bool>();   // absent: keep default
    JsonVariantConst z = s["ledZones"];
    if (z.is<JsonArrayConst>()) {
      int i = 0;
      for (JsonVariantConst m : z.as<JsonArrayConst>()) {
        if (i >= kKeyCount) break;
        st.ledZones[i++] = (uint16_t)(m.as<long>() & 0x3FFF);
      }
    }
  }

  JsonVariantConst icons = src["icons"];
  if (icons.is<JsonArrayConst>()) {
    for (JsonVariantConst i : icons.as<JsonArrayConst>()) {
      IconInfo info;
      info.id = getStr(i, "id");
      info.name = getStr(i, "name", info.id.c_str());
      if (!validIconId(info.id)) { err = "bad icon id in icons"; return false; }
      c.icons.push_back(std::move(info));
    }
  }

  JsonVariantConst pages = src["pages"];
  if (!pages.is<JsonArrayConst>() || pages.as<JsonArrayConst>().size() == 0) {
    err = "pages must be a non-empty array";
    return false;
  }
  if (pages.as<JsonArrayConst>().size() > (size_t)kMaxPages) {
    err = "too many pages";
    return false;
  }
  for (JsonVariantConst p : pages.as<JsonArrayConst>()) {
    PageCfg page;
    page.name = getStr(p, "name", "Page");
    if (page.name.empty()) page.name = "Page";
    if (page.name.size() > 15) page.name.resize(15);
    JsonVariantConst led = p["led"];
    if (led.is<JsonObjectConst>()) {
      if (!effectFromName(getStr(led, "effect", "off").c_str(), page.led.effect)) {
        err = "bad led effect";
        return false;
      }
      std::string col = getStr(led, "color");
      if (!col.empty() && !parseColor(col.c_str(), page.led.color)) { err = "bad led color"; return false; }
      page.led.speed = (uint8_t)getInt(led, "speed", 50, 1, 100);
    }
    JsonVariantConst keys = p["keys"];
    if (keys.is<JsonArrayConst>()) {
      int i = 0;
      for (JsonVariantConst k : keys.as<JsonArrayConst>()) {
        if (i >= kKeyCount) break;
        if (!parseKey(k, page.keys[i++], err)) return false;
      }
    }
    if (!parseKey(p["encoder"], page.encoder, err)) return false;
    page.encoder.hold = HoldGesture();  // reserved: 3 s long press shows Wi-Fi info
    c.pages.push_back(std::move(page));
  }
  if (c.settings.activePage >= c.pages.size()) c.settings.activePage = 0;
  out = std::move(c);
  return true;
}

void writeConfig(const Config& cfg, JsonDocument& dst) {
  dst["version"] = cfg.version;
  const Settings& s = cfg.settings;
  JsonObject so = dst["settings"].to<JsonObject>();
  so["apSsid"] = s.apSsid;
  so["apPass"] = s.apPass;
  so["brightness"] = s.brightness;
  so["ledBrightness"] = s.ledBrightness;
  so["dimMinutes"] = s.dimMinutes;
  so["doubleMs"] = s.doubleMs;
  so["holdMs"] = s.holdMs;
  so["revertMs"] = s.revertMs;
  so["activePage"] = s.activePage;
  so["ledHue"] = s.ledHue;
  JsonArray z = so["ledZones"].to<JsonArray>();
  for (int i = 0; i < kKeyCount; ++i) z.add(s.ledZones[i]);

  JsonArray icons = dst["icons"].to<JsonArray>();
  for (const IconInfo& i : cfg.icons) {
    JsonObject o = icons.add<JsonObject>();
    o["id"] = i.id;
    o["name"] = i.name;
  }

  JsonArray pages = dst["pages"].to<JsonArray>();
  for (const PageCfg& p : cfg.pages) {
    JsonObject po = pages.add<JsonObject>();
    po["name"] = p.name;
    JsonObject lo = po["led"].to<JsonObject>();
    lo["effect"] = effectName(p.led.effect);
    lo["color"] = formatColor(p.led.color);
    lo["speed"] = p.led.speed;
    JsonArray keys = po["keys"].to<JsonArray>();
    for (int i = 0; i < kKeyCount; ++i) writeKey(p.keys[i], keys.add<JsonObject>());
    writeKey(p.encoder, po["encoder"].to<JsonObject>());
  }
}

// ---------------------------------------------------------------- defaults

namespace {

Step keyTap(uint8_t mods, const char* key) {
  Step s;
  s.type = Step::Key;
  s.mode = Step::Tap;
  s.mods = mods;
  s.key = keyUsage(key);
  return s;
}

Step keyDown(uint8_t mods, const char* key) {
  Step s = keyTap(mods, key);
  s.mode = Step::Down;
  return s;
}

Step text(const char* t) {
  Step s;
  s.type = Step::Text;
  s.text = t;
  return s;
}

Step media(const char* name) {
  Step s;
  s.type = Step::Media;
  s.media = mediaUsage(name);
  return s;
}

Step delay(uint16_t ms) {
  Step s;
  s.type = Step::Delay;
  s.ms = ms;
  return s;
}

Visual vis(const char* icon, uint32_t color) {
  Visual v;
  v.icon = icon;
  v.hasColor = true;
  v.color = color;
  return v;
}

Gesture gesture(Steps steps, Visual v = Visual()) {
  Gesture g;
  g.present = true;
  g.steps = std::move(steps);
  g.vis = v;
  return g;
}

}  // namespace

Config defaultConfig() {
  Config c;

  PageCfg claude;
  claude.name = "Claude Code";
  claude.led = {Effect::Breathe, 0x3355FF, 30};

  // Key 1: hold = push-to-talk (VS Code extension shows Ctrl+D; CLI: bind voice:pushToTalk).
  KeyCfg& mic = claude.keys[0];
  mic.idle = vis("mic-red", 0xFF0000);
  mic.hold.present = true;
  mic.hold.start = {keyDown(MOD_CTRL, "d")};
  mic.hold.vis = vis("mic-green", 0x00FF00);

  KeyCfg& clear = claude.keys[1];
  clear.idle = vis("clear", 0);
  clear.idle.hasColor = false;
  clear.single = gesture({text("/clear\n")}, vis("", 0xFFAA00));
  clear.dbl = gesture({text("/compact\n")}, vis("", 0xFFAA00));

  KeyCfg& esc = claude.keys[2];
  esc.idle = vis("esc", 0);
  esc.idle.hasColor = false;
  esc.single = gesture({keyTap(0, "esc")}, vis("", 0xFF3333));
  esc.dbl = gesture({keyTap(0, "esc"), delay(80), keyTap(0, "esc")}, vis("", 0xFF3333));

  KeyCfg& rc = claude.keys[3];
  rc.idle = vis("remote", 0);
  rc.idle.hasColor = false;
  rc.single = gesture({text("/rc\n")}, vis("", 0x00FFFF));
  rc.dbl = gesture({text("/voice\n")}, vis("", 0x00FFFF));

  claude.encoder.single = gesture({keyTap(0, "enter")});
  c.pages.push_back(claude);

  PageCfg win;
  win.name = "Windows";
  win.led = {Effect::Rainbow, 0x3355FF, 20};
  win.keys[0].idle = vis("win", 0);
  win.keys[0].idle.hasColor = false;
  win.keys[0].single = gesture({keyTap(MOD_GUI, "d")}, vis("", 0x3355FF));
  win.keys[1].idle = vis("play", 0);
  win.keys[1].idle.hasColor = false;
  win.keys[1].single = gesture({media("playpause")}, vis("", 0x00FF00));
  win.keys[2].idle = vis("vol", 0);
  win.keys[2].idle.hasColor = false;
  win.keys[2].single = gesture({media("volup")}, vis("", 0xFFFFFF));
  win.keys[2].dbl = gesture({media("voldown")}, vis("", 0xFFFFFF));
  win.keys[3].idle = vis("stop", 0);
  win.keys[3].idle.hasColor = false;
  win.keys[3].single = gesture({keyTap(MOD_GUI, "l")}, vis("", 0xFF0000));
  win.encoder.single = gesture({media("mute")});
  c.pages.push_back(win);

  // Starter icons shipped in data/icons (see tools/make_icons.py).
  c.icons = {{"mic-red", "Mic (red)"}, {"mic-green", "Mic (green)"}, {"clear", "Clear"},
             {"esc", "Escape"},        {"remote", "Remote"},         {"win", "Windows"},
             {"play", "Play"},         {"vol", "Volume"},            {"stop", "Stop"},
             {"check", "Accept"},      {"enter", "Enter"},           {"spark", "Claude"}};
  return c;
}

}  // namespace core
