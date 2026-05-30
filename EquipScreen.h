#pragma once
#include <M5GFX.h>
#include <string.h>
#include "Config.h"
#include "GameState.h"
#include "MenuUI.h"

// Refit R18: equipment shop. Reached from LandingScreen → EQUIP.
//
// 9 catalog rows. ↑/↓ navigate, ENTER buys the highlighted item, ESC
// returns to the docked menu. Status badge on the right of each row
// shows OWNED / MAX / FULL / etc. so the player can see at a glance
// what's still buyable. Prices are listed in whole CR but charged in
// tenths-CR against `game.credits`. REFUEL scales with empty fraction.

namespace EquipScreen {

struct Item {
  const char* label;
  uint16_t    priceCR;   // base price in whole credits; REFUEL is dynamic
};

constexpr int N = 7;

enum : int {
  ItemMissile = 0,
  ItemECM,
  ItemLargeHold,
  ItemBeamLaser,
  ItemMilLaser,
  ItemDockComp,
  ItemEscapePod,
};

inline const Item items[N] = {
  {"MISSILE",       30},
  {"ECM SYSTEM",   600},
  {"LARGE HOLD",   400},
  {"BEAM LASER",  1000},
  {"MIL LASER",   6000},
  {"DOCK COMP",   1500},
  {"ESCAPE POD",  1000},
};

inline int   selected = 0;
inline float toast    = 0.0f;
inline char  toastMsg[24] = "";

inline void moveUp()   { selected = (selected - 1 + N) % N; }
inline void moveDown() { selected = (selected + 1) % N; }

inline void flashToast(const char* msg) {
  strncpy(toastMsg, msg, sizeof(toastMsg));
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toast = 1.2f;
}

inline void tick(float dt) { MenuUI::tickToast(toast, dt); }

// Effective price (tenths-CR) for a row given current player state.
inline int priceTenthsFor(int idx, const GameState& /*s*/) {
  return (int)items[idx].priceCR * 10;
}

// Status string for the right-hand column. "" means buyable.
inline const char* statusFor(int idx, const GameState& s) {
  switch (idx) {
    case ItemMissile:    return (s.missiles >= 4)              ? "MAX 4"     : "";
    case ItemECM:        return s.ecm                          ? "OWNED"     : "";
    case ItemLargeHold:  return (s.cargoMax >= s.CargoMaxLarge) ? "OWNED"     : "";
    case ItemBeamLaser:  return (s.laserTier >= 1)             ? "OWNED"     : "";
    case ItemMilLaser:   return (s.laserTier >= 2)             ? "OWNED"
                              : (s.laserTier <  1)             ? "NEED BEAM" : "";
    case ItemDockComp:   return s.dockingComputer              ? "OWNED"     : "";
    case ItemEscapePod:  return s.escapePod                    ? "OWNED"     : "";
  }
  return "";
}

inline void apply(int idx, GameState& s) {
  switch (idx) {
    case ItemMissile:    s.missiles++;                  break;
    case ItemECM:        s.ecm = true;                  break;
    case ItemLargeHold:  s.cargoMax = s.CargoMaxLarge;  break;
    case ItemBeamLaser:  s.laserTier = 1;               break;
    case ItemMilLaser:   s.laserTier = 2;               break;
    case ItemDockComp:   s.dockingComputer = true;      break;
    case ItemEscapePod:  s.escapePod = true;            break;
  }
}

inline bool tryBuy(GameState& s) {
  int idx = selected;
  const char* st = statusFor(idx, s);
  if (strcmp(st, "OWNED")     == 0) { flashToast("ALREADY OWNED");    return false; }
  if (strcmp(st, "MAX 4")     == 0) { flashToast("MISSILE RACK FULL"); return false; }
  if (strcmp(st, "NEED BEAM") == 0) { flashToast("NEEDS BEAM FIRST"); return false; }

  int price = priceTenthsFor(idx, s);
  if (s.credits < price) { flashToast("LOW CREDITS"); return false; }

  s.credits -= price;
  apply(idx, s);
  flashToast("PURCHASED");
  return true;
}

inline void draw(M5Canvas& g, const GameState& s) {
  MenuUI::clearBg(g);

  char credits[20];
  MenuUI::formatCredits(credits, sizeof(credits), s.credits);
  MenuUI::drawHeader(g, "EQUIPMENT SHOP", credits);

  // Rows.
  const int firstY = MenuUI::ContentY0 + 2;     // sits below the header rule
  const int rowH   = MenuUI::CompactRowH;
  for (int i = 0; i < N; i++) {
    int y = firstY + i * rowH;
    bool sel = (i == selected);
    uint16_t bg = MenuUI::drawRowBg(g, y, rowH - 1, sel,
                                    MenuUI::RowSelBgWarm);

    const char* st = statusFor(i, s);
    bool unavailable = (strcmp(st, "OWNED") == 0) ||
                       (strcmp(st, "MAX 4") == 0) ||
                       (strcmp(st, "FULL")  == 0) ||
                       (strcmp(st, "NEED BEAM") == 0);
    uint16_t fg = sel ? MenuUI::SelTextColor
                      : (unavailable ? MenuUI::DisabledColor : TFT_LIGHTGREY);
    g.setTextSize(1);
    g.setTextColor(fg, bg);
    g.setCursor(4, y);
    g.print(items[i].label);

    // Price column.
    int price = priceTenthsFor(i, s);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d.%d", price / 10, price % 10);
    int pw = (int)strlen(buf) * MenuUI::CharW;
    g.setCursor(160 - pw, y);
    g.print(buf);

    // Status badge (right-aligned).
    if (st[0]) {
      uint16_t scol = sel ? MenuUI::SelTextColor : MenuUI::StatusColor;
      g.setTextColor(scol, bg);
      int sw = (int)strlen(st) * MenuUI::CharW;
      g.setCursor(Config::ScreenW - sw - 4, y);
      g.print(st);
    }
  }

  MenuUI::drawFooter(g, "UP/DN  ENTER=BUY  ESC=BACK");
  MenuUI::drawToast(g, toastMsg, toast);
}

} // namespace EquipScreen
