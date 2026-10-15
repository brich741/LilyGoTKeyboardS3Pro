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

// Queue "release everything" for one runner, or all runners when runner < 0.
void releaseAll(int runner = -1);

}  // namespace actions
