#include <unity.h>

#include <string>
#include <vector>

#include "config.h"
#include "gesture.h"
#include "keymap.h"
#include "leds.h"
#include "runner.h"
#include "visual.h"

using namespace core;

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------ keymap

void test_keymap_basic() {
  TEST_ASSERT_EQUAL_UINT8(0x04, keyUsage("a"));
  TEST_ASSERT_EQUAL_UINT8(0x1D, keyUsage("z"));
  TEST_ASSERT_EQUAL_UINT8(0x1E, keyUsage("1"));
  TEST_ASSERT_EQUAL_UINT8(0x27, keyUsage("0"));
  TEST_ASSERT_EQUAL_UINT8(0x3A, keyUsage("f1"));
  TEST_ASSERT_EQUAL_UINT8(0x45, keyUsage("f12"));
  TEST_ASSERT_EQUAL_UINT8(0x68, keyUsage("f13"));
  TEST_ASSERT_EQUAL_UINT8(0x73, keyUsage("f24"));
  TEST_ASSERT_EQUAL_UINT8(0, keyUsage("f25"));
  TEST_ASSERT_EQUAL_UINT8(0x28, keyUsage("enter"));
  TEST_ASSERT_EQUAL_UINT8(0, keyUsage("bogus"));
  TEST_ASSERT_EQUAL_UINT8(0xE3, modUsage(MOD_GUI));
  TEST_ASSERT_EQUAL_UINT8(MOD_ALT, modFlag("alt"));
  TEST_ASSERT_EQUAL_UINT16(0xE2, mediaUsage("mute"));
}

void test_keymap_roundtrip() {
  for (int u = 0x04; u <= 0x73; ++u) {
    const char* n = keyName((uint8_t)u);
    if (!n) continue;
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(u, keyUsage(n), n);
  }
}

// ------------------------------------------------------------ gestures

struct Sim {
  GestureEngine eng;
  uint32_t t = 1000;
  std::vector<GEvent> events;
  explicit Sim(bool dbl, bool hold) {
    GestureTiming g;
    g.hasDouble = dbl;
    g.hasHold = hold;
    eng.configure(g);
  }
  void run(bool pressed, uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i, ++t) {
      GEvent out[GestureEngine::kMaxEvents];
      int n = eng.update(pressed, t, out);
      for (int k = 0; k < n; ++k) events.push_back(out[k]);
    }
  }
};

void test_single_no_double_fires_on_release() {
  Sim s(false, false);
  s.run(true, 100);
  TEST_ASSERT_EQUAL(0, (int)s.events.size());
  s.run(false, 30);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[0] == GEvent::Single);
}

void test_single_waits_for_double_window() {
  Sim s(true, false);
  s.run(true, 80);
  s.run(false, 200);
  TEST_ASSERT_EQUAL(0, (int)s.events.size());
  s.run(false, 100);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[0] == GEvent::Single);
}

void test_double_press() {
  Sim s(true, false);
  s.run(true, 80);
  s.run(false, 100);
  s.run(true, 80);
  s.run(false, 30);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[0] == GEvent::Double);
  s.run(false, 500);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());  // no trailing Single
}

void test_hold_start_end() {
  Sim s(false, true);
  s.run(true, 300);
  TEST_ASSERT_EQUAL(0, (int)s.events.size());
  s.run(true, 200);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[0] == GEvent::HoldStart);
  s.run(true, 1000);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
  s.run(false, 30);
  TEST_ASSERT_EQUAL(2, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[1] == GEvent::HoldEnd);
}

void test_long_press_without_hold_is_single() {
  Sim s(false, false);
  s.run(true, 2000);
  s.run(false, 30);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[0] == GEvent::Single);
}

void test_click_then_hold_emits_single_then_hold() {
  Sim s(true, true);
  s.run(true, 80);
  s.run(false, 100);
  s.run(true, 600);
  TEST_ASSERT_EQUAL(2, (int)s.events.size());
  TEST_ASSERT_TRUE(s.events[0] == GEvent::Single);
  TEST_ASSERT_TRUE(s.events[1] == GEvent::HoldStart);
  s.run(false, 30);
  TEST_ASSERT_TRUE(s.events.back() == GEvent::HoldEnd);
}

