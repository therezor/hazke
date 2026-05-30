#pragma once
#include <M5GFX.h>
#include <string.h>
#include "Config.h"
#include "MenuUI.h"

namespace TitleScreen {

struct MenuItem { const char* label; };

inline const MenuItem items[] = {
  {"NEW GAME"},
  {"CONTROLS"},
  {"ABOUT"},
};
inline constexpr int N = sizeof(items) / sizeof(items[0]);

inline void draw(M5Canvas& g, float phaseSec, int selected) {
  MenuUI::clearBg(g);

  // Top/bottom rules for a framed feel.
  g.drawFastHLine(0, 2, Config::ScreenW, TFT_DARKCYAN);
  g.drawFastHLine(0, Config::ScreenH - 3, Config::ScreenW, TFT_DARKCYAN);

  // Title block.
  g.setTextSize(2);
  g.setTextColor(TFT_WHITE, TFT_BLACK);
  g.setCursor(MenuUI::centerX2("HAZKE"), 10);
  g.print("HAZKE");
  MenuUI::printCenter(g, 30, "- CARDPUTER EDITION -", TFT_CYAN);

  // Menu items — vertically centered in the lower half.
  const int firstY = 60;
  const int itemH  = 14;
  for (int i = 0; i < N; i++) {
    MenuUI::drawBigMenuItem(g, firstY + i * itemH, items[i].label,
                            i == selected, phaseSec, /*destructive=*/false,
                            MenuUI::DisabledColor, /*textSize=*/1);
  }

  MenuUI::printCenter(g, Config::ScreenH - 13, "ARROWS  ENTER",
                      MenuUI::HintColor);

  // Version badge in the bottom-left corner.
  g.setTextSize(1);
  g.setTextColor(MenuUI::HintColor, TFT_BLACK);
  g.setCursor(4, Config::ScreenH - 13);
  g.print("v0.4");
}

} // namespace TitleScreen
