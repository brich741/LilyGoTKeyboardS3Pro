// Replays the LED calls used by LilyGo's own examples, verbatim, one scene at a time, with the scene
// name shown on screen 1 (library: examples/Advanced/BLEKeyboard and Claude-Desktop-Buddy).
// Build: pio run -e ledexamples -t upload
#include <Arduino.h>
#include <TKeyboardS3Pro.h>

static void label(const char* a, const char* b = "") {
    Display& s = TKeyboardS3Pro.displayAt(0);
    s.fillScreen(TFT_BLACK);
    s.setTextColor(TFT_WHITE);
    s.setTextSize(2);
    s.setCursor(4, 20);
    s.print(a);
    s.setCursor(4, 70);
    s.print(b);
}

static void scene(const char* name, const char* detail, uint32_t ms, void (*body)(uint32_t t))
{
    label(name, detail);
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        body(millis() - t0);
        delay(50);
    }
}

void setup(void)
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();
    TKeyboardS3Pro.setBrightness(255);
    TKeyboardS3Pro.fillAllScreens(TFT_BLACK);
}

void loop(void)
{
    // --- BLEKeyboard.ino updateLeds(): "connected" = gentle green breathe
    scene("BLEKB", "green 120/70", 5000, [](uint32_t) {
        uint8_t brt = 10 + (uint8_t)(10.0f * sinf((float)millis() / 800.0f));
        TKeyboardS3Pro.setLeds(120, 70, brt);
    });
    // --- BLEKeyboard.ino updateLeds(): "advertising" = slow red fade
    scene("BLEKB", "red 0/80", 5000, [](uint32_t) {
        uint8_t brt = 5 + (uint8_t)(5.0f * sinf((float)millis() / 1200.0f));
        TKeyboardS3Pro.setLeds(0, 80, brt);
    });
    // --- Claude-Desktop-Buddy persona table (hue, sat, brightness) used with setLeds()
    struct P { const char* n; uint16_t h; uint8_t s; uint8_t b; } personas[] = {
        {"sleep",     240, 30, 5},  {"idle",   200, 50, 10}, {"busy",  120, 80, 20},
        {"attention",  45, 90, 30}, {"dizzy",  300, 80, 20}, {"heart",   0, 90, 25}};
    for (auto& p : personas) {
        label("BUDDY", p.n);
        uint32_t t0 = millis();
        while (millis() - t0 < 4000) { TKeyboardS3Pro.setLeds(p.h, p.s, p.b); delay(50); }
    }
    // --- Buddy "celebrate": hue = (millis()/100) % 360, sat 80, brightness 40
    scene("BUDDY", "celebrate", 6000, [](uint32_t) {
        TKeyboardS3Pro.setLeds((millis() / 100) % 360, 80, 40);
    });
}
