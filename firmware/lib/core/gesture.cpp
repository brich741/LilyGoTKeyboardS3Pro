#include "gesture.h"

namespace core {

namespace {
// Wraparound-safe "a is at or after b".
inline bool reached(uint32_t now, uint32_t target) { return (int32_t)(now - target) >= 0; }
}  // namespace

int GestureEngine::update(bool raw, uint32_t now, GEvent out[kMaxEvents]) {
  int n = 0;

  // Debounce: a new level is accepted only once the raw input has held it
  // steady for debounceMs.
  if (raw != lastRaw_) {
    lastRaw_ = raw;
    rawSince_ = now;
  }
  if (raw != pressed_ && reached(now, rawSince_ + cfg_.debounceMs)) pressed_ = raw;

  if (ignoreUntilRelease_) {
    if (!pressed_) ignoreUntilRelease_ = false;
    return 0;
  }

  switch (state_) {
    case State::Idle:
      if (pressed_) {
        state_ = State::Down;
        t0_ = now;
      }
      break;

    case State::Down:
      if (!pressed_) {
        if (cfg_.hasDouble) {
          state_ = State::WaitSecond;
          deadline_ = now + cfg_.doubleMs;
        } else {
          state_ = State::Idle;
          out[n++] = GEvent::Single;
        }
      } else if (cfg_.hasHold && reached(now, t0_ + cfg_.holdMs)) {
        state_ = State::Holding;
        out[n++] = GEvent::HoldStart;
      }
      break;

    case State::WaitSecond:
      if (pressed_) {
        state_ = State::Down2;
        t0_ = now;
      } else if (reached(now, deadline_)) {
        state_ = State::Idle;
        out[n++] = GEvent::Single;
      }
      break;

    case State::Down2:
      if (!pressed_) {
        state_ = State::Idle;
        out[n++] = GEvent::Double;
      } else if (cfg_.hasHold && reached(now, t0_ + cfg_.holdMs)) {
        state_ = State::Holding;
        out[n++] = GEvent::Single;     // the first click was real
        out[n++] = GEvent::HoldStart;
      }
      break;

    case State::Holding:
      if (!pressed_) {
        state_ = State::Idle;
        out[n++] = GEvent::HoldEnd;
      }
      break;
  }
  return n;
}

bool GestureEngine::reset() {
  bool wasHolding = state_ == State::Holding;
  state_ = State::Idle;
  // If the finger is still on the key, swallow it until it is released.
  ignoreUntilRelease_ = pressed_;
  return wasHolding;
}

}  // namespace core
