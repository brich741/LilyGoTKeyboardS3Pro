#include "runner.h"

#include "keymap.h"

namespace core {

namespace {
constexpr uint8_t kModFlags[] = {MOD_CTRL, MOD_SHIFT, MOD_ALT, MOD_GUI};
constexpr uint32_t kTapMs = 10;
}  // namespace

void StepRunner::press(uint8_t usage) {
  if (!usage) return;
  if (refs_[usage] < 255) ++refs_[usage];
  if (refs_[usage] == 1) sink_.keyDown(usage);
}

void StepRunner::release(uint8_t usage) {
  if (!usage || refs_[usage] == 0) return;
  if (--refs_[usage] == 0) sink_.keyUp(usage);
}

int StepRunner::heldCount() const {
  int n = 0;
  for (int i = 0; i < 256; ++i)
    if (refs_[i]) ++n;
  return n;
}

void StepRunner::releaseAll() {
  for (int i = 255; i >= 0; --i) {
    if (refs_[i]) {
      refs_[i] = 0;
      sink_.keyUp((uint8_t)i);
    }
  }
}

void StepRunner::runStep(const Step& s) {
  switch (s.type) {
    case Step::Key: {
      switch (s.mode) {
        case Step::Tap:
          for (uint8_t f : kModFlags)
            if (s.mods & f) press(modUsage(f));
          press(s.key);
          sink_.sleepMs(kTapMs);
          release(s.key);
          for (int i = 3; i >= 0; --i)
            if (s.mods & kModFlags[i]) release(modUsage(kModFlags[i]));
          sink_.sleepMs(kTapMs);
          break;
        case Step::Down:
          for (uint8_t f : kModFlags)
            if (s.mods & f) press(modUsage(f));
          press(s.key);
          break;
        case Step::Up:
          release(s.key);
          for (int i = 3; i >= 0; --i)
            if (s.mods & kModFlags[i]) release(modUsage(kModFlags[i]));
          break;
      }
      break;
    }
    case Step::Text:
      for (char c : s.text) sink_.typeChar(c);
      break;
    case Step::Media:
      sink_.mediaPress(s.media);
      sink_.sleepMs(20);
      sink_.mediaRelease();
      break;
    case Step::Delay:
      sink_.sleepMs(s.ms);
      break;
  }
}

void StepRunner::run(const Steps& steps) {
  for (const Step& s : steps) runStep(s);
}

}  // namespace core
