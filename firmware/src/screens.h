#pragma once
#include <Arduino.h>

#include <string>

// Drawing on the four key displays. Every function takes the hardware mutex itself.
namespace screens {

void begin();
void setBrightness(uint8_t percent);

// Show icon `iconId` (raw RGB565 file) on screen `index` 0..3. If the icon file is
// missing, a plain tile with `fallback` text is drawn instead.
void drawIcon(int index, const std::string& iconId, const char* fallback);

// Overlay a text banner over the bottom of screen `index`.
void banner(int index, const char* text);

// Info panel: coloured title (Font4) and up to 4 text strings, auto-wrapped to the 128 px width
// (switches to a smaller font if it does not fit). Use for SSID / password / address screens.
void panel(int index, uint16_t titleColor, const char* title, const char* t1 = nullptr,
           const char* t2 = nullptr, const char* t3 = nullptr, const char* t4 = nullptr);

// Full-screen text on one display.
void message(int index, const char* title, const char* line1, const char* line2 = nullptr);

}  // namespace screens
