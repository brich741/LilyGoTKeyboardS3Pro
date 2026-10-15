#pragma once
#include <stdint.h>

#include "config.h"
#include "visual.h"

namespace core {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Rgb& o) const { return !(*this == o); }
};

inline Rgb rgbFromHex(uint32_t c) { return {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c}; }

// Fill `out[14]` with the page's idle effect at time `tMs`.
void renderEffect(const LedCfg& cfg, uint32_t tMs, Rgb out[kLedCount]);

// Effect plus per-key zone overrides: a key whose current look has a color
// paints only its own zone (mask over the 14 LEDs) with that color.
void composeLeds(const LedCfg& cfg, const uint16_t zones[kKeyCount],
                 const KeyLook looks[kKeyCount], uint32_t tMs, Rgb out[kLedCount]);

// The STM32 paints one HSV color per select operation. NOTE: on the shipped STM32 firmware the hue
// registers have no effect (verified on hardware): only brightness and saturation work, so every
// color renders as white (S=0) .. magenta-pink (S=100). We keep h = 0 and encode the rest in s/v.
struct Hsv {
  uint16_t h = 0;   // 0..359
  uint8_t s = 0;    // 0..100
  uint8_t v = 0;    // 0..100
  bool operator==(const Hsv& o) const { return h == o.h && s == o.s && v == o.v; }
};

Hsv toHsv(Rgb c);

struct PaintOp {
  Hsv color;
  uint16_t mask = 0;   // bit0 = LED1
};

// Group the LEDs that differ between `prev` and `next` into as few paint
// operations as possible. Colors are quantized, and `brightnessPct` (0..100)
// scales V. `prev` may be nullptr (paint everything). Returns the op count.
int planPaint(const Rgb* prev, const Rgb next[kLedCount], uint8_t brightnessPct,
              PaintOp ops[kLedCount]);

}  // namespace core
