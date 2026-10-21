#include "visual.h"

namespace core {

KeyLook KeyVisual::resolve(const KeyCfg& key, const Visual& v) const {
  KeyLook l;
  l.icon = v.icon.empty() ? key.idle.icon : v.icon;
  if (v.hasColor) {
    l.hasColor = true;
    l.color = v.color;
  } else {
    l.hasColor = key.idle.hasColor;
    l.color = key.idle.color;
  }
  return l;
}

void KeyVisual::reset(const KeyCfg& key) {
  look_ = resolve(key, key.idle);
  timed_ = false;
  on_ = false;
}

void KeyVisual::show(const KeyCfg& key, const Visual& v, uint32_t now, uint16_t ms,
                     uint16_t revertMs) {
  look_ = resolve(key, v);  if (ms == kStickyMs) {
    timed_ = false;
  } else {
    timed_ = true;
    until_ = now + (ms ? ms : revertMs);
  }
}

const Gesture& KeyVisual::onSingle(const KeyCfg& key, uint32_t now, uint16_t revertMs) {
  if (!key.toggle.present) {
    show(key, key.single.vis, now, key.single.ms, revertMs);
    return key.single;
  }
  on_ = !on_;
  const Gesture& g = on_ ? key.single : key.toggle;
  show(key, g.vis, now, kStickyMs, revertMs);   // the look is the state: it stays until the next press
  return g;
}

void KeyVisual::onDouble(const KeyCfg& key, uint32_t now, uint16_t revertMs) {
  show(key, key.dbl.vis, now, key.dbl.ms, revertMs);
}

void KeyVisual::onHoldStart(const KeyCfg& key) {
  look_ = resolve(key, key.hold.vis);
  timed_ = false;  // lasts until release
}

void KeyVisual::onHoldEnd(const KeyCfg& key, uint32_t now, uint16_t revertMs) {
  if (key.hold.hasAfter) {
    show(key, key.hold.after, now, key.hold.afterMs, revertMs);
  } else {
    reset(key);
  }
}

bool KeyVisual::tick(const KeyCfg& key, uint32_t now) {
  if (!timed_ || (int32_t)(now - until_) < 0) return false;
  KeyLook before = look_;
  reset(key);
  return look_ != before;
}

}  // namespace core
