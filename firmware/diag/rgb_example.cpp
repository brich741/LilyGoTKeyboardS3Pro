// Verbatim copy of LilyGo's examples/Basic/RGB/RGB.ino, for hardware diagnosis only
// (build with:  pio run -e rgbdemo -t upload).
#include <Arduino.h>
#include <TKeyboardS3Pro.h>

void setup(void)
{
    Serial.begin(115200);
    TKeyboardS3Pro.begin();

    uint8_t fw = TKeyboardS3Pro.firmwareVersion();
    Serial.printf("STM32 firmware version: 0x%02X\n", fw);
}

void loop(void)
{
    // Sweep the hue across all 14 LEDs (saturation 100 = fully saturated).
    for (uint16_t hue = 0; hue < 360; hue += 5) {
        TKeyboardS3Pro.setLeds(hue, 100 /* saturation */, 60 /* brightness */);
        delay(20);
    }
}