void test_debounce_ignores_glitch() {
  Sim s(false, false);
  s.run(true, 60);
  s.run(false, 3);   // 3 ms bounce
  s.run(true, 60);
  s.run(false, 30);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());
}

void test_reset_during_hold_reports_and_swallows() {
  Sim s(false, true);
  s.run(true, 500);
  TEST_ASSERT_TRUE(s.eng.reset());
  s.run(true, 500);   // still pressed: ignored
  s.run(false, 30);
  TEST_ASSERT_EQUAL(1, (int)s.events.size());  // only the original HoldStart
}

// ------------------------------------------------------------ runner

struct Rec : HidSink {
  std::vector<std::string> log;
  void keyDown(uint8_t u) override { log.push_back("d" + std::to_string(u)); }
  void keyUp(uint8_t u) override { log.push_back("u" + std::to_string(u)); }
  void typeChar(char c) override { log.push_back(std::string("t") + c); }
  void mediaPress(uint16_t u) override { log.push_back("m" + std::to_string(u)); }
  void mediaRelease() override { log.push_back("mr"); }
  void sleepMs(uint32_t) override {}
};

static Step keyStep(Step::Mode m, uint8_t mods, const char* key) {
  Step s;
  s.type = Step::Key;
  s.mode = m;
  s.mods = mods;
  s.key = keyUsage(key);
  return s;
}

void test_runner_tap_order() {
  Rec r;
  StepRunner run(r);
  run.run({keyStep(Step::Tap, MOD_CTRL | MOD_SHIFT, "p")});
  std::vector<std::string> want = {"d224", "d225", "d19", "u19", "u225", "u224"};
  TEST_ASSERT_TRUE(r.log == want);
  TEST_ASSERT_EQUAL(0, run.heldCount());
}

void test_runner_down_up_and_release_all() {
  Rec r;
  StepRunner run(r);
  run.run({keyStep(Step::Down, MOD_ALT, "k")});
  TEST_ASSERT_EQUAL(2, run.heldCount());
  run.releaseAll();
  TEST_ASSERT_EQUAL(0, run.heldCount());
  // Both released.
  int ups = 0;
  for (auto& l : r.log) ups += l[0] == 'u';
  TEST_ASSERT_EQUAL(2, ups);
}

void test_runner_shared_modifier_refcount() {
  Rec r;
  StepRunner run(r);
  run.run({keyStep(Step::Down, MOD_CTRL, "a"), keyStep(Step::Down, MOD_CTRL, "b")});
  r.log.clear();
  run.run({keyStep(Step::Up, MOD_CTRL, "a")});
  // ctrl must still be held for "b": only 'a' is released.
  TEST_ASSERT_EQUAL(1, (int)r.log.size());
  TEST_ASSERT_EQUAL_STRING("u4", r.log[0].c_str());
}

struct Abortable : Rec {
  int typed = 0;
  bool stop = false;
  void typeChar(char c) override {
    Rec::typeChar(c);
    if (++typed == 2) stop = true;
  }
  bool shouldAbort() override { return stop; }
};

void test_runner_abort_stops_text_and_remaining_steps() {
  Abortable r;
  StepRunner run(r);
  Step t;
  t.type = Step::Text;
  t.text = "abcdef";
  Step tap = keyStep(Step::Tap, 0, "a");
  run.run({t, tap});
  TEST_ASSERT_EQUAL(2, r.typed);              // text stopped after 2 characters
  TEST_ASSERT_EQUAL(2, (int)r.log.size());    // and the following key step never ran
  TEST_ASSERT_EQUAL(0, run.heldCount());
}

void test_runner_text_and_media() {
  Rec r;
  StepRunner run(r);
  Step t;
  t.type = Step::Text;
  t.text = "ab";
  Step m;
  m.type = Step::Media;
  m.media = mediaUsage("mute");
  run.run({t, m});
  std::vector<std::string> want = {"ta", "tb", "m226", "mr"};
  TEST_ASSERT_TRUE(r.log == want);
}

// ------------------------------------------------------------ config

