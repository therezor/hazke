#pragma once
#include <M5GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "Market.h"
#include "GameState.h"
#include "Faction.h"
#include "MenuUI.h"
#include "Quest.h"

// Day 4: commodity market UI.

namespace MarketScreen {

constexpr int ListY0      = 22;
constexpr int RowH        = 8;
constexpr int VisibleRows = 12;
constexpr int ColName     = 4;
constexpr int ColPrice    = 70;
constexpr int ColQty      = 122;
constexpr int ColHeld     = 168;

inline int cursor = 0;
inline int scroll = 0;

inline uint8_t localQty[Market::N];
inline int     localSys = -1;
inline int     localPOI = -1;

// Active fetch-contract stock rule: the contracted commodity is never
// sold at the planet that posted the quest, and every OTHER planet in
// the system stocks at least the contracted amount so the run is always
// completable. The zero-at-home half re-applies on every visit (the
// quest may have been accepted after this planet's stock was cached);
// the availability boost only runs on a fresh cache so re-entering the
// market can't restock what the player already bought.
inline void applyQuestStock(int sysIdx, int planetPOI, bool fresh) {
  if (!Quest::isActive())                         return;
  if (Quest::status != Quest::Status::InProgress) return;
  const Quest::Slot& q = Quest::active;
  if ((int)q.fromSys != sysIdx)                   return;
  if (q.commodity >= (uint8_t)Market::N)          return;
  if ((int)q.fromPOI == planetPOI) {
    localQty[q.commodity] = 0;
  } else if (fresh && localQty[q.commodity] < q.qty) {
    localQty[q.commodity] = q.qty;
  }
}

inline void enter(int sysIdx, int planetPOI) {
  bool fresh = (sysIdx != localSys) || (planetPOI != localPOI);
  if (fresh) {
    for (int i = 0; i < (int)Market::N; i++) {
      localQty[i] = Market::qtyAtPlanet(sysIdx, planetPOI, i);
    }
    localSys = sysIdx;
    localPOI = planetPOI;
  }
  applyQuestStock(sysIdx, planetPOI, fresh);
}

inline void moveCursor(int delta) {
  cursor += delta;
  MenuUI::clampScroll(cursor, scroll, VisibleRows, (int)Market::N);
}

inline void drawHeader(M5Canvas& g, int sysIdx, const GameState& s) {
  char credits[20];
  MenuUI::formatCredits(credits, sizeof(credits), s.credits);
  MenuUI::drawHeader2(g, Galaxy::systems[sysIdx].name, " MARKET",
                      credits,
                      /*leftA*/ MenuUI::TitleColor,
                      /*leftB*/ MenuUI::TitleColor,
                      /*right*/ MenuUI::CreditsColor,
                      /*sep  */ MenuUI::SepColor);

  // Column header strip.
  g.setTextSize(1);
  g.setTextColor(TFT_DARKCYAN, TFT_BLACK);
  g.setCursor(ColName,  13);  g.print("NAME");
  g.setCursor(ColPrice, 13);  g.print("PRICE");
  g.setCursor(ColQty,   13);  g.print("AVL");
  g.setCursor(ColHeld,  13);  g.print("HELD");
  g.drawFastHLine(0, 21, Config::ScreenW, MenuUI::SepColor);
}

inline void drawRow(M5Canvas& g, int slot, int itemIdx, int sysIdx,
                    const GameState& s, bool isCursor) {
  int y = ListY0 + slot * RowH;
  uint16_t bg = MenuUI::drawRowBg(g, y, RowH, isCursor,
                                  MenuUI::RowSelBgCool);

  const Market::Item& it = Market::itemAt(itemIdx);
  int scale = Faction::priceScalePermille(s, sysIdx);
  uint16_t price = Market::priceWithScale(sysIdx, itemIdx, scale);
  uint8_t  qty   = localQty[itemIdx];
  uint8_t  held  = s.cargo[itemIdx];

  uint16_t nameC = it.illegal ? TFT_RED : TFT_WHITE;
  if (qty == 0) nameC = TFT_DARKGREY;

  g.setTextSize(1);
  g.setTextColor(nameC, bg);
  g.setCursor(ColName, y);
  g.print(it.name);

  char buf[12];
  snprintf(buf, sizeof(buf), "%u.%u", price / 10, price % 10);
  g.setTextColor(TFT_GREEN, bg);
  g.setCursor(ColPrice, y);
  g.print(buf);

  snprintf(buf, sizeof(buf), "%u%s", (unsigned)qty, it.unit);
  g.setTextColor(qty > 0 ? TFT_LIGHTGREY : TFT_DARKGREY, bg);
  g.setCursor(ColQty, y);
  g.print(buf);

  snprintf(buf, sizeof(buf), "%u%s", (unsigned)held, it.unit);
  g.setTextColor(held > 0 ? TFT_CYAN : TFT_DARKGREY, bg);
  g.setCursor(ColHeld, y);
  g.print(buf);
}

inline void drawFooter(M5Canvas& g, const GameState& s) {
  char left[20];
  snprintf(left, sizeof(left), "CARGO %d/%d",
           s.cargoTotal(), (int)s.cargoMax);
  MenuUI::drawFooter3(g,
                      left,                       MenuUI::TitleColor,
                      nullptr,                    MenuUI::HintColor,
                      "< SELL  > BUY  ESC BACK",  MenuUI::HintColor);
}

inline void draw(M5Canvas& g, int sysIdx, const GameState& s) {
  MenuUI::clearBg(g);
  drawHeader(g, sysIdx, s);

  MenuUI::clampScroll(cursor, scroll, VisibleRows, (int)Market::N);
  for (int slot = 0; slot < VisibleRows; slot++) {
    int idx = scroll + slot;
    if (idx >= (int)Market::N) break;
    drawRow(g, slot, idx, sysIdx, s, idx == cursor);
  }

  MenuUI::drawScrollHints(g, scroll, VisibleRows, (int)Market::N,
                          ListY0, RowH);
  drawFooter(g, s);
}

// Returns true if the player attempted a buy/sell.
inline bool handleInput(const MenuInput& mk, int sysIdx, GameState& s) {
  if (mk.upE)    moveCursor(-1);
  if (mk.downE)  moveCursor(+1);
  int scale = Faction::priceScalePermille(s, sysIdx);
  if (mk.rightE) {
    int p = Market::priceWithScale(sysIdx, cursor, scale);
    int q = localQty[cursor];
    if (s.buyOne(cursor, q, p)) {
      localQty[cursor]--;
      Faction::applyTrade(s, sysIdx);
      return true;
    }
  }
  if (mk.leftE) {
    int p = Market::priceWithScale(sysIdx, cursor, scale);
    if (s.sellOne(cursor, p)) {
      Faction::applyTrade(s, sysIdx);
      return true;
    }
  }
  return false;
}

} // namespace MarketScreen
