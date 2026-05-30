#pragma once
#include <M5GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "GameState.h"
#include "Market.h"
#include "Faction.h"
#include "Quest.h"
#include "SolarSystem.h"
#include "Audio.h"
#include "MenuUI.h"

// Quest board UI — rewritten R30.
//
// One quest can be active at a time. Each planet exposes a deterministic
// board of `Quest::BoardSize` offers. ENTER accepts (only if no active
// quest); '0' or BACKSPACE-on-board discards the current one (with a
// small standing penalty).

namespace QuestScreen {

inline int          sysIdx       = -1;
inline int          planetPOIidx = -1;
inline int          cursor       = 0;
inline Quest::Slot  board[Quest::BoardSize];
inline float        toast        = 0.0f;
inline char         toastMsg[28] = "";

inline void flashToast(const char* msg) {
  strncpy(toastMsg, msg, sizeof(toastMsg));
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toast = 1.4f;
}

inline void tick(float dt) { MenuUI::tickToast(toast, dt); }

inline void enter(int sys, int poi) {
  sysIdx       = sys;
  planetPOIidx = poi;
  cursor       = 0;
  toast        = 0.0f;
  Quest::buildBoard(sys, poi, board);
}

inline void moveUp()   { cursor = (cursor - 1 + Quest::BoardSize) % Quest::BoardSize; }
inline void moveDown() { cursor = (cursor + 1) % Quest::BoardSize; }

// True if the active quest was taken here. Used to draw the badge on
// whichever board row matches, and to toggle ENTER between "ALREADY
// HAVE QUEST" and "DISCARD".
inline bool activeFromHere() {
  return Quest::isActive() &&
         (int)Quest::active.fromSys == sysIdx &&
         (int)Quest::active.fromPOI == planetPOIidx;
}

inline int activeBoardRow() {
  if (!activeFromHere()) return -1;
  for (int i = 0; i < Quest::BoardSize; i++) {
    const Quest::Slot& a = Quest::active;
    const Quest::Slot& b = board[i];
    if (a.type == b.type && a.qty == b.qty &&
        a.toPOI == b.toPOI && a.commodity == b.commodity) {
      return i;
    }
  }
  return -1;
}

inline void tryEnter(GameState& g) {
  // With at most one active quest, ENTER toggles: discard if one is
  // already in play (regardless of which row is highlighted — there
  // may be no matching row if the board re-rolled), otherwise accept
  // the cursor row. Discard incurs a half-reward standing penalty.
  if (Quest::isActive()) {
    Quest::discard(g);
    flashToast("DISCARDED -REP");
    return;
  }
  if (!Quest::accept(g, board[cursor])) {
    Audio::deny();
    flashToast("CAN'T ACCEPT");
    return;
  }
  flashToast("QUEST ACCEPTED");
}

inline void drawRow(M5Canvas& g, const Quest::Slot& s, int y, bool sel,
                    bool isActiveRow, int rowH) {
  uint16_t bg = MenuUI::drawRowBg(g, y, rowH - 1, sel,
                                  MenuUI::RowSelBgCool);
  uint16_t fg = isActiveRow ? TFT_GREEN
                            : (sel ? TFT_YELLOW : TFT_LIGHTGREY);

  g.setTextSize(1);
  g.setTextColor(fg, bg);
  g.setCursor(2, y);
  g.print(isActiveRow ? "[A] " : "[ ] ");

  char buf[40];
  Quest::formatObjective(s, buf, sizeof(buf));
  g.print(buf);

  // Second line — reward, difficulty, faction.
  int cr = s.rewardTenthsCR;
  g.setTextColor(sel ? TFT_WHITE : TFT_DARKCYAN, bg);
  g.setCursor(20, y + 8);
  snprintf(buf, sizeof(buf), "%d.%dCR  %s  %s+%d",
           cr / 10, cr % 10,
           Quest::difficultyLabel(s.difficulty),
           Faction::shortName((Faction::Id)s.faction),
           (int)s.factionDelta);
  g.print(buf);
}

inline void draw(M5Canvas& g, const GameState& gs) {
  (void)gs;
  MenuUI::clearBg(g);

  // Header: planet name + active quest indicator.
  char planetName[24];
  SolarSystem::Layout L;
  SolarSystem::layoutFor(sysIdx, L);
  if (planetPOIidx >= 0 && planetPOIidx < L.numPOIs) {
    SolarSystem::displayName(sysIdx, L.poi[planetPOIidx],
                             planetName, sizeof(planetName));
  } else {
    snprintf(planetName, sizeof(planetName), "%s",
             Galaxy::systems[sysIdx].name);
  }

  char rightInfo[20];
  if (Quest::isActive()) {
    snprintf(rightInfo, sizeof(rightInfo), "%s ACTIVE",
             Quest::typeShort(Quest::active.type));
  } else {
    snprintf(rightInfo, sizeof(rightInfo), "%s",
             Quest::difficultyLabel(Quest::systemDifficulty(sysIdx)));
  }
  MenuUI::drawHeader2(g, "QUESTS  ", planetName,
                      rightInfo,
                      /*leftA*/ MenuUI::TitleColor,
                      /*leftB*/ TFT_WHITE,
                      /*right*/ Quest::isActive() ? TFT_GREEN
                                                  : MenuUI::SubColor,
                      /*sep  */ MenuUI::SepColorWarm);

  const int firstY = MenuUI::ContentY0;
  const int rowH   = 18;
  int activeRow    = activeBoardRow();

  for (int i = 0; i < Quest::BoardSize; i++) {
    int y = firstY + i * rowH;
    drawRow(g, board[i], y, cursor == i, i == activeRow, rowH);
  }

  // Footer hint changes with state.
  const char* hint = Quest::isActive()
                       ? "ENTER=DISCARD  DEL=BACK"
                       : "ENTER=TAKE  DEL=BACK";
  MenuUI::drawFooter(g, hint);

  MenuUI::drawToast(g, toastMsg, toast, 0.6f, 220, 220, 55);
}

} // namespace QuestScreen
