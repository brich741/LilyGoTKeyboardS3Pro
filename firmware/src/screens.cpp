#include "screens.h"

#include <string>
#include <vector>

#include <TKeyboardS3Pro.h>

#include "app.h"
#include "storage.h"

namespace screens {

namespace {
uint8_t* iconBuf = nullptr;   // 32 KB scratch, guarded by hwMtx
constexpr int kSize = 128;

void centered(Display& d, const char* text, int y, const lgfx::IFont* font, uint16_t fg) {
  d.setFont(font);
  d.setTextColor(fg);
  d.setTextDatum(lgfx::middle_center);
  d.drawString(text, kSize / 2, y);
}
}  // namespace

void begin() {
  iconBuf = (uint8_t*)malloc(storage::kIconBytes);
}

void setBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  app::Lock l(app::hwMtx);
  TKeyboardS3Pro.setBrightness((uint8_t)(percent * 255 / 100));
}

void drawIcon(int index, const std::string& iconId, const char* fallback) {
  if (index < 0 || index >= 4) return;
  app::Lock l(app::hwMtx);
  Display& d = TKeyboardS3Pro.displayAt((uint8_t)index);
  if (!iconId.empty() && iconBuf && storage::readIcon(iconId, iconBuf)) {
    d.pushImage(0, 0, kSize, kSize, (const lgfx::swap565_t*)iconBuf);
    return;
  }
  d.fillScreen(TFT_BLACK);
  d.drawRect(2, 2, kSize - 4, kSize - 4, 0x4208);
  centered(d, fallback ? fallback : "", kSize / 2 - 8, &lgfx::fonts::Font4, TFT_WHITE);
  if (!iconId.empty()) centered(d, iconId.c_str(), kSize / 2 + 20, &lgfx::fonts::Font2, 0x8410);
}

void banner(int index, const char* text) {
  if (index < 0 || index >= 4) return;
  app::Lock l(app::hwMtx);
  Display& d = TKeyboardS3Pro.displayAt((uint8_t)index);
  d.fillRect(0, kSize - 26, kSize, 26, TFT_BLACK);
  centered(d, text, kSize - 13, &lgfx::fonts::Font2, TFT_WHITE);
}

void message(int index, const char* title, const char* line1, const char* line2) {
  if (index < 0 || index >= 4) return;
  app::Lock l(app::hwMtx);
  Display& d = TKeyboardS3Pro.displayAt((uint8_t)index);
  d.fillScreen(TFT_BLACK);
  centered(d, title, 22, &lgfx::fonts::Font4, TFT_CYAN);
  centered(d, line1 ? line1 : "", 64, &lgfx::fonts::Font2, TFT_WHITE);
  if (line2) centered(d, line2, 92, &lgfx::fonts::Font2, TFT_WHITE);
}

void panel(int index, uint16_t titleColor, const char* title, const char* t1, const char* t2,
           const char* t3, const char* t4) {
  if (index < 0 || index >= 4) return;
  app::Lock l(app::hwMtx);
  Display& d = TKeyboardS3Pro.displayAt((uint8_t)index);
  d.fillScreen(TFT_BLACK);
  centered(d, title, 20, &lgfx::fonts::Font4, titleColor);

  const char* parts[4] = {t1, t2, t3, t4};
  const lgfx::IFont* fonts[2] = {&lgfx::fonts::Font2, &lgfx::fonts::Font0};
  const int steps[2] = {21, 11};
  const int maxLines[2] = {4, 8};
  for (int attempt = 0; attempt < 2; ++attempt) {
    d.setFont(fonts[attempt]);
    std::vector<std::string> lines;
    for (const char* p : parts) {
      if (!p || !*p) continue;
      std::string rest = p;
      while (!rest.empty()) {   // greedy wrap: longest prefix that fits in 120 px
        size_t n = rest.size();
        while (n > 1 && d.textWidth(rest.substr(0, n).c_str()) > 120) --n;
        lines.push_back(rest.substr(0, n));
        rest.erase(0, n);
      }
    }
    if ((int)lines.size() > maxLines[attempt] && attempt == 0) continue;   // try the smaller font
    int y = 52;
    for (size_t i = 0; i < lines.size() && (int)i < maxLines[attempt]; ++i, y += steps[attempt])
      centered(d, lines[i].c_str(), y, fonts[attempt], TFT_WHITE);
    break;
  }
}

}  // namespace screens
