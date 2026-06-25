#pragma once
#include <M5GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "MenuUI.h"
#include "Quest.h"
#include "Galaxy.h"

// In-game pause menu. Triggered by ESC from SystemFlight; covers the
// frame with a dimmed overlay so the player still sees a hint of the
// cockpit underneath. Selecting an item drives the outer state machine
// in hazke.ino.

namespace PauseMenu {

enum : int {
  ItemResume = 0,
  ItemMap,
  ItemSound,
  ItemControls,
  ItemExit,
};

inline const char* items[] = {
  "RESUME",
  "MAP",
  "SOUND",        // label resolved at draw time from Audio::muted
  "CONTROLS",
  "EXIT TO MENU",
};
inline constexpr int N = sizeof(items) / sizeof(items[0]);

inline int selected = 0;

inline void open()        { selected = 0; }
inline void moveUp()      { selected = (selected - 1 + N) % N; }
inline void moveDown()    { selected = (selected + 1) % N; }
inline void tick(float)   {}

inline void draw(M5Canvas& g, float phase) {
  // Dim the world behind the menu (assumes the caller already painted
  // the cockpit/frame underneath). A 50% black wash is cheap and reads
  // as "the game is paused" without redrawing anything.
  for (int y = 0; y < Config::ScreenH; y += 2) {
    g.drawFastHLine(0, y, Config::ScreenW, TFT_BLACK);
  }

  // Title strip.
  MenuUI::drawHeader(g, nullptr, nullptr, MenuUI::TitleColor,
                     MenuUI::CreditsColor, MenuUI::SepColorWarm);
  MenuUI::printCenter(g, MenuUI::HeaderY + 1, "PAUSED",
                      MenuUI::TitleColor);

  // Active quest banner — a single line showing the CURRENT step the
  // player should do next, not the full contract. The board screen is
  // where the contract wording lives; here we only need the verb.
  {
    char step[40];
    uint16_t col;
    if (Quest::isActive()) {
      Quest::formatStatus(step, sizeof(step));
      col = (Quest::status == Quest::Status::ReadyToTurnIn)
              ? TFT_GREEN : TFT_CYAN;
    } else {
      snprintf(step, sizeof(step), "NO ACTIVE QUEST");
      col = TFT_DARKGREY;
    }
    MenuUI::printCenter(g, 22, step, col);
  }

  // Centered list. Push it down so the quest banner has room above it.
  // itemH=16 matches the textSize-2 default glyph height in
  // drawBigMenuItem; tighter pitches would overlap.
  const int itemH  = 16;
  const int firstY = 36;
  for (int i = 0; i < N; i++) {
    const char* label = items[i];
    if (i == ItemSound) label = Audio::muted ? "SOUND: OFF" : "SOUND: ON";
    MenuUI::drawBigMenuItem(g, firstY + i * itemH, label,
                            i == selected, phase);
  }

  MenuUI::drawFooter(g, "ARROWS  ENTER  ESC=RESUME");
}

} // namespace PauseMenu
