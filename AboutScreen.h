#pragma once
#include <M5GFX.h>
#include <string.h>
#include "Config.h"
#include "MenuUI.h"

namespace AboutScreen {

inline void draw(M5Canvas& g) {
  MenuUI::clearBg(g);

  MenuUI::printCenter(g, 6, "ABOUT", MenuUI::TitleColor);
  g.drawFastHLine(80, 16, 80, MenuUI::SepColor);

  struct Line { uint16_t color; const char* text; };
  static const Line lines[] = {
    {TFT_WHITE,     "HAZKE  v1.0"},
    {TFT_LIGHTGREY, "An open-universe space sim"},
    {TFT_LIGHTGREY, "for the M5Cardputer."},
    {TFT_DARKGREY,  ""},
    {TFT_CYAN,      "Trade. Hunt. Survive."},
  };
  constexpr int N = sizeof(lines) / sizeof(lines[0]);

  int y = 26;
  for (int i = 0; i < N; i++) {
    MenuUI::printCenter(g, y, lines[i].text, lines[i].color);
    y += 11;
  }

  MenuUI::printCenter(g, Config::ScreenH - 11,
                      "PRESS ANY KEY TO RETURN", MenuUI::HintColor);
}

} // namespace AboutScreen
