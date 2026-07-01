#pragma once
#include <M5Cardputer.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "GameState.h"
#include "MenuUI.h"
#include "Audio.h"

// NEW COMMANDER name entry — shown between the title menu's NEW GAME and
// the first flight. The player types on the Cardputer QWERTY; ENTER
// confirms (a blank name keeps the classic JAMESON default), ESC backs
// out to the title. Both of those edges come from pollMenuInput() in the
// main loop; only the character/backspace stream is read here.
//
// Typing uses manual per-char edge tracking against keysState().word —
// the library's Keyboard.isChange() only compares held-key COUNTS, so it
// misses same-count rollover and fires on release. Tracking each char
// ourselves matches the pollMenuInput() idiom and gives one append per
// physical press.

namespace NameEntryScreen {

constexpr int MaxLen = GameState::NameCap - 1;   // 11 visible chars

inline char buf[GameState::NameCap] = "";
inline int  len = 0;
inline bool prevDown[128] = {false};
inline bool prevDel = false;

inline void enter() {
  buf[0] = '\0';
  len    = 0;
  memset(prevDown, 0, sizeof prevDown);
  prevDel = true;   // swallow a held backspace across the mode switch
}

// Fold a raw keyboard char into the allowed charset; 0 = rejected.
inline char foldChar(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (c == '_') c = '-';
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
  if (c == '-' && len > 0) return c;   // dash allowed, but not leading
  return 0;
}

// Per-frame character stream. Appends folded chars on their press edge,
// handles backspace, beeps on anything rejected (bad char / full buffer).
inline void handleTyping() {
  bool down[128] = {false};
  bool del = false;

  if (M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    del = st.del;
    for (char c : st.word) {
      uint8_t u = (uint8_t)c;
      if (u >= 128) continue;
      down[u] = true;
      if (prevDown[u]) continue;       // still held from last frame
      char f = foldChar(c);
      if (f == 0)          { Audio::deny(); continue; }
      if (len >= MaxLen)   { Audio::deny(); continue; }
      buf[len++] = f;
      buf[len]   = '\0';
    }
  }

  if (del && !prevDel && len > 0) {
    buf[--len] = '\0';
  }

  memcpy(prevDown, down, sizeof prevDown);
  prevDel = del;
}

// Copy the typed name into the freshly-reset commander. A blank entry
// leaves reset()'s JAMESON default in place.
inline void applyTo(GameState& g) {
  if (len == 0) return;
  strncpy(g.commanderName, buf, GameState::NameCap);
  g.commanderName[GameState::NameCap - 1] = '\0';
}

inline void draw(M5Canvas& g, float phase) {
  MenuUI::clearBg(g);
  MenuUI::drawHeader(g, "NEW COMMANDER");

  // 11-cell rail centered on screen; each cell is one size-2 character
  // with a 2 px gap, underlined so the remaining space reads at a glance.
  const int cellW  = MenuUI::CharW2 + 2;
  const int railW  = MaxLen * cellW;
  const int railX  = (Config::ScreenW - railW) / 2;
  const int nameY  = 52;
  const int lineY  = nameY + MenuUI::CharH2 + 3;

  g.setTextSize(2);
  for (int i = 0; i < MaxLen; i++) {
    int x = railX + i * cellW;
    g.drawFastHLine(x, lineY, MenuUI::CharW2, MenuUI::SepColor);
    if (i < len) {
      g.setTextColor(TFT_WHITE, TFT_BLACK);
      g.setCursor(x, nameY);
      g.print(buf[i]);
    }
  }

  // Pulsing cursor block on the next free cell.
  if (len < MaxLen) {
    uint8_t lum = MenuUI::pulseLum(phase, 60, 220);
    g.fillRect(railX + len * cellW, nameY,
               MenuUI::CharW2, MenuUI::CharH2, g.color565(lum, lum, 0));
  }

  char counter[8];
  snprintf(counter, sizeof(counter), "%d/%d", len, MaxLen);
  MenuUI::printCenter(g, lineY + 8, counter, MenuUI::StatusColor);

  MenuUI::printCenter(g, lineY + 22, "BLANK = JAMESON", MenuUI::HintColor);
  MenuUI::drawFooter(g, "TYPE NAME  ENTER=OK  ESC=BACK");
}

} // namespace NameEntryScreen
