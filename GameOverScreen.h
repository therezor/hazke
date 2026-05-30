#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdio.h>
#include "Config.h"
#include "GameState.h"
#include "Galaxy.h"
#include "Rank.h"
#include "MenuUI.h"

// Endgame card. Shown after the ship-destroyed animation finishes.
// Displays the final stats and prompts the player to start over —
// outer loop catches ENTER and runs newCommander().

namespace GameOverScreen {

inline float phase = 0.0f;

inline void enter() { phase = 0.0f; }
inline void tick(float dt) { phase += dt; }

inline void draw(M5Canvas& g, int currentIdx, const GameState& s) {
  MenuUI::clearBg(g);

  // Pulsing title.
  float t = sinf(phase * 4.0f) * 0.5f + 0.5f;
  uint8_t lum = (uint8_t)(180 + 75 * t);
  uint16_t titleCol = g.color565(lum, 0, 0);
  g.setTextSize(2);
  const char* title = "GAME OVER";
  int tw = (int)strlen(title) * 12;
  g.setTextColor(titleCol, TFT_BLACK);
  g.setCursor((Config::ScreenW - tw) / 2, 14);
  g.print(title);

  g.drawFastHLine(20, 36, Config::ScreenW - 40, MenuUI::SepColor);

  // Stats.
  char buf[28];
  Rank::Id rank = Rank::forKills(s.kills);
  g.setTextSize(1);

  MenuUI::drawStatRow(g, 44, "COMMANDER", s.commanderName, TFT_WHITE);

  snprintf(buf, sizeof(buf), "%s K%d", Rank::nameFor(rank), s.kills);
  MenuUI::drawStatRow(g, 56, "RANK", buf, Rank::colorFor(rank));

  snprintf(buf, sizeof(buf), "%d.%d CR", s.credits / 10, s.credits % 10);
  MenuUI::drawStatRow(g, 68, "CREDITS", buf, MenuUI::CreditsColor);

  MenuUI::drawStatRow(g, 80, "LAST SYS",
                      Galaxy::systems[currentIdx].name, TFT_LIGHTGREY);

  // Restart hint, blinking.
  bool show = ((int)(phase * 2.0f) & 1) == 0;
  if (show) {
    const char* prompt = "ENTER  RESTART";
    int pw = (int)strlen(prompt) * 6;
    g.setTextColor(TFT_CYAN, TFT_BLACK);
    g.setCursor((Config::ScreenW - pw) / 2, Config::ScreenH - 18);
    g.print(prompt);
  }
}

} // namespace GameOverScreen
