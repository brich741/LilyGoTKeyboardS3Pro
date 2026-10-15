#include "actions.h"

#include <Arduino.h>
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <deque>

#include "debuglog.h"
#include "runner.h"

namespace actions {

namespace {

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;

// Sits between the per-key runners and the real USB device: a usage stays down
// while ANY runner holds it (e.g. two keys both holding Ctrl).
class SharedHid : public core::HidSink {
 public:
  void keyDown(uint8_t u) override {
    if (refs_[u]++ == 0) Keyboard.pressRaw(u);
  }
  void keyUp(uint8_t u) override {
    if (refs_[u] == 0) return;
    if (--refs_[u] == 0) Keyboard.releaseRaw(u);
  }
  void typeChar(char c) override {
    Keyboard.write((uint8_t)c);
    vTaskDelay(pdMS_TO_TICKS(6));
  }
  void mediaPress(uint16_t u) override { Consumer.press(u); }
  void mediaRelease() override { Consumer.release(); }
  void sleepMs(uint32_t ms) override {
    if (ms) vTaskDelay(pdMS_TO_TICKS(ms));
  }

 private:
  uint8_t refs_[256] = {0};
};

struct Job {
  int runner;
  core::Steps steps;
  bool releaseAfter;
  bool releaseOnly;
};

SharedHid hid;
core::StepRunner* runners[kRunnerCount];
std::deque<Job> queue;
SemaphoreHandle_t queueMtx;
TaskHandle_t worker;

void push(Job&& j) {
  xSemaphoreTake(queueMtx, portMAX_DELAY);
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
      runners[j.runner]->run(j.steps);
      if (j.releaseAfter) runners[j.runner]->releaseAll();
    }
  }
}

void onUsbEvent(void*, esp_event_base_t, int32_t id, void*) {
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
  USB.PID(0x4B50);
  USB.onEvent(onUsbEvent);
  debuglog::begin();   // CDC must be registered before USB.begin()
  USB.begin();
}

void enqueue(int runner, const core::Steps& steps, bool releaseAfter) {
  if (runner < 0 || runner >= kRunnerCount) return;
  push(Job{runner, steps, releaseAfter, false});
}

void releaseAll(int runner) {
  push(Job{runner, {}, false, true});
}

}  // namespace actions
