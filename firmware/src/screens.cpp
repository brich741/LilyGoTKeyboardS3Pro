#include "screens.h"

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

}  // namespace screens
