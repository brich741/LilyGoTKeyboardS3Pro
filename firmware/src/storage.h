#pragma once
#include <Arduino.h>

#include <string>

#include "config.h"

namespace storage {

constexpr size_t kIconBytes = 128 * 128 * 2;

bool begin();                                  // mount LittleFS (formats on first boot)

// Config: returns the stored config, or the factory default if missing/invalid.
core::Config load(bool* wasDefault = nullptr);
bool save(const core::Config& cfg);            // atomic: temp file + rename
void wipe();                                   // delete config and all icons

// Icons (raw 128x128 RGB565, big-endian per pixel).
bool iconExists(const std::string& id);
bool readIcon(const std::string& id, uint8_t* buf);   // buf must hold kIconBytes
bool writeIcon(const std::string& id, const uint8_t* buf);
bool deleteIcon(const std::string& id);

size_t totalBytes();
size_t usedBytes();

}  // namespace storage