void test_default_config_roundtrip() {
  Config a = defaultConfig();
  JsonDocument doc;
  writeConfig(a, doc);
  std::string json;
  serializeJson(doc, json);

  JsonDocument doc2;
  TEST_ASSERT_TRUE(deserializeJson(doc2, json) == DeserializationError::Ok);
  Config b;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(parseConfig(doc2.as<JsonVariantConst>(), b, err), err.c_str());

  JsonDocument doc3;
  writeConfig(b, doc3);
  std::string json2;
  serializeJson(doc3, json2);
  TEST_ASSERT_EQUAL_STRING(json.c_str(), json2.c_str());
  TEST_ASSERT_EQUAL(2, (int)b.pages.size());
  TEST_ASSERT_TRUE(b.pages[0].keys[0].hold.present);
  TEST_ASSERT_EQUAL_STRING("mic-green", b.pages[0].keys[0].hold.vis.icon.c_str());
}

static bool parse(const char* json, Config& c, std::string& err) {
  JsonDocument d;
  if (deserializeJson(d, json) != DeserializationError::Ok) {
    err = "json";
    return false;
  }
  return parseConfig(d.as<JsonVariantConst>(), c, err);
}

void test_config_rejects_bad_input() {
  Config c;
  std::string err;
  TEST_ASSERT_FALSE(parse("[]", c, err));
  TEST_ASSERT_FALSE(parse("{\"pages\":[]}", c, err));
  TEST_ASSERT_FALSE(parse("{\"version\":99,\"pages\":[{}]}", c, err));
  TEST_ASSERT_FALSE(parse(
      "{\"pages\":[{\"keys\":[{\"single\":{\"steps\":[{\"t\":\"key\",\"key\":\"nope\"}]}}]}]}", c, err));
  TEST_ASSERT_FALSE(parse("{\"settings\":{\"apPass\":\"short\"},\"pages\":[{}]}", c, err));
}

void test_config_clamps_and_ignores_unknown() {
  Config c;
  std::string err;
  TEST_ASSERT_TRUE(parse(
      "{\"surprise\":1,\"settings\":{\"holdMs\":99999,\"brightness\":0},\"pages\":[{\"name\":\"x\"}]}",
      c, err));
  TEST_ASSERT_EQUAL(3000, c.settings.holdMs);
  TEST_ASSERT_EQUAL(5, c.settings.brightness);
}

void test_wifi_enabled_setting_roundtrips() {
  Config c;
  std::string err;
  TEST_ASSERT_TRUE(defaultConfig().settings.wifiEnabled);   // on by default
  TEST_ASSERT_TRUE(parse("{\"settings\":{\"wifiEnabled\":false},\"pages\":[{}]}", c, err));
  TEST_ASSERT_FALSE(c.settings.wifiEnabled);
  JsonDocument doc;
  writeConfig(c, doc);
  TEST_ASSERT_FALSE(doc["settings"]["wifiEnabled"].as<bool>());
  TEST_ASSERT_TRUE(parse("{\"pages\":[{}]}", c, err));    // absent -> default on
  TEST_ASSERT_TRUE(c.settings.wifiEnabled);
}

void test_encoder_hold_is_dropped() {
  Config c;
  std::string err;
  TEST_ASSERT_TRUE(parse(
      "{\"pages\":[{\"encoder\":{\"hold\":{\"start\":[{\"t\":\"delay\",\"ms\":1}]}}}]}", c, err));
  TEST_ASSERT_FALSE(c.pages[0].encoder.hold.present);
}

void test_color_and_icon_id() {
  uint32_t c = 0;
  TEST_ASSERT_TRUE(parseColor("#00ff7A", c));
  TEST_ASSERT_EQUAL_UINT32(0x00FF7A, c);
  TEST_ASSERT_FALSE(parseColor("00ff7a", c));
  TEST_ASSERT_FALSE(parseColor("#00ff7", c));
  TEST_ASSERT_TRUE(validIconId("mic-red_2"));
  TEST_ASSERT_FALSE(validIconId("Mic"));
  TEST_ASSERT_FALSE(validIconId("../x"));
  TEST_ASSERT_FALSE(validIconId(""));
}

// ------------------------------------------------------------ visuals

