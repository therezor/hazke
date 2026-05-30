#pragma once
#include <M5GFX.h>
#include <math.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "GameState.h"
#include "Market.h"
#include "NPCShip.h"
#include "Faction.h"

// Refit R16: minimal in-flight trade dialog when the player hails an NPC.
//
// The NPC offers 3 commodities, picked deterministically from its hailSeed
// (so two hails on the same trader quote the same goods until they leave
// the system). Asks and bids fan out from the station price by a per-NPC
// spread, giving the player a reason to seek out NPCs (or avoid them).
//
// Input model: ARROWS UP/DOWN to move row; RIGHT = buy one, LEFT = sell
// one; ESC closes. The outer state machine in hazke.ino owns mode flow.

namespace NPCTradeScreen {

constexpr int Slots = 3;

inline int      sysIdx     = 0;
inline int      shipIdx    = -1;
inline int      selected   = 0;
inline int      commodity[Slots] = {0, 0, 0};
inline uint16_t askPrice[Slots]  = {0, 0, 0};  // NPC sells (player buys) at this
inline uint16_t bidPrice[Slots]  = {0, 0, 0};  // NPC buys  (player sells) at this
inline uint8_t  npcStock[Slots]  = {0, 0, 0};
inline char     traderName[16]   = "";
inline float    toast            = 0.0f;
inline char     toastMsg[24]     = "";
// Loot mode: set when the hailed ship isn't a Trader. Items are
// free (askPrice=0), there's no bid (player can't sell back), and
// the header reads LOOT instead of TRADER.
inline bool     lootMode         = false;

inline void flashToast(const char* msg) {
  strncpy(toastMsg, msg, sizeof(toastMsg));
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toast = 1.0f;
}

inline void tick(float dt) {
  if (toast > 0.0f) {
    toast -= dt;
    if (toast < 0.0f) toast = 0.0f;
  }
}

inline void enter(int sys, int npcIdx, uint32_t hailSeed) {
  sysIdx   = sys;
  shipIdx  = npcIdx;
  selected = 0;
  toast    = 0.0f;
  // Decide trade vs. loot mode from the hailed ship's role.
  lootMode = false;
  if (npcIdx >= 0 && npcIdx < NPCShip::MaxNPCs) {
    lootMode = NPCShip::ships[npcIdx].role != NPCShip::Role::Trader;
  }
  uint32_t s = hailSeed ^ 0xA5A5A5A5u;
  for (int i = 0; i < Slots; i++) {
    int c = (int)(Galaxy::lcg(s) % (uint32_t)Market::N);
    commodity[i] = c;
    if (lootMode) {
      // Loot is a free scratch in the wreck's hold — no prices, small
      // stacks. Some slots come up empty so loot quality varies.
      askPrice[i]  = 0;
      bidPrice[i]  = 0;
      npcStock[i]  = (uint8_t)(Galaxy::lcg(s) % 3);   // 0..2 tons
    } else {
      uint16_t base = Market::priceAt(sysIdx, c);
      // Two-tier spread: NPCs ask 5-30% over base, bid 5-25% under.
      int askPct = 5 + (int)(Galaxy::lcg(s) % 26);
      int bidPct = 5 + (int)(Galaxy::lcg(s) % 21);
      int ask = (int)base + ((int)base * askPct) / 100;
      int bid = (int)base - ((int)base * bidPct) / 100;
      if (ask < 1)   ask = 1;
      if (bid < 1)   bid = 1;
      if (ask > 9999) ask = 9999;
      askPrice[i]  = (uint16_t)ask;
      bidPrice[i]  = (uint16_t)bid;
      npcStock[i]  = (uint8_t)(2 + (Galaxy::lcg(s) % 8));   // 2..9 tons
    }
  }
  if (lootMode) {
    snprintf(traderName, sizeof(traderName), "LOOT #%03u",
             (unsigned)(hailSeed % 1000u));
  } else {
    snprintf(traderName, sizeof(traderName), "TRADER #%03u",
             (unsigned)(hailSeed % 1000u));
  }
}

inline void moveUp()   { selected = (selected - 1 + Slots) % Slots; }
inline void moveDown() { selected = (selected + 1) % Slots; }

// R22: NPC trades nudge the trader's own faction (their homeFaction) so
// hailing FRT traders builds Free-Trader standing even while you're
// docked somewhere lawful. Inlined into tryBuy/trySell below.

inline bool tryBuy(GameState& gs) {
  int i = selected;
  if (npcStock[i] == 0)         { flashToast(lootMode ? "EMPTY" : "NPC EMPTY"); return false; }
  if (gs.cargoFree() <= 0)      { flashToast("HOLD FULL");   return false; }
  if (gs.credits < askPrice[i]) { flashToast("LOW CREDITS"); return false; }
  if (gs.buyOne(commodity[i], npcStock[i], askPrice[i])) {
    npcStock[i]--;
    // No reputation gain for picking goods off a cracked-open hulk —
    // only legitimate trade nudges standing.
    if (!lootMode && shipIdx >= 0 && shipIdx < NPCShip::MaxNPCs) {
      Faction::nudge(gs, (Faction::Id)NPCShip::ships[shipIdx].homeFaction, +1);
    }
    if (lootMode) flashToast("LOOTED");
    return true;
  }
  return false;
}

inline bool trySell(GameState& gs) {
  // A pirate/patrol with broken shields isn't going to pay you for
  // cargo — only proper traders run a two-way deal.
  if (lootMode) { flashToast("NO BIDS"); return false; }
  int i = selected;
  if (gs.cargo[commodity[i]] == 0) { flashToast("NONE IN HOLD"); return false; }
  if (gs.sellOne(commodity[i], bidPrice[i])) {
    if (npcStock[i] < 99) npcStock[i]++;
    if (shipIdx >= 0 && shipIdx < NPCShip::MaxNPCs) {
      Faction::nudge(gs, (Faction::Id)NPCShip::ships[shipIdx].homeFaction, +1);
    }
    return true;
  }
  return false;
}

inline void draw(M5Canvas& g, const GameState& gs) {
  g.fillSprite(TFT_BLACK);

  // Header
  g.setTextSize(1);
  uint16_t headCol = lootMode ? TFT_RED : TFT_YELLOW;
  g.setTextColor(headCol, TFT_BLACK);
  g.setCursor(4, 4);
  g.print(lootMode ? "LOOT  " : "HAIL  ");
  g.setTextColor(TFT_WHITE, TFT_BLACK);
  g.print(traderName);

  char buf[24];
  snprintf(buf, sizeof(buf), "CR %d.%d", gs.credits / 10, gs.credits % 10);
  int w = (int)strlen(buf) * 6;
  g.setTextColor(TFT_GREEN, TFT_BLACK);
  g.setCursor(Config::ScreenW - w - 4, 4);
  g.print(buf);

  g.drawFastHLine(0, 14, Config::ScreenW, TFT_DARKGREY);

  // Column headers
  g.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g.setCursor(4,   20);  g.print("COMMODITY");
  g.setCursor(104, 20);  g.print("BUY");
  g.setCursor(142, 20);  g.print("SELL");
  g.setCursor(180, 20);  g.print("STK");
  g.setCursor(208, 20);  g.print("HLD");

  // Rows
  const int rowH = 14;
  const int firstY = 34;
  for (int i = 0; i < Slots; i++) {
    int y = firstY + i * rowH;
    bool sel = (i == selected);
    uint16_t bg = sel ? 0x18C3 : TFT_BLACK;
    if (sel) g.fillRect(0, y - 2, Config::ScreenW, 11, bg);
    uint16_t fg = sel ? TFT_YELLOW : TFT_LIGHTGREY;
    g.setTextColor(fg, bg);

    g.setCursor(4, y);
    g.print(Market::itemAt(commodity[i]).name);

    if (lootMode) {
      g.setCursor(104, y); g.print("FREE");
      g.setCursor(142, y); g.print("--");
    } else {
      snprintf(buf, sizeof(buf), "%d.%d",
               askPrice[i] / 10, askPrice[i] % 10);
      g.setCursor(104, y); g.print(buf);

      snprintf(buf, sizeof(buf), "%d.%d",
               bidPrice[i] / 10, bidPrice[i] % 10);
      g.setCursor(142, y); g.print(buf);
    }

    snprintf(buf, sizeof(buf), "%2u", (unsigned)npcStock[i]);
    g.setCursor(180, y); g.print(buf);

    snprintf(buf, sizeof(buf), "%2u", (unsigned)gs.cargo[commodity[i]]);
    g.setCursor(208, y); g.print(buf);
  }

  // Footer
  g.drawFastHLine(0, Config::ScreenH - 12, Config::ScreenW, TFT_DARKGREY);
  g.setTextColor(TFT_DARKGREY, TFT_BLACK);
  const char* hint = lootMode
                       ? "UP/DN  RIGHT=TAKE  ESC"
                       : "UP/DN  RIGHT=BUY  LEFT=SELL  ESC";
  g.setCursor((Config::ScreenW - (int)strlen(hint) * 6) / 2,
              Config::ScreenH - 9);
  g.print(hint);

  if (toast > 0.0f) {
    float a = toast > 0.5f ? 1.0f : toast / 0.5f;
    uint8_t lum = (uint8_t)(220 * a);
    uint16_t col = g.color565(lum, lum / 2, lum / 4);
    g.setTextColor(col, TFT_BLACK);
    int len = (int)strlen(toastMsg);
    g.setCursor((Config::ScreenW - len * 6) / 2, Config::ScreenH - 24);
    g.print(toastMsg);
  }
}

} // namespace NPCTradeScreen
