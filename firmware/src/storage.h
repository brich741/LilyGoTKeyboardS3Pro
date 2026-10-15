#pragma once
#include <Arduino.h>

#include <string>

#include "config.h"

namespace storage {

constexpr size_t kIconBytes = 128 * 128 * 2;

bool begin();                                  // mount LittleFS (formats on first boot)

// Config: returns the stored config, or the factory default if missing/invalid.
core::Config load(bool* wasDefault = nullptr);
// Atomic (temp file + rename), serialized across tasks. `seq` (from app::saveSeq, taken together with the
// snapshot) makes a stale snapshot a no-op if a newer one was already written; 0 = always write.
bool save(const core::Config& cfg, uint32_t seq = 0);
void wipe();                                   // delete config and all non-starter icons

// Icons (raw 128x128 RGB565, big-endian per pixel).
bool iconExists(const std::string& id);
bool readIcon(const std::string& id, uint8_t* buf);   // buf must hold kIconBytes
bool writeIcon(const std::string& id, const uint8_t* buf);
bool deleteIcon(const std::string& id);

size_t totalBytes();
size_t usedBytes();

}  // namespace storage
