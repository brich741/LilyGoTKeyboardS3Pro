#include "leds.h"

#include <math.h>

namespace core {

namespace {

constexpr float kPi = 3.14159265f;

Rgb scale(Rgb c, float f) {
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  return {(uint8_t)(c.r * f + 0.5f), (uint8_t)(c.g * f + 0.5f), (uint8_t)(c.b * f + 0.5f)};
}

Rgb fromHsv(float h, float s, float v) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s;
  float x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1));
  float m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) r = c, g = x;
  else if (h < 120) r = x, g = c;
  else if (h < 180) g = c, b = x;
  else if (h < 240) g = x, b = c;
  else if (h < 300) r = x, b = c;
  else r = c, b = x;
  return {(uint8_t)((r + m) * 255 + 0.5f), (uint8_t)((g + m) * 255 + 0.5f),
          (uint8_t)((b + m) * 255 + 0.5f)};
}

uint32_t hash(uint32_t a, uint32_t b) {
  uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u) * 40503u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

}  // namespace

void renderEffect(const LedCfg& cfg, uint32_t t, Rgb out[kLedCount]) {
  Rgb base = rgbFromHex(cfg.color);
  float sec = t / 1000.0f;
  float speed = (float)cfg.speed;

  switch (cfg.effect) {
    case Effect::Off:
      for (int i = 0; i < kLedCount; ++i) out[i] = Rgb();
      break;
    case Effect::Solid:
      for (int i = 0; i < kLedCount; ++i) out[i] = base;
      break;
    case Effect::Breathe: {
      float period = 6.0f - speed * 0.055f;  // 5.9 s .. 0.5 s
      float f = 0.5f - 0.5f * cosf(2 * kPi * sec / period);
      Rgb c = scale(base, 0.08f + 0.92f * f);
      for (int i = 0; i < kLedCount; ++i) out[i] = c;
      break;
    }
    case Effect::Rainbow: {
      // This board's STM32 ignores hue, so "rainbow" is a rotating white <-> pink (saturation) wave.
      float phase = sec * speed * 0.06f;
      for (int i = 0; i < kLedCount; ++i) {
        float sat = 0.5f + 0.5f * sinf(2 * kPi * (phase + i / (float)kLedCount));
        out[i] = {255, (uint8_t)(255 * (1 - sat) + 0.5f), (uint8_t)(255 * (1 - sat) + 0.5f)};
      }
      break;
    }
    case Effect::Chase: {
      float pos = fmodf(sec * speed * 0.2f, (float)kLedCount);
      for (int i = 0; i < kLedCount; ++i) {
        float dist = pos - i;
        if (dist < 0) dist += kLedCount;
        out[i] = scale(base, dist < 4.0f ? 1.0f - dist / 4.0f : 0.0f);
      }
      break;
    }
    case Effect::Sparkle: {
      uint32_t slotMs = (uint32_t)(900.0f / (1.0f + speed * 0.05f));  // 900 .. ~150 ms
      if (slotMs < 60) slotMs = 60;
      uint32_t slot = t / slotMs;
      float within = (float)(t % slotMs) / (float)slotMs;
      for (int i = 0; i < kLedCount; ++i) {
        bool lit = hash((uint32_t)i, slot) % 100 < 18;
        out[i] = lit ? scale(base, 1.0f - within) : Rgb();
      }
      break;
    }
  }
}

void composeLeds(const LedCfg& cfg, const uint16_t zones[kKeyCount],
                 const KeyLook looks[kKeyCount], uint32_t t, Rgb out[kLedCount]) {
  renderEffect(cfg, t, out);
  for (int k = 0; k < kKeyCount; ++k) {
    if (!looks[k].hasColor) continue;
    Rgb c = rgbFromHex(looks[k].color);
    for (int i = 0; i < kLedCount; ++i)
      if (zones[k] & (1u << i)) out[i] = c;
  }
}

Hsv toHsv(Rgb c) {
  float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
  float d = mx - mn;
  float h = 0;
  if (d > 0) {
    if (mx == r) h = 60.0f * fmodf((g - b) / d, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2);
    else h = 60.0f * ((r - g) / d + 4);
    if (h < 0) h += 360.0f;
  }
  Hsv o;
  o.h = (uint16_t)h % 360;
  o.s = (uint8_t)(mx > 0 ? d / mx * 100.0f + 0.5f : 0);
  o.v = (uint8_t)(mx * 100.0f + 0.5f);
  return o;
}

int planPaint(const Rgb* prev, const Rgb next[kLedCount], uint8_t brightnessPct,
              PaintOp ops[kLedCount]) {
  int n = 0;
  for (int i = 0; i < kLedCount; ++i) {
    if (prev && prev[i] == next[i]) continue;
    Hsv c = toHsv(next[i]);
    // Quantize to keep the number of distinct colors (= I2C bursts) small.
    c.h = 0;   // hue is ignored by the board's STM32 firmware; keep groups few
    c.s = (uint8_t)((c.s / 5) * 5);
    c.v = (uint8_t)(((uint32_t)c.v * brightnessPct / 100 / 4) * 4);
    int slot = -1;
    for (int j = 0; j < n; ++j)
      if (ops[j].color == c) { slot = j; break; }
    if (slot < 0) {
      slot = n++;
      ops[slot].color = c;
      ops[slot].mask = 0;
    }
    ops[slot].mask |= (uint16_t)(1u << i);
  }
  return n;
}

}  // namespace core