void test_visual_hold_and_after() {
  Config c = defaultConfig();
  KeyCfg k = c.pages[0].keys[0];
  k.hold.hasAfter = true;
  k.hold.after = Visual();
  k.hold.after.icon = "mic-red";
  k.hold.after.hasColor = true;
  k.hold.after.color = 0xFF0000;
  k.hold.afterMs = 500;

  KeyVisual v;
  v.reset(k);
  TEST_ASSERT_EQUAL_STRING("mic-red", v.look().icon.c_str());
  v.onHoldStart(k);
  TEST_ASSERT_EQUAL_STRING("mic-green", v.look().icon.c_str());
  TEST_ASSERT_FALSE(v.tick(k, 100000));  // holding never times out
  v.onHoldEnd(k, 1000, 1500);
  TEST_ASSERT_EQUAL_STRING("mic-red", v.look().icon.c_str());
  TEST_ASSERT_FALSE(v.tick(k, 1400));
  TEST_ASSERT_FALSE(v.tick(k, 1500));  // reverted to idle (same look) -> no change reported
}

void test_visual_flash_reverts_and_inherits() {
  Config c = defaultConfig();
  KeyCfg k = c.pages[0].keys[1];  // /clear: override color only
  KeyVisual v;
  v.reset(k);
  bool idleHasColor = v.look().hasColor;
  v.onSingle(k, 1000, 1500);
  TEST_ASSERT_EQUAL_STRING("clear", v.look().icon.c_str());  // icon inherited
  TEST_ASSERT_TRUE(v.look().hasColor);
  TEST_ASSERT_EQUAL_UINT32(0xFFAA00, v.look().color);
  TEST_ASSERT_FALSE(v.tick(k, 2499));
  TEST_ASSERT_TRUE(v.tick(k, 2500));
  TEST_ASSERT_EQUAL(idleHasColor, v.look().hasColor);
}

void test_visual_sticky_after() {
  KeyCfg k;
  k.idle.icon = "a";
  k.hold.present = true;
  k.hold.hasAfter = true;
  k.hold.after.icon = "b";
  k.hold.afterMs = kStickyMs;
  KeyVisual v;
  v.reset(k);
  v.onHoldEnd(k, 0, 1500);
  TEST_ASSERT_FALSE(v.tick(k, 1000000));
  TEST_ASSERT_EQUAL_STRING("b", v.look().icon.c_str());
}

// ------------------------------------------------------------ leds

void test_led_zone_overlay() {
  LedCfg cfg;
  cfg.effect = Effect::Off;
  uint16_t zones[kKeyCount] = {0x3, 0x18, 0xC0, 0x600};
  KeyLook looks[kKeyCount];
  looks[1].hasColor = true;
  looks[1].color = 0xFF0000;
  Rgb out[kLedCount];
  composeLeds(cfg, zones, looks, 0, out);
  for (int i = 0; i < kLedCount; ++i) {
    bool inZone = (zones[1] >> i) & 1;
    TEST_ASSERT_EQUAL_UINT8(inZone ? 255 : 0, out[i].r);
  }
}

void test_plan_paint_groups_and_diffs() {
  Rgb a[kLedCount], b[kLedCount];
  for (int i = 0; i < kLedCount; ++i) a[i] = b[i] = rgbFromHex(0x0000FF);
  PaintOp ops[kLedCount];
  TEST_ASSERT_EQUAL(1, planPaint(nullptr, a, 100, ops));
  TEST_ASSERT_EQUAL_HEX16(0x3FFF, ops[0].mask);
  TEST_ASSERT_EQUAL(0, planPaint(a, b, 100, ops));
  b[2] = rgbFromHex(0xFF0000);
  b[5] = rgbFromHex(0xFF0000);
  TEST_ASSERT_EQUAL(1, planPaint(a, b, 100, ops));
  TEST_ASSERT_EQUAL_HEX16((1 << 2) | (1 << 5), ops[0].mask);
  TEST_ASSERT_EQUAL_UINT16(0, ops[0].color.h);
}

