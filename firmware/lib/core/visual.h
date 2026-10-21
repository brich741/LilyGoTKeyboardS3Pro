#pragma once
#include <stdint.h>

#include "config.h"

namespace core {

// What a key should currently show.
struct KeyLook {
  std::string icon;
  bool hasColor = false;
  uint32_t color = 0;
  bool operator==(const KeyLook& o) const {
    return icon == o.icon && hasColor == o.hasColor && (!hasColor || color == o.color);
  }
  bool operator!=(const KeyLook& o) const { return !(*this == o); }
};

// Tracks the visual state of one key: idle -> (flash | held | after-hold) -> idle.
// Holds copies of visuals, so it is safe across config changes.
class KeyVisual {
 public:
  // Reset to idle with the given key config (call on page switch / config change).
  void reset(const KeyCfg& key);

  // Returns the gesture whose steps to run. With key.toggle present, presses alternate
  // single ("on", look stays until the next press) / toggle ("off", back to idle or its own look).
  const Gesture& onSingle(const KeyCfg& key, uint32_t now, uint16_t revertMs);
  void onDouble(const KeyCfg& key, uint32_t now, uint16_t revertMs);
  void onHoldStart(const KeyCfg& key);
  void onHoldEnd(const KeyCfg& key, uint32_t now, uint16_t revertMs);

  // Advance timers. Returns true if the look changed.
  bool tick(const KeyCfg& key, uint32_t now);

  const KeyLook& look() const { return look_; }

 private:
  void show(const KeyCfg& key, const Visual& v, uint32_t now, uint16_t ms, uint16_t revertMs);
  KeyLook resolve(const KeyCfg& key, const Visual& v) const;

  KeyLook look_;
  bool on_ = false;       // toggle state
  bool timed_ = false;
  uint32_t until_ = 0;
};

}  // namespace core
