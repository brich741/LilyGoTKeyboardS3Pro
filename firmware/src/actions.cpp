#include "actions.h"

#include <Arduino.h>
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <tusb.h>

#include <deque>

#include "debuglog.h"
#include "runner.h"

namespace actions {

namespace {

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;

// Sits between the per-key runners and the real USB device: a usage stays down
// while ANY runner holds it (e.g. two keys both holding Ctrl).
volatile uint32_t abortGen = 0;   // bumped by releaseAll()
volatile uint32_t jobGen = 0;     // generation of the job the worker is running

volatile uint32_t usbSuspends = 0, usbResumes = 0, usbWakeups = 0, usbWakeFails = 0;

// The Arduino HID layer drops a report silently while the host has the bus suspended (Windows
// idles USB devices), which looks like "the first presses after a pause do nothing". Wake the
// host first and wait for the bus to come back before sending.
void wakeHost() {
  if (!tud_suspended()) return;
  tud_remote_wakeup();
  usbWakeups = usbWakeups + 1;
  for (int i = 0; i < 50 && tud_suspended(); ++i) vTaskDelay(pdMS_TO_TICKS(4));   // up to ~200 ms
  if (tud_suspended()) usbWakeFails = usbWakeFails + 1;
}

class SharedHid : public core::HidSink {
 public:
  void keyDown(uint8_t u) override {
    wakeHost();
    if (refs_[u]++ == 0) Keyboard.pressRaw(u);
  }
  void keyUp(uint8_t u) override {
    if (refs_[u] == 0) return;
    if (--refs_[u] == 0) Keyboard.releaseRaw(u);
  }
  void typeChar(char c) override {
    wakeHost();
    Keyboard.write((uint8_t)c);
    vTaskDelay(pdMS_TO_TICKS(6));
  }
  bool shouldAbort() override { return abortGen != jobGen; }
  void mediaPress(uint16_t u) override {
    wakeHost();
    Consumer.press(u);
  }
  void mediaRelease() override { Consumer.release(); }
  void sleepMs(uint32_t ms) override {
    while (ms) {   // sleep in slices so a release/page switch can interrupt a long delay
      uint32_t t = ms > 20 ? 20 : ms;
      vTaskDelay(pdMS_TO_TICKS(t));
      ms -= t;
      if (abortGen != jobGen) return;
    }
  }

 private:
  uint8_t refs_[256] = {0};
};

struct Job {
  int runner;
  core::Steps steps;
  bool releaseAfter;
  bool releaseOnly;
  uint32_t gen;   // abortGen when queued; a later releaseAll() invalidates older jobs
};

constexpr size_t kMaxQueue = 16;

SharedHid hid;
core::StepRunner* runners[kRunnerCount];
std::deque<Job> queue;
SemaphoreHandle_t queueMtx;
TaskHandle_t worker;

volatile uint32_t jobsQueued = 0, jobsDropped = 0, jobsSuperseded = 0, jobsRan = 0;

void push(Job&& j) {
  xSemaphoreTake(queueMtx, portMAX_DELAY);
  if (!j.releaseOnly && queue.size() >= kMaxQueue) {   // mashing keys / spamming /api/test
    jobsDropped = jobsDropped + 1;
    xSemaphoreGive(queueMtx);
    return;
  }
  if (!j.releaseOnly) jobsQueued = jobsQueued + 1;
  queue.push_back(std::move(j));
  xSemaphoreGive(queueMtx);
  xTaskNotifyGive(worker);
}

void workerTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      Job j;
      xSemaphoreTake(queueMtx, portMAX_DELAY);
      if (queue.empty()) {
        xSemaphoreGive(queueMtx);
        break;
      }
      j = std::move(queue.front());
      queue.pop_front();
      xSemaphoreGive(queueMtx);

      if (j.releaseOnly) {
        if (j.runner < 0) {
          for (auto* r : runners) r->releaseAll();
        } else {
          runners[j.runner]->releaseAll();
        }
        continue;
      }
      if (j.gen != abortGen) {   // superseded by a release since it was queued
        jobsSuperseded = jobsSuperseded + 1;
        continue;
      }
      jobGen = j.gen;
      jobsRan = jobsRan + 1;
      runners[j.runner]->run(j.steps);
      if (j.releaseAfter) runners[j.runner]->releaseAll();
    }
  }
}

void onUsbEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (id == ARDUINO_USB_SUSPEND_EVENT) usbSuspends = usbSuspends + 1;
  if (id == ARDUINO_USB_RESUME_EVENT) usbResumes = usbResumes + 1;
  // Host went away or suspended: never leave keys logically held.
  if (id == ARDUINO_USB_SUSPEND_EVENT || id == ARDUINO_USB_STOPPED_EVENT) releaseAll(-1);
}

}  // namespace

void begin() {
  queueMtx = xSemaphoreCreateMutex();
  for (auto& r : runners) r = new core::StepRunner(hid);

  xTaskCreatePinnedToCore(workerTask, "actions", 6144, nullptr, 3, &worker, 1);

  Keyboard.begin();
  Consumer.begin();
  USB.productName("Claude Macro Pad");
  USB.manufacturerName("LilyGo");
  USB.serialNumber("CLAUDEPAD-0001");
  // The default VID:PID (303A:1001) belongs to the chip's JTAG/serial port; Windows binds that
  // driver to interface 0 and the HID keyboard ends up in "Error". Use our own product id.
  USB.VID(0x303A);
  USB.PID(0x4B50);  USB.onEvent(onUsbEvent);
  debuglog::begin();   // CDC must be registered before USB.begin()
  USB.begin();
}

void enqueue(int runner, const core::Steps& steps, bool releaseAfter) {
  if (runner < 0 || runner >= kRunnerCount) return;
  push(Job{runner, steps, releaseAfter, false, abortGen});
}

JobStats jobStats() { return {jobsQueued, jobsDropped, jobsSuperseded, jobsRan}; }

UsbStats usbStats() { return {usbSuspends, usbResumes, usbWakeups, usbWakeFails, tud_suspended()}; }

void setTapMs(uint16_t ms) {
  for (auto* r : runners) r->setTapMs(ms);
}

void releaseAll(int runner) {
  abortGen = abortGen + 1;   // interrupt the running job and drop queued ones
  push(Job{runner, {}, false, true, abortGen});
}

}  // namespace actions
