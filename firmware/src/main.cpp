// Claude Macro Pad: LilyGo T-Keyboard S3 Pro firmware.
// Keys -> gestures -> USB HID actions; screens + LEDs show per-key state; a Wi-Fi AP
// serves the phone UI that edits everything live. See docs/CONFIG.md.
#include <Arduino.h>
#include <TKeyboardS3Pro.h>

#include "actions.h"
#include "app.h"
#include "config.h"
#include "debuglog.h"
#include "gesture.h"
#include "ledstrip.h"
#include "net.h"
#include "screens.h"
#include "storage.h"
#include "visual.h"

namespace app {
SemaphoreHandle_t cfgMtx;
SemaphoreHandle_t hwMtx;
core::Config cfg;
volatile uint32_t generation = 1;
volatile uint32_t redrawGeneration = 1;
volatile uint32_t requestedPage = UINT32_MAX;
volatile bool restartApRequested = false;
volatile bool factoryResetRequested = false;
}  // namespace app

namespace {

constexpr int kInputs = 5;                 // 4 keys + encoder push
constexpr int kEncoder = 4;
constexpr uint32_t kWifiInfoHoldMs = 3000;
constexpr uint32_t kBannerMs = 1000;
constexpr uint32_t kSaveDelayMs = 2000;

core::GestureEngine eng[kInputs];
core::KeyVisual vis[kInputs];
core::KeyLook drawn[core::kKeyCount];
bool drawnValid[core::kKeyCount];

uint32_t seenGeneration = 0;
uint32_t seenRedraw = 0;
uint8_t page = 0;
bool forceRedraw = true;
bool infoShown = false;
uint32_t bannerUntil = 0;
bool bannerShown = false;
std::string bannerLabel;
uint32_t saveAt = 0;
bool savePending = false;
uint32_t lastInput = 0;
bool dimmed = false;

const core::KeyCfg& keyCfg(const core::PageCfg& p, int i) {
  return i < core::kKeyCount ? p.keys[i] : p.encoder;
}

// ---- all helpers below run with cfgMtx held ------------------------------

void configureEngines() {
  const core::Settings& s = app::cfg.settings;
  const core::PageCfg& p = app::cfg.pages[page];
  for (int i = 0; i < kInputs; ++i) {
    core::GestureTiming t;
    const core::KeyCfg& k = keyCfg(p, i);
    t.hasDouble = k.dbl.present;
    t.hasHold = i == kEncoder ? false : k.hold.present;
    t.doubleMs = s.doubleMs;
    t.holdMs = s.holdMs;
    eng[i].configure(t);
  }
}

void resetVisuals() {
  const core::PageCfg& p = app::cfg.pages[page];
  for (int i = 0; i < kInputs; ++i) vis[i].reset(keyCfg(p, i));
  forceRedraw = true;
}

void dispatch(int i, core::GEvent ev, uint32_t now) {
  debuglog::printf("[evt] input=%d event=%d page=%u\n", i, (int)ev, (unsigned)page);
  const core::PageCfg& p = app::cfg.pages[page];
  const core::KeyCfg& k = keyCfg(p, i);
  uint16_t revert = app::cfg.settings.revertMs;
  switch (ev) {
    case core::GEvent::Single:
      if (!k.single.present) break;
      actions::enqueue(i, k.single.steps, true);
      vis[i].onSingle(k, now, revert);
      break;
    case core::GEvent::Double:
      if (!k.dbl.present) break;
      actions::enqueue(i, k.dbl.steps, true);
      vis[i].onDouble(k, now, revert);
      break;
    case core::GEvent::HoldStart:
      actions::enqueue(i, k.hold.start, false);
      vis[i].onHoldStart(k);
      break;
    case core::GEvent::HoldEnd:
      actions::enqueue(i, k.hold.end, true);   // releases anything still held
      vis[i].onHoldEnd(k, now, revert);
      break;
    case core::GEvent::None:
      break;
  }
}

void switchPage(int target, uint32_t now) {
  int n = (int)app::cfg.pages.size();
  target = ((target % n) + n) % n;
  if (target == page) return;

  // End any hold in progress on the page we are leaving.
  for (int i = 0; i < kEncoder; ++i)
    if (eng[i].reset()) dispatch(i, core::GEvent::HoldEnd, now);
  eng[kEncoder].reset();
  actions::releaseAll();

  page = (uint8_t)target;
  app::cfg.settings.activePage = page;
  configureEngines();
  resetVisuals();
  bannerUntil = now + kBannerMs;
  bannerShown = false;
  savePending = true;
  saveAt = now + kSaveDelayMs;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  app::cfgMtx = xSemaphoreCreateMutex();
  app::hwMtx = xSemaphoreCreateMutex();

  bool storageOk = storage::begin();
  bool wasDefault = false;
  app::cfg = storageOk ? storage::load(&wasDefault) : core::defaultConfig();
  if (storageOk && wasDefault) storage::save(app::cfg);

  TKeyboardS3Pro.begin();
  screens::begin();
  screens::setBrightness(app::cfg.settings.brightness);

  // Recovery: hold the encoder button while powering up to reset Wi-Fi credentials.
  TKeyboardS3Pro.update();
  if (TKeyboardS3Pro.keyState() & tkbpro::KEY_5) {
    core::Settings defaults;
    app::cfg.settings.apSsid = defaults.apSsid;
    app::cfg.settings.apPass = defaults.apPass;
    storage::save(app::cfg);
    screens::message(0, "Wi-Fi reset", "Release the", "encoder button");
    delay(1500);
  }

  page = app::cfg.settings.activePage < app::cfg.pages.size() ? app::cfg.settings.activePage : 0;

  actions::begin();
  ledstrip::begin();
  net::begin();

  lastInput = millis();
}

void loop() {
  uint32_t now = millis();

  {
    app::Lock hw(app::hwMtx);
    TKeyboardS3Pro.update();
  }
  uint8_t keys = TKeyboardS3Pro.keyState();
  long encDelta = TKeyboardS3Pro.encoder.delta();

  core::KeyLook looks[core::kKeyCount];
  core::LedCfg led;
  uint16_t zones[core::kKeyCount];
  uint8_t ledBrightness, screenBrightness = 0;
  uint16_t dimMinutes;
  bool drawBanner = false, clearBanner = false, showInfo = false, hideInfo = false;
  std::string ssid, pass;
  std::string iconNames[core::kKeyCount];
  bool doSave = false;
  core::Config toSave;
  bool resetNow = false;

  {
    app::Lock l(app::cfgMtx);

    // Config replaced from the web UI (or first run).
    if (seenGeneration != app::generation) {
      seenGeneration = app::generation;
      if (page >= app::cfg.pages.size()) page = 0;
      app::cfg.settings.activePage = page;
      for (int i = 0; i < kInputs; ++i) eng[i].reset();
      actions::releaseAll();
      configureEngines();
      resetVisuals();
      screenBrightness = app::cfg.settings.brightness;
      screens::setBrightness(dimmed ? 0 : screenBrightness);
    }
    if (seenRedraw != app::redrawGeneration) {
      seenRedraw = app::redrawGeneration;
      forceRedraw = true;
    }
    if (app::requestedPage != UINT32_MAX) {
      switchPage((int)app::requestedPage, now);
      app::requestedPage = UINT32_MAX;
    }

    // Any activity wakes the screens.
    bool anyDown = keys != 0;
    if (anyDown || encDelta != 0) {
      lastInput = now;
      if (dimmed) {
        dimmed = false;
        ledstrip::setDimmed(false);
        screens::setBrightness(app::cfg.settings.brightness);
      }
    }
    dimMinutes = app::cfg.settings.dimMinutes;
    if (!dimmed && dimMinutes && now - lastInput > (uint32_t)dimMinutes * 60000u) {
      dimmed = true;
      ledstrip::setDimmed(true);
      screens::setBrightness(0);
    }

    if (infoShown) {
      // Any key press or knob turn closes the Wi-Fi info screen and is swallowed.
      if (keys != 0 || encDelta != 0) {
        infoShown = false;
        hideInfo = true;
        for (int i = 0; i < kInputs; ++i) eng[i].reset();
      }
    } else {
      if (encDelta != 0) switchPage((int)page + (encDelta > 0 ? 1 : -1), now);

      for (int i = 0; i < kInputs; ++i) {
        bool down = (keys & tkbpro::keyMaskFromIndex((uint8_t)i)) != 0;
        core::GEvent ev[core::GestureEngine::kMaxEvents];
        int n = eng[i].update(down, now, ev);
        for (int e = 0; e < n; ++e) dispatch(i, ev[e], now);
      }

      // Encoder push held 3 s -> Wi-Fi info (reserved gesture).
      if (eng[kEncoder].isDown() && now - eng[kEncoder].downSinceMs() >= kWifiInfoHoldMs) {
        eng[kEncoder].reset();   // swallow the rest of this press
        infoShown = true;
        showInfo = true;
        ssid = app::cfg.settings.apSsid;
        pass = app::cfg.settings.apPass;
      }
    }

    for (int i = 0; i < kInputs; ++i) vis[i].tick(keyCfg(app::cfg.pages[page], i), now);
    for (int i = 0; i < core::kKeyCount; ++i) looks[i] = vis[i].look();

    led = app::cfg.pages[page].led;
    for (int i = 0; i < core::kKeyCount; ++i) zones[i] = app::cfg.settings.ledZones[i];
    ledBrightness = app::cfg.settings.ledBrightness;

    if (bannerUntil && !bannerShown && !infoShown) {
      drawBanner = true;
      bannerShown = true;
      bannerLabel = std::to_string(page + 1) + "/" + std::to_string(app::cfg.pages.size()) + " " +
                   app::cfg.pages[page].name;
    } else if (bannerUntil && bannerShown && (int32_t)(now - bannerUntil) >= 0) {
      clearBanner = true;
      bannerUntil = 0;
    }

    if (savePending && (int32_t)(now - saveAt) >= 0) {
      savePending = false;
      doSave = true;
      toSave = app::cfg;
    }
    if (app::factoryResetRequested) resetNow = true;
  }

  ledstrip::set(led, zones, looks, ledBrightness);

  if (resetNow) {
    storage::wipe();
    delay(300);
    ESP.restart();
  }
  if (doSave) storage::save(toSave);

  // ---- drawing (slow; no config lock held) ----
  if (showInfo) {
    screens::message(0, "Wi-Fi", ssid.c_str());
    screens::message(1, "Password", pass.c_str());
    screens::message(2, "Open", net::ipString().c_str(), "in your browser");
    screens::message(3, "Any key", "to close");
  }
  if (hideInfo) forceRedraw = true;

  if (!infoShown) {
    for (int i = 0; i < core::kKeyCount; ++i) {
      if (forceRedraw || !drawnValid[i] || looks[i] != drawn[i]) {
        std::string label = "Key " + std::to_string(i + 1);
        screens::drawIcon(i, looks[i].icon, label.c_str());
        drawn[i] = looks[i];
        drawnValid[i] = true;
        if (bannerShown && bannerUntil) drawBanner = true;  // keep the banner over fresh icons
      }
    }
    forceRedraw = false;
    if (drawBanner)
      for (int i = 0; i < core::kKeyCount; ++i) screens::banner(i, bannerLabel.c_str());
    if (clearBanner) {
      for (int i = 0; i < core::kKeyCount; ++i) {
        screens::drawIcon(i, drawn[i].icon, ("Key " + std::to_string(i + 1)).c_str());
      }
    }
  }

  debuglog::poll();
  delay(2);
}
