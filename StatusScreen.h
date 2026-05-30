#pragma once
#include <M5GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "GameState.h"
#include "Galaxy.h"
#include "Hyperspace.h"
#include "Faction.h"
#include "Quest.h"
#include "Rank.h"
#include "MenuUI.h"

// Day 9: Commander Status card. Read-only summary of who you are and
// where you are. Rank text comes from Rank.h.

namespace StatusScreen {

// Kept for callers that still want a string-only API.
inline const char* rankFor(int kills) {
  return Rank::nameFor(Rank::forKills(kills));
}

inline void draw(M5Canvas& g, int currentIdx, const GameState& s) {
  MenuUI::clearBg(g);

  MenuUI::drawHeader(g, nullptr, nullptr, MenuUI::TitleColor,
                     MenuUI::CreditsColor, MenuUI::SepColor);
  MenuUI::printCenter(g, 3, "COMMANDER STATUS", MenuUI::TitleColor);

  char buf[28];

  // Identity rows.
  MenuUI::drawStatRow(g, 18, "NAME", s.commanderName, TFT_WHITE);

  {
    Rank::Id    cur  = Rank::forKills(s.kills);
    uint16_t    col  = Rank::colorFor(cur);
    const char* name = Rank::nameFor(cur);
    char rk[28];
    if ((int)cur < Rank::N - 1) {
      int need = Rank::Thresholds[(int)cur + 1] - s.kills;
      if (need < 0) need = 0;
      snprintf(rk, sizeof(rk), "%s K%d +%d", name, s.kills, need);
    } else {
      snprintf(rk, sizeof(rk), "%s K%d", name, s.kills);
    }
    MenuUI::drawStatRow(g, 30, "RANK", rk, col);
  }

  // Faction standing one-liner.
  {
    static const char letters[4] = { 'I', 'F', 'C', 'T' };
    char rep[28]; int p = 0;
    for (int i = 0; i < GameState::NumFactions; i++) {
      int st = (int)s.standing[i];
      p += snprintf(rep + p, sizeof(rep) - p, "%s%c%+d",
                    (i == 0) ? "" : " ", letters[i], st);
    }
    bool hostile = false;
    for (int i = 0; i < GameState::NumFactions; i++) {
      if (s.standing[i] <= Faction::HostileThreshold) { hostile = true; break; }
    }
    MenuUI::drawStatRow(g, 42, "REP", rep,
                        hostile ? TFT_RED : TFT_LIGHTGREY);
  }

  snprintf(buf, sizeof(buf), "%d.%d CR", s.credits / 10, s.credits % 10);
  MenuUI::drawStatRow(g, 54, "CREDITS", buf, MenuUI::CreditsColor);

  snprintf(buf, sizeof(buf), "%d/%d t", s.cargoTotal(), (int)s.cargoMax);
  MenuUI::drawStatRow(g, 66, "CARGO", buf);

  MenuUI::drawStatRow(g, 78, "LOCATION", Galaxy::systems[currentIdx].name,
                      TFT_LIGHTGREY);

  snprintf(buf, sizeof(buf), "%d%%", (int)(s.shield * 100));
  MenuUI::drawStatRow(g, 90, "SHIELD", buf);

  snprintf(buf, sizeof(buf), "%d%%", (int)(s.hull * 100));
  uint16_t hc = s.hull > 0.66f ? TFT_GREEN
              : s.hull > 0.33f ? TFT_YELLOW
              :                  TFT_RED;
  MenuUI::drawStatRow(g, 102, "HULL", buf, hc);

  // Equipment one-liner.
  {
    char eq[24]; int p = 0;
    if (s.missiles > 0)
      p += snprintf(eq + p, sizeof(eq) - p, "M%d ", (int)s.missiles);
    if (s.ecm)             p += snprintf(eq + p, sizeof(eq) - p, "ECM ");
    if (s.cargoMax >= s.CargoMaxLarge)
                           p += snprintf(eq + p, sizeof(eq) - p, "HOLD ");
    if (s.laserTier == 1)  p += snprintf(eq + p, sizeof(eq) - p, "BEAM ");
    if (s.laserTier >= 2)  p += snprintf(eq + p, sizeof(eq) - p, "MIL ");
    if (Quest::isActive())
      p += snprintf(eq + p, sizeof(eq) - p, "Q:%s",
                    Quest::typeShort(Quest::active.type));
    if (p == 0) strncpy(eq, "-", sizeof(eq));
    MenuUI::drawStatRow(g, 114, "GEAR", eq, TFT_LIGHTGREY);
  }

  MenuUI::drawFooter(g, "DEL=BACK");
}

} // namespace StatusScreen
