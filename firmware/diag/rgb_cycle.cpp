// User-supplied test: red / green / blue, one second each, via the library's setLeds().
// The current colour name is shown on all four key screens so it can be matched to the strip.
// Build: pio run -e rgbcycle -t upload
#include <Arduino.h>
#include <TKeyboardS3Pro.h>

static void show(const char* name, uint16_t bg) {
  for (uint8_t i = 0; i < 4; i++) {
    Display& s = TKeyboardS3Pro.displayAt(i);
    s.fillScreen(bg);
    s.setTextColor(TFT_WHITE, bg);
    s.setTextSize(3);
    s.setCursor(8, 48);
    s.print(name);
  }
}

void setup() {
  Serial.begin(115200);
  TKeyboardS3Pro.begin();
  TKeyboardS3Pro.setBrightness(255);
}

void loop() {
  show("RED", TFT_RED);
  TKeyboardS3Pro.setLeds(0, 100, 30);   // red
  delay(1000);

  show("GREEN", TFT_DARKGREEN);
  TKeyboardS3Pro.setLeds(120, 100, 30); // green
  delay(1000);

  show("BLUE", TFT_BLUE);
  TKeyboardS3Pro.setLeds(240, 100, 30); // blue
  delay(1000);
}
