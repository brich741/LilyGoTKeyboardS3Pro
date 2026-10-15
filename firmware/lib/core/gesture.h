#pragma once
#include <stdint.h>

namespace core {

enum class GEvent : uint8_t { None, Single, Double, HoldStart, HoldEnd };

struct GestureTiming {
  bool hasDouble = false;   // a double-press action exists -> single waits for the window
  bool hasHold = false;     // a hold action exists -> long press is a hold, not a click
  uint16_t doubleMs = 250;
  uint16_t holdMs = 400;
  uint16_t debounceMs = 15;
};

// Turns raw pressed/released samples into Single / Double / HoldStart / HoldEnd.
// One instance per physical key. Pure logic: no hardware, time is passed in.
class GestureEngine {
 public:
  static constexpr int kMaxEvents = 2;

  void configure(const GestureTiming& t) { cfg_ = t; }

  // Feed one sample. Returns the number of events written to `out`
  // (at most kMaxEvents). `nowMs` must be monotonic (wraparound-safe).
  int update(bool pressedRaw, uint32_t nowMs, GEvent out[kMaxEvents]);

  // Forget any in-progress gesture (e.g. on page switch). If a hold is active
  // returns true so the caller can fire the HoldEnd action itself.
  bool reset();

  bool isHolding() const { return state_ == State::Holding; }
  bool isDown() const { return pressed_; }
  uint32_t downSinceMs() const { return t0_; }

 private:
  enum class State : uint8_t { Idle, Down, WaitSecond, Down2, Holding };
  GestureTiming cfg_;
  State state_ = State::Idle;
  bool pressed_ = false;       // debounced level
  bool lastRaw_ = false;
  uint32_t rawSince_ = 0;
  uint32_t t0_ = 0;            // press start
  uint32_t deadline_ = 0;      // end of double-press window
  bool ignoreUntilRelease_ = false;
};

}  // namespace core
