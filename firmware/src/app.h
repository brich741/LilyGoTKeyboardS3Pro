#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

#include "config.h"

// Shared state between the main loop, the web server task, the LED task and the
// action task. Lock order: cfgMtx before hwMtx (never take cfgMtx while holding hwMtx).
namespace app {

extern SemaphoreHandle_t cfgMtx;   // guards cfg, generation, activePage
extern SemaphoreHandle_t hwMtx;    // guards I2C/SPI: displays, keys, LEDs

extern core::Config cfg;
extern uint32_t saveSeq;                   // bumped (under cfgMtx) for every config snapshot that gets saved
extern volatile uint32_t generation;  // bumped whenever cfg is replaced
extern volatile uint32_t redrawGeneration;  // bumped when icon files change
extern volatile uint32_t requestedPage;  // set by web API; UINT32_MAX = none
extern volatile bool restartApRequested;
extern volatile bool factoryResetRequested;

// Counters for the debug console (`keys`): where do physical presses get lost?
struct Diag {
  volatile uint32_t edges[5];     // raw press edges seen per input (4 keys + encoder push)
  volatile uint32_t events[5];    // gesture events (single/double/hold start) dispatched per input
  volatile uint32_t maxLoopMs;    // slowest main-loop pass since reset
  volatile uint32_t maxReadMs;    // slowest key read (includes waiting for the I2C lock)
};
extern Diag diag;

struct Lock {
  SemaphoreHandle_t m;
  explicit Lock(SemaphoreHandle_t mtx) : m(mtx) { xSemaphoreTake(m, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

}  // namespace app