void test_hue_is_dropped_but_saturation_and_value_kept() {
  Rgb in[kLedCount];
  in[0] = rgbFromHex(0x00FF00);   // green
  in[1] = rgbFromHex(0x0000FF);   // blue
  in[2] = rgbFromHex(0xFFFFFF);   // white
  for (int i = 3; i < kLedCount; ++i) in[i] = Rgb();
  PaintOp ops[kLedCount];
  int n = planPaint(nullptr, in, 100, ops);
  // green + blue share one op (hue ignored), white is its own op, the rest is off.
  bool sawColor = false, sawWhite = false;
  for (int i = 0; i < n; ++i) {
    TEST_ASSERT_EQUAL_UINT16(0, ops[i].color.h);
    if (ops[i].color.v && ops[i].color.s == 100) { sawColor = true; TEST_ASSERT_EQUAL_HEX16(0x3, ops[i].mask); }
    if (ops[i].color.v && ops[i].color.s == 0) { sawWhite = true; TEST_ASSERT_EQUAL_HEX16(0x4, ops[i].mask); }
  }
  TEST_ASSERT_TRUE(sawColor);
  TEST_ASSERT_TRUE(sawWhite);
}

void test_hue_mode_keeps_hue_groups() {
  Rgb in[kLedCount];
  in[0] = rgbFromHex(0xFF0000);
  in[1] = rgbFromHex(0x00FF00);
  in[2] = rgbFromHex(0x0000FF);
  for (int i = 3; i < kLedCount; ++i) in[i] = Rgb();
  PaintOp ops[kLedCount];
  int n = planPaint(nullptr, in, 100, ops, true);
  bool r = false, g = false, b = false;
  for (int i = 0; i < n; ++i) {
    if (ops[i].color.v == 0) continue;
    if (ops[i].color.h == 0) r = true;
    if (ops[i].color.h == 120) g = true;
    if (ops[i].color.h == 240) b = true;
  }
  TEST_ASSERT_TRUE(r && g && b);
}

void test_effects_are_bounded_and_animate() {
  const Effect all[] = {Effect::Solid, Effect::Breathe, Effect::Rainbow, Effect::Chase,
                        Effect::Sparkle};
  for (Effect e : all) {
    LedCfg cfg;
    cfg.effect = e;
    cfg.color = 0x00FF00;
    cfg.speed = 80;
    bool changed = false;
    Rgb first[kLedCount], cur[kLedCount];
    renderEffect(cfg, 0, first);
    for (uint32_t t = 37; t < 5000; t += 37) {
      renderEffect(cfg, t, cur);
      for (int i = 0; i < kLedCount; ++i)
        if (cur[i] != first[i]) changed = true;
    }
    if (e != Effect::Solid) TEST_ASSERT_TRUE_MESSAGE(changed, effectName(e));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_keymap_basic);
  RUN_TEST(test_keymap_roundtrip);
  RUN_TEST(test_single_no_double_fires_on_release);
  RUN_TEST(test_single_waits_for_double_window);
  RUN_TEST(test_double_press);
  RUN_TEST(test_hold_start_end);
  RUN_TEST(test_long_press_without_hold_is_single);
  RUN_TEST(test_click_then_hold_emits_single_then_hold);
  RUN_TEST(test_debounce_ignores_glitch);
  RUN_TEST(test_reset_during_hold_reports_and_swallows);
  RUN_TEST(test_runner_tap_order);
  RUN_TEST(test_runner_down_up_and_release_all);
  RUN_TEST(test_runner_shared_modifier_refcount);
  RUN_TEST(test_runner_text_and_media);
  RUN_TEST(test_runner_abort_stops_text_and_remaining_steps);
  RUN_TEST(test_default_config_roundtrip);
  RUN_TEST(test_config_rejects_bad_input);
  RUN_TEST(test_config_clamps_and_ignores_unknown);
  RUN_TEST(test_wifi_enabled_setting_roundtrips);
  RUN_TEST(test_encoder_hold_is_dropped);
  RUN_TEST(test_color_and_icon_id);
  RUN_TEST(test_visual_hold_and_after);
  RUN_TEST(test_visual_flash_reverts_and_inherits);
  RUN_TEST(test_visual_sticky_after);
  RUN_TEST(test_led_zone_overlay);
  RUN_TEST(test_plan_paint_groups_and_diffs);
  RUN_TEST(test_hue_is_dropped_but_saturation_and_value_kept);
  RUN_TEST(test_hue_mode_keeps_hue_groups);
  RUN_TEST(test_effects_are_bounded_and_animate);
  return UNITY_END();
}
