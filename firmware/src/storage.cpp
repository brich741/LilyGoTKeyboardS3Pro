#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

namespace storage {

namespace {
constexpr const char* kConfigPath = "/config.json";
constexpr const char* kTempPath = "/config.tmp";

SemaphoreHandle_t fsMtx = nullptr;   // serializes config/icon writes across the web, main and debug tasks
uint32_t lastSeq = 0;

struct FsLock {
  FsLock() { if (fsMtx) xSemaphoreTake(fsMtx, portMAX_DELAY); }
  ~FsLock() { if (fsMtx) xSemaphoreGive(fsMtx); }
};

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
  if (!fsMtx) fsMtx = xSemaphoreCreateMutex();
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

bool save(const core::Config& cfg, uint32_t seq) {
  FsLock lock;
  if (seq && seq < lastSeq) return true;   // a newer snapshot has already been written
  JsonDocument doc;
  core::writeConfig(cfg, doc);
  File f = LittleFS.open(kTempPath, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0 || n != measureJson(doc)) {   // short write (flash full): keep the old good config
    LittleFS.remove(kTempPath);
    return false;
  }
  LittleFS.remove(kConfigPath);
  if (!LittleFS.rename(kTempPath, kConfigPath)) return false;
  if (seq) lastSeq = seq;
  return true;
}

void wipe() {
  FsLock lock;
  LittleFS.remove(kConfigPath);
  LittleFS.remove(kTempPath);
  // Keep the shipped starter icons (they live in the filesystem image); drop everything else.
  std::vector<std::string> keep;
  for (const auto& i : core::defaultConfig().icons) keep.push_back(iconPath(i.id));
  File dir = LittleFS.open("/icons");
  if (dir && dir.isDirectory()) {
    std::vector<std::string> names;
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) names.push_back(std::string("/icons/") + e.name());
    dir.close();
    for (const auto& n : names) {
      bool starter = false;
      for (const auto& k : keep) starter |= k == n;
      if (!starter) LittleFS.remove(n.c_str());
    }
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
  FsLock lock;
  std::string tmp = iconPath(id) + ".tmp";
  File f = LittleFS.open(tmp.c_str(), "w");
  if (!f) return false;
  size_t n = f.write(buf, kIconBytes);
  f.close();
  if (n != kIconBytes) {   // keep the previous icon intact
    LittleFS.remove(tmp.c_str());
    return false;
  }
  LittleFS.remove(iconPath(id).c_str());
  return LittleFS.rename(tmp.c_str(), iconPath(id).c_str());
}

bool deleteIcon(const std::string& id) { return LittleFS.remove(iconPath(id).c_str()); }

size_t totalBytes() { return LittleFS.totalBytes(); }
size_t usedBytes() { return LittleFS.usedBytes(); }

}  // namespace storage
