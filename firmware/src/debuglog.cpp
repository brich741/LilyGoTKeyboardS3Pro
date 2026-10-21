#include "debuglog.h"

#include <ArduinoJson.h>
#include <TKeyboardS3Pro.h>
#include <USB.h>
#include <USBCDC.h>
#include <stdarg.h>

#include "actions.h"
#include "app.h"
#include "keymap.h"
#include "net.h"
#include "ledstrip.h"
#include "screens.h"
#include "storage.h"

namespace debuglog {

namespace {

USBCDC cdc(0);
String line;

void reply(const char* msg) { cdc.println(msg); }

uint16_t parseMask(const char* s) { return (uint16_t)strtoul(s, nullptr, 0) & 0x3FFF; }


// ---- raw LED register access with optional pacing, for bring-up experiments ----
void wr(uint8_t cmd, uint8_t val, uint32_t gapUs) {
  TwoWire& w = TKeyboardS3Pro.bus();
  w.beginTransmission(tkbpro::DEFAULT_ADDRESS);
  w.write(cmd);
  w.write(val);
  w.endTransmission();
  if (gapUs) delayMicroseconds(gapUs);
}

void rawSelect(uint16_t mask, uint32_t gapUs) {
  uint8_t c1 = 0, c2 = 0;
  for (uint8_t i = 0; i < 6; ++i)
    if (mask & (1u << i)) c1 |= (uint8_t)(1u << (5 - i));
  for (uint8_t i = 6; i < 14; ++i)
    if (mask & (1u << i)) c2 |= (uint8_t)(1u << (7 - (i - 6)));
  wr(tkbpro::CMD_WR_LED_CONTROL_1, c1, gapUs);
  wr(tkbpro::CMD_WR_LED_CONTROL_2, c2, gapUs);
}

// Paint `mask` with an HSV color into the STM32 framebuffer (no show).
void rawPaint(uint16_t h, uint8_t s, uint8_t v, uint16_t mask, uint32_t gapUs) {
  wr(tkbpro::CMD_WR_LED_COLOR_HUE_H, (uint8_t)(h >> 8), gapUs);
  wr(tkbpro::CMD_WR_LED_COLOR_HUE_L, (uint8_t)(h & 0xFF), gapUs);
  wr(tkbpro::CMD_WR_LED_COLOR_SATURATION, s, gapUs);
  wr(tkbpro::CMD_WR_LED_BRIGHTNESS, v, gapUs);
  rawSelect(mask, gapUs);
}

void rawShow(uint32_t gapUs) { wr(tkbpro::CMD_WR_LED_CONTROL_1, tkbpro::LED_CTRL1_SHOW, gapUs); }
void rawClear(uint32_t gapUs) { wr(tkbpro::CMD_WR_LED_CONTROL_1, tkbpro::LED_CTRL1_CLEAR, gapUs); }

// Two-color split + rainbow variants.
//   showEach: latch to the strip after every paint instead of once at the end.
void splitTest(uint32_t gapUs, bool showEach) {
  app::Lock hw(app::hwMtx);
  rawClear(gapUs);
  rawPaint(0, 100, 40, 0x007F, gapUs);     // LEDs 1-7 red
  if (showEach) rawShow(gapUs);
  rawPaint(240, 100, 40, 0x3F80, gapUs);   // LEDs 8-14 blue
  rawShow(gapUs);
}

void rainbowTest(uint32_t gapUs, bool showEach) {
  app::Lock hw(app::hwMtx);
  rawClear(gapUs);
  for (int i = 0; i < 14; ++i) {
    rawPaint((uint16_t)(i * 360 / 14), 100, 40, (uint16_t)(1u << i), gapUs);
    if (showEach) rawShow(gapUs);
  }
  rawShow(gapUs);
}

void step(int n, const char* name) {
  char num[16];
  snprintf(num, sizeof(num), "Step %d", n);
  screens::message(0, num, name);
  screens::message(1, num, name);
  screens::message(2, num, name);
  screens::message(3, num, name);
  cdc.println(num);
}

void ledTest() {
  ledstrip::setPaused(true);
  {
    app::Lock hw(app::hwMtx);
    TKeyboardS3Pro.ledSetMode(tkbpro::LED_MODE_FREE);
  }
  struct Solid { const char* name; uint16_t h; uint8_t s; } solids[] = {
      {"all RED", 0, 100}, {"all GREEN", 120, 100}, {"all BLUE", 240, 100}, {"all WHITE", 0, 0}};
  int n = 1;
  for (auto& t : solids) {
    step(n++, t.name);
    { app::Lock hw(app::hwMtx); TKeyboardS3Pro.setLeds(t.h, t.s, 40); }
    delay(3500);
  }
  step(n++, "split paced"); splitTest(2000, false); delay(4000);
  step(n++, "split fast");  splitTest(0, false);    delay(4000);
  step(n++, "split show/each"); splitTest(2000, true); delay(4000);
  step(n++, "rainbow paced"); rainbowTest(2000, false); delay(4000);
  step(n++, "rainbow fast");  rainbowTest(0, false);    delay(4000);
  step(n++, "rainbow show/each"); rainbowTest(2000, true); delay(4000);
  cdc.println("done");
  { app::Lock hw(app::hwMtx); rawClear(0); rawShow(0); }
  ledstrip::setPaused(false);
  app::redrawGeneration = app::redrawGeneration + 1;   // main loop repaints the key icons
}

// Probe how this board's STM32 really handles LED registers. Every step is labelled on screen.
void paintAll(uint16_t hue, uint8_t sat, uint8_t bri) {
  app::Lock hw(app::hwMtx);
  rawClear(2000);
  delay(5);
  rawPaint(hue, sat, bri, 0x3FFF, 2000);
  delay(5);
  rawShow(2000);
  delay(2);
  wr(0x02, 0, 0);   // any further transaction flushes the STM32's one-write-late queue
}

void ledProbe() {
  ledstrip::setPaused(true);
  cdc.printf("STM32 firmware version: 0x%02X\n", TKeyboardS3Pro.firmwareVersion());
  int n = 1;
  step(n++, "built-in test1");
  { app::Lock hw(app::hwMtx); wr(tkbpro::CMD_WR_LED_MODE, tkbpro::LED_MODE_TEST1, 2000); }
  delay(5000);
  step(n++, "built-in test2");
  { app::Lock hw(app::hwMtx); wr(tkbpro::CMD_WR_LED_MODE, tkbpro::LED_MODE_TEST2, 2000); }
  delay(5000);
  { app::Lock hw(app::hwMtx); wr(tkbpro::CMD_WR_LED_MODE, tkbpro::LED_MODE_FREE, 2000); }
  struct P { const char* name; uint16_t h; uint8_t s; uint8_t v; } ps[] = {
      {"hue 0 (red)", 0, 100, 40},     {"hue 120 (green)", 120, 100, 40}, {"hue 240 (blue)", 240, 100, 40},
      {"hue 360", 360, 100, 40},        {"sat 0 (white)", 120, 0, 40},     {"sat 50", 120, 50, 40},
      {"bri 5", 120, 100, 5},           {"bri 100", 120, 100, 100}};
  for (auto& q : ps) {
    step(n++, q.name);
    paintAll(q.h, q.s, q.v);
    delay(3500);
  }
  cdc.println("done");
  { app::Lock hw(app::hwMtx); rawClear(0); rawShow(0); }
  ledstrip::setPaused(false);
  app::redrawGeneration = app::redrawGeneration + 1;
}

// Does ANY value written to the hue registers change the colour? Labelled on screen.
void sweepStep(int n, const char* label, uint8_t h5, uint8_t l6, bool lowFirst) {
  step(n, label);
  app::Lock hw(app::hwMtx);
  rawClear(2000);
  delay(5);
  wr(tkbpro::CMD_WR_LED_COLOR_SATURATION, 100, 2000);
  wr(tkbpro::CMD_WR_LED_BRIGHTNESS, 40, 2000);
  if (lowFirst) {
    wr(tkbpro::CMD_WR_LED_COLOR_HUE_L, l6, 2000);
    wr(tkbpro::CMD_WR_LED_COLOR_HUE_H, h5, 2000);
  } else {
    wr(tkbpro::CMD_WR_LED_COLOR_HUE_H, h5, 2000);
    wr(tkbpro::CMD_WR_LED_COLOR_HUE_L, l6, 2000);
  }
  delay(5);
  rawSelect(0x3FFF, 2000);
  delay(5);
  rawShow(2000);
  delay(2);
  wr(0x02, 0, 0);
}

void hueSweep() {
  ledstrip::setPaused(true);
  { app::Lock hw(app::hwMtx); wr(tkbpro::CMD_WR_LED_MODE, tkbpro::LED_MODE_FREE, 2000); }
  int n = 1;
  const uint8_t lows[] = {0, 40, 80, 120, 160, 200, 240};
  for (uint8_t l : lows) {
    char label[24];
    snprintf(label, sizeof(label), "0x06=%u", l);
    sweepStep(n++, label, 0, l, false);
    delay(1600);
  }
  const uint8_t highs[] = {1, 2, 4, 85, 128};
  for (uint8_t h : highs) {
    char label[24];
    snprintf(label, sizeof(label), "0x05=%u", h);
    sweepStep(n++, label, h, 0, false);
    delay(1600);
  }
  sweepStep(n++, "low-first 0x06=120", 0, 120, true);
  delay(1600);
  cdc.println("done");
  { app::Lock hw(app::hwMtx); rawClear(0); rawShow(0); }
  ledstrip::setPaused(false);
  app::redrawGeneration = app::redrawGeneration + 1;
}

void help() {
  reply("commands:");
  reply("  status                      usb/fs/config summary");
  reply("  keys [reset]                presses seen / events / jobs / loop timing (find lost presses)");
  reply("  usb                         bus suspend/resume/wakeup counters");
  reply("  type <text>                send text over USB HID");
  reply("  tap <key> [mods]            e.g. tap d gui   |  tap f13");
  reply("  ledpause 0|1                stop/resume the LED task (use before led tests)");
  reply("  led <h> <s> <v> <mask>      library setLeds(): h 0-360, s/v 0-100, mask hex like 0x7");
  reply("  ledraw <h> <s> <v> <mask>   color+bri+select+show with NO clear");
  reply("  ledclear                    clear flag then show");
  reply("  huesweep                    sweep hue registers 0x05/0x06 (~25 s, labelled)");
  reply("  ledprobe                    labelled probe of the STM32 LED registers (~40 s)");
  reply("  ledtest                     ~30 s automatic LED colour/pacing test (watch the strip)");
  reply("  ledhue 0|1                  enable real LED hues (only after flashing the fixed STM32 firmware)");
  reply("  wifi                        Wi-Fi access point state");
  reply("  dumpconfig                  print the saved config.json");
  reply("  wipe                        erase config + icons and restart (back to defaults)");
  reply("  reset                       restart");
}

void run(char* cmd) {
  char* argv[8];
  int argc = 0;
  for (char* t = strtok(cmd, " \t"); t && argc < 8; t = strtok(nullptr, " \t")) argv[argc++] = t;
  if (!argc) return;
  const char* c = argv[0];

  if (!strcmp(c, "help")) {
    help();
  } else if (!strcmp(c, "status")) {
    cdc.printf("fw %s, heap %u, fs %u/%u\n", FW_VERSION, (unsigned)ESP.getFreeHeap(),
               (unsigned)storage::usedBytes(), (unsigned)storage::totalBytes());
    cdc.printf("usb mounted-ish: keys byte 0x%02X\n", TKeyboardS3Pro.keyState());
    app::Lock l(app::cfgMtx);
    cdc.printf("pages %u, active %u, icons in config %u\n", (unsigned)app::cfg.pages.size(),
               (unsigned)app::cfg.settings.activePage, (unsigned)app::cfg.icons.size());
    for (auto& i : app::cfg.icons)
      cdc.printf("  icon %s file=%s\n", i.id.c_str(), storage::iconExists(i.id) ? "yes" : "MISSING");
  } else if (!strcmp(c, "usb")) {
    actions::UsbStats u = actions::usbStats();
    cdc.printf("usb: %s now; suspends %u, resumes %u, wake-on-press %u (failed %u)\n",
               u.suspendedNow ? "SUSPENDED" : "active", (unsigned)u.suspends, (unsigned)u.resumes,
               (unsigned)u.wakeups, (unsigned)u.wakeFails);
  } else if (!strcmp(c, "keys")) {
    if (argc >= 2 && !strcmp(argv[1], "reset")) {
      for (int i = 0; i < 5; ++i) app::diag.edges[i] = app::diag.events[i] = 0;
      app::diag.maxLoopMs = app::diag.maxReadMs = 0;
    }
    cdc.printf("keys: presses seen   %u %u %u %u enc %u\n", (unsigned)app::diag.edges[0], (unsigned)app::diag.edges[1],
               (unsigned)app::diag.edges[2], (unsigned)app::diag.edges[3], (unsigned)app::diag.edges[4]);
    cdc.printf("keys: events fired   %u %u %u %u enc %u\n", (unsigned)app::diag.events[0], (unsigned)app::diag.events[1],
               (unsigned)app::diag.events[2], (unsigned)app::diag.events[3], (unsigned)app::diag.events[4]);
    actions::JobStats j = actions::jobStats();
    cdc.printf("keys: jobs queued %u dropped %u superseded %u ran %u\n", (unsigned)j.queued, (unsigned)j.dropped,
               (unsigned)j.superseded, (unsigned)j.ran);
    cdc.printf("keys: slowest loop %u ms, slowest key read %u ms\n", (unsigned)app::diag.maxLoopMs, (unsigned)app::diag.maxReadMs);
  } else if (!strcmp(c, "type") && argc >= 2) {
    core::Step s;
    s.type = core::Step::Text;
    String rest;
    for (int i = 1; i < argc; ++i) rest += (i > 1 ? " " : "") + String(argv[i]);
    s.text = rest.c_str();
    actions::enqueue(actions::kTestRunner, {s}, true);
    reply("queued");
  } else if (!strcmp(c, "tap") && argc >= 2) {
    core::Step s;
    s.type = core::Step::Key;
    s.key = core::keyUsage(argv[1]);
    for (int i = 2; i < argc; ++i) s.mods |= core::modFlag(argv[i]);
    if (!s.key) { reply("unknown key"); return; }
    actions::enqueue(actions::kTestRunner, {s}, true);
    reply("queued");
  } else if (!strcmp(c, "ledpause") && argc >= 2) {
    ledstrip::setPaused(atoi(argv[1]) != 0);
    reply("ok");
  } else if ((!strcmp(c, "led") || !strcmp(c, "ledraw")) && argc >= 5) {
    uint16_t h = (uint16_t)atoi(argv[1]);
    uint8_t s = (uint8_t)atoi(argv[2]), v = (uint8_t)atoi(argv[3]);
    uint16_t mask = parseMask(argv[4]);
    app::Lock hw(app::hwMtx);
    if (!strcmp(c, "led")) {
      TKeyboardS3Pro.setLeds(h, s, v, mask);
    } else {
      TKeyboardS3Pro.ledSetMode(tkbpro::LED_MODE_FREE);
      TKeyboardS3Pro.ledSetColor(h, s);
      TKeyboardS3Pro.ledSetBrightness(v);
      TKeyboardS3Pro.ledSelect(mask);
      TKeyboardS3Pro.ledShow();
    }
    reply("ok");
  } else if (!strcmp(c, "huesweep")) {
    hueSweep();
  } else if (!strcmp(c, "ledprobe")) {
    ledProbe();
  } else if (!strcmp(c, "ledtest")) {
    ledTest();
  } else if (!strcmp(c, "ledclear")) {
    app::Lock hw(app::hwMtx);
    TKeyboardS3Pro.ledClear();
    TKeyboardS3Pro.ledShow();
    reply("ok");
  } else if (!strcmp(c, "wifi")) {
    if (argc >= 2) {   // "wifi 0|1": switch the access point (not persisted; the knob screen persists)
      net::requestWifi(atoi(argv[1]) != 0);
      delay(1500);
    }
    cdc.println(net::debugInfo());
  } else if (!strcmp(c, "dumpicon") && argc >= 2) {
    // Print an icon file as hex between BEGIN/END markers (used to back up custom icons before a filesystem flash).
    std::string id = argv[1];
    uint8_t* buf = core::validIconId(id) ? (uint8_t*)malloc(storage::kIconBytes) : nullptr;
    if (!buf) {
      reply("bad id or out of memory");
    } else if (!storage::readIcon(id, buf)) {
      reply("no such icon");
      free(buf);
    } else {
      cdc.printf("ICON %s BEGIN\n", id.c_str());
      static const char* hex = "0123456789abcdef";
      char line[129];
      for (size_t i = 0; i < storage::kIconBytes; i += 64) {
        for (int k = 0; k < 64; ++k) {
          line[2 * k] = hex[buf[i + k] >> 4];
          line[2 * k + 1] = hex[buf[i + k] & 15];
        }
        line[128] = 0;
        cdc.println(line);
        delay(4);
      }
      cdc.println("ICON END");
      free(buf);
    }
  } else if (!strcmp(c, "dumpconfig")) {
    JsonDocument doc;
    {
      app::Lock l(app::cfgMtx);
      core::writeConfig(app::cfg, doc);
    }
    String out;
    serializeJson(doc, out);
    for (size_t i = 0; i < out.length(); i += 200) {
      cdc.print(out.substring(i, i + 200));
      delay(15);
    }
    cdc.println();
  } else if (!strcmp(c, "ledhue") && argc >= 2) {
    bool on = atoi(argv[1]) != 0;
    core::Config toSave;
    uint32_t seq;
    {
      app::Lock l(app::cfgMtx);
      app::cfg.settings.ledHue = on;
      app::generation = app::generation + 1;
      toSave = app::cfg;
      seq = ++app::saveSeq;
    }
    storage::save(toSave, seq);
    cdc.printf("ledHue = %d\n", on ? 1 : 0);
  } else if (!strcmp(c, "wipe")) {
    storage::wipe();
    reply("wiped; restarting");
    delay(200);
    ESP.restart();
  } else if (!strcmp(c, "reset")) {
    ESP.restart();
  } else {
    reply("unknown command (try: help)");
  }
}

}  // namespace

void begin() {
  cdc.begin();
}

void printf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (cdc) cdc.print(buf);
}

void poll() {
  while (cdc.available()) {
    char ch = (char)cdc.read();
    if (ch == '\r' || ch == '\n') {
      if (line.length()) {
        char buf[96];
        line.toCharArray(buf, sizeof(buf));
        line = "";
        run(buf);
      }
    } else if (line.length() < 90) {
      line += ch;
    }
  }
}

}  // namespace debuglog
