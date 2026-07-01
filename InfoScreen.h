#pragma once
#include <M5GFX.h>
#include "Config.h"
#include "MenuUI.h"

namespace InfoScreen {

inline void draw(M5Canvas& g) {
  MenuUI::clearBg(g);

  MenuUI::printCenter(g, 4, "CONTROLS", MenuUI::TitleColor);
  g.drawFastHLine(60, 14, 120, MenuUI::SepColor);

  struct Row { const char* k; const char* v; };
  static const Row rows[] = {
    {"UP / DN",  "Pitch up / down"},
    {"LT / RT",  "Roll"},
    {"L / '",    "Yaw left / right"},
    {"E / S",    "Accel / brake"},
    {"W",        "Fire laser"},
    {"R / A",    "Lock / fire missile"},
    {"Q",        "ECM blast"},
    {"TAB",      "Cycle target"},
    {"H",        "Dock with ship"},
    {"M",        "System map"},
    {"ESC",      "Back / title"},
  };
  constexpr int N = sizeof(rows) / sizeof(rows[0]);

  // 11 rows at 9 px pitch end at y=116, clear of the return hint at
  // ScreenH-11.
  int y = 18;
  for (int i = 0; i < N; i++) {
    g.setTextSize(1);
    g.setTextColor(MenuUI::LabelColor, TFT_BLACK);
    g.setCursor(14, y);
    g.print(rows[i].k);
    g.setTextColor(MenuUI::ValueColor, TFT_BLACK);
    g.setCursor(78, y);
    g.print(rows[i].v);
    y += 9;
  }

  MenuUI::printCenter(g, Config::ScreenH - 11,
                      "PRESS ANY KEY TO RETURN", MenuUI::HintColor);
}

} // namespace InfoScreen
