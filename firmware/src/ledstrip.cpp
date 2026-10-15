#include "ledstrip.h"

#include <TKeyboardS3Pro.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app.h"
#include "leds.h"

namespace ledstrip {

namespace {

SemaphoreHandle_t stateMtx;
core::LedCfg pageCfg;
uint16_t zones[core::kKeyCount];
core::KeyLook looks[core::kKeyCount];
uint8_t brightness = 40;
bool hueOk = false;
bool dimmed = false;
uint16_t probeMask = 0;
volatile bool paused = false;

bool havePrev = false;
core::Rgb prev[core::kLedCount];
bool modeSet = false;

// The STM32 keeps a framebuffer: writing LED-select bits paints those LEDs with the *current*
// hue/sat/brightness registers, "clear" wipes the whole buffer, "show" latches it to the strip.
// So: paint every changed group, then show ONCE (never clear between groups).
void paint(const core::PaintOp& op) {
  TKeyboardS3Pro.ledSetColor(op.color.h, op.color.s);
  TKeyboardS3Pro.ledSetBrightness(op.color.v);
  TKeyboardS3Pro.ledSelect(op.mask);
}

void task(void*) {
  for (;;) {
    if (paused) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    core::LedCfg cfg;
    uint16_t z[core::kKeyCount];
    core::KeyLook lk[core::kKeyCount];
    uint8_t bri;
    bool dim;
    uint16_t probing;
    bool hue;
    xSemaphoreTake(stateMtx, portMAX_DELAY);
    cfg = pageCfg;
    memcpy(z, zones, sizeof(z));
    for (int i = 0; i < core::kKeyCount; ++i) lk[i] = looks[i];
    bri = brightness;
    dim = dimmed;
    probing = probeMask;
    hue = hueOk;
    xSemaphoreGive(stateMtx);

    core::Rgb frame[core::kLedCount];
    if (probing) {
      for (int i = 0; i < core::kLedCount; ++i)
        frame[i] = (probing >> i) & 1 ? core::Rgb{255, 255, 255} : core::Rgb();
    } else if (dim) {
      for (auto& p : frame) p = core::Rgb();
    } else {
      core::composeLeds(cfg, z, lk, millis(), frame, hue);
    }

    core::PaintOp ops[core::kLedCount];
    int n = core::planPaint(havePrev ? prev : nullptr, frame, probing ? 100 : bri, ops, hue);
    if (n > 0) {
      app::Lock l(app::hwMtx);
      if (!modeSet) {
        TKeyboardS3Pro.ledSetMode(tkbpro::LED_MODE_FREE);
        TKeyboardS3Pro.ledClear();
        modeSet = true;
      }
      for (int i = 0; i < n; ++i) paint(ops[i]);
      TKeyboardS3Pro.ledShow();
      memcpy(prev, frame, sizeof(prev));
      havePrev = true;
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

}  // namespace

void begin() {
  stateMtx = xSemaphoreCreateMutex();
  for (int i = 0; i < core::kKeyCount; ++i) zones[i] = 0;
  xTaskCreatePinnedToCore(task, "leds", 4096, nullptr, 2, nullptr, 0);
}

void set(const core::LedCfg& page, const uint16_t z[core::kKeyCount],
         const core::KeyLook l[core::kKeyCount], uint8_t brightnessPct, bool hue) {
  xSemaphoreTake(stateMtx, portMAX_DELAY);
  pageCfg = page;
  for (int i = 0; i < core::kKeyCount; ++i) {
    zones[i] = z[i];
    looks[i] = l[i];
  }
  brightness = brightnessPct;
  hueOk = hue;
  xSemaphoreGive(stateMtx);
}

void setDimmed(bool d) {
  xSemaphoreTake(stateMtx, portMAX_DELAY);
  dimmed = d;
  xSemaphoreGive(stateMtx);
}

void setPaused(bool p) { paused = p; }

void probe(uint16_t mask) {
  xSemaphoreTake(stateMtx, portMAX_DELAY);
  probeMask = mask & 0x3FFF;
  xSemaphoreGive(stateMtx);
}

}  // namespace ledstrip
