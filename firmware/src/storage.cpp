#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace storage {

namespace {
constexpr const char* kConfigPath = "/config.json";
constexpr const char* kTempPath = "/config.tmp";

std::string iconPath(const std::string& id) { return "/icons/" + id + ".565"; }

bool loadFrom(const char* path, core::Config& out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) return false;
  std::string err;
  return core::parseConfig(doc.as<JsonVariantConst>(), out, err);
}
}  // namespace

bool begin() {
  if (!LittleFS.begin(true)) return false;  // true = format if mount fails
  if (!LittleFS.exists("/icons")) LittleFS.mkdir("/icons");
  return true;
}

core::Config load(bool* wasDefault) {
  core::Config c;
  bool ok = loadFrom(kConfigPath, c) || loadFrom(kTempPath, c);  // temp = interrupted save
  if (wasDefault) *wasDefault = !ok;
  return ok ? c : core::defaultConfig();
}

bool save(const core::Config& cfg) {
  JsonDocument doc;
  core::writeConfig(cfg, doc);
  File f = LittleFS.open(kTempPath, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) return false;
  LittleFS.remove(kConfigPath);
  return LittleFS.rename(kTempPath, kConfigPath);
}

void wipe() {
  LittleFS.remove(kConfigPath);
  LittleFS.remove(kTempPath);
  File dir = LittleFS.open("/icons");
  if (dir && dir.isDirectory()) {
    std::string names[128];
    int n = 0;
    for (File e = dir.openNextFile(); e && n < 128; e = dir.openNextFile())
      names[n++] = std::string("/icons/") + e.name();
    dir.close();
    for (int i = 0; i < n; ++i) LittleFS.remove(names[i].c_str());
  }
}

bool iconExists(const std::string& id) { return LittleFS.exists(iconPath(id).c_str()); }

bool readIcon(const std::string& id, uint8_t* buf) {
  File f = LittleFS.open(iconPath(id).c_str(), "r");
  if (!f) return false;
  size_t n = f.read(buf, kIconBytes);
  f.close();
  return n == kIconBytes;
}

bool writeIcon(const std::string& id, const uint8_t* buf) {
  File f = LittleFS.open(iconPath(id).c_str(), "w");
  if (!f) return false;
  size_t n = f.write(buf, kIconBytes);
  f.close();
  return n == kIconBytes;
}

bool deleteIcon(const std::string& id) { return LittleFS.remove(iconPath(id).c_str()); }

size_t totalBytes() { return LittleFS.totalBytes(); }
size_t usedBytes() { return LittleFS.usedBytes(); }

}  // namespace storage
