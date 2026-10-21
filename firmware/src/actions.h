#pragma once
#include "config.h"

// USB keyboard output. Steps are executed on a dedicated task so slow steps
// (delays, long text) never block the UI. Every key (4 keys + encoder push) has
// its own StepRunner so one key's held keys are never released by another.
namespace actions {

constexpr int kRunnerCount = 6;   // 0..4 = keys + encoder push, 5 = web "Test" button
constexpr int kTestRunner = 5;

void begin();   // starts USB (HID keyboard + consumer control) and the worker task

// Queue steps for a runner. If releaseAfter, every key that runner still holds is
// released once the steps have run.
void enqueue(int runner, const core::Steps& steps, bool releaseAfter);

// How long a tap step holds its key down (settings.tapMs); applies to all runners.
void setTapMs(uint16_t ms);

// Queue "release everything" for one runner, or all runners when runner < 0.
void releaseAll(int runner = -1);

// USB bus health, for diagnosing "the PC ignored my first presses" (debug console: `usb`).
struct UsbStats {
  uint32_t suspends, resumes;   // bus suspend/resume events from the host
  uint32_t wakeups;             // times a press found the bus suspended and sent a remote wakeup
  uint32_t wakeFails;           // ... and the host still had not resumed after ~200 ms
  bool suspendedNow;
};
UsbStats usbStats();

// Action queue health: queued = accepted, dropped = queue full, superseded = cancelled by a release/page switch before it ran.
struct JobStats {
  uint32_t queued, dropped, superseded, ran;
};
JobStats jobStats();

}  // namespace actions
