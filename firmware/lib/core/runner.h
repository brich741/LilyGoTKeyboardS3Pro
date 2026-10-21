#pragma once
#include <stdint.h>

#include "config.h"

namespace core {

// What the runner needs from the USB side. Implemented by the device layer
// (and by a recorder in the unit tests).
class HidSink {
 public:
  virtual ~HidSink() {}
  virtual void keyDown(uint8_t usage) = 0;     // press one HID usage (modifiers are 0xE0..0xE7)
  virtual void keyUp(uint8_t usage) = 0;
  virtual void typeChar(char c) = 0;           // press+release an ASCII char (US layout)
  virtual void mediaPress(uint16_t usage) = 0;
  virtual void mediaRelease() = 0;
  virtual void sleepMs(uint32_t ms) = 0;
  virtual bool shouldAbort() { return false; }   // polled between steps/characters
};

// Executes Step lists against a HidSink and remembers which keys are still held
// down so they can always be released (no stuck keys).
class StepRunner {
 public:
  explicit StepRunner(HidSink& sink) : sink_(sink) {}

  void run(const Steps& steps);
  void runStep(const Step& step);

  // Release every key this runner pressed and has not yet released.
  void releaseAll();

  int heldCount() const;

  // How long a tap holds its key down. Some apps (hotkey/dictation tools that poll key state)
  // miss the 10 ms default of the old firmware; a human press lasts ~80 ms.
  void setTapMs(uint16_t ms) { tapMs_ = ms; }

 private:
  uint16_t tapMs_ = 60;
  void press(uint8_t usage);
  void release(uint8_t usage);

  HidSink& sink_;
  uint8_t refs_[256] = {0};
};

}  // namespace core
