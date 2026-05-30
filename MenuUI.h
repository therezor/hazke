#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"

// Shared menu / HUD chrome. Screens (TitleScreen, LandingScreen,
// EquipScreen, QuestScreen, …) all share the same header-bar /
// list-row / footer-hint / toast layout — this header centralizes that
// language so new screens can drop in without re-deriving the y-offsets
// or color palette.
//
// Conventions:
//   * `M5Canvas& g` is the back-buffer the parent already cleared (or
//     callers can use `clearBg(g)`).
//   * All public draw helpers leave the text size at 1 unless they
//     specifically chose 2 for a big menu item.
//   * Layout constants are exposed so screens that need to slot in
//     custom content can position it against the same y-grid.

namespace MenuUI {

// ---- Layout constants ----
constexpr int CharW   = 6;    // textSize 1
constexpr int CharW2  = 12;   // textSize 2
constexpr int CharH   = 8;
constexpr int CharH2  = 16;

constexpr int HeaderY      = 2;
constexpr int HeaderSepY   = 11;
constexpr int ContentY0    = 16;   // first row of body content
constexpr int CompactRowH  = 11;
constexpr int DenseRowH    = 8;

constexpr int FooterSepY   = Config::ScreenH - 12;
constexpr int FooterTextY  = Config::ScreenH - 9;
constexpr int ToastY       = Config::ScreenH - 22;

// ---- Standard palette ----
constexpr uint16_t TitleColor    = TFT_YELLOW;
constexpr uint16_t SubColor      = TFT_CYAN;
constexpr uint16_t HintColor     = TFT_DARKGREY;
constexpr uint16_t SepColor      = TFT_DARKGREY;
constexpr uint16_t SepColorWarm  = TFT_DARKCYAN;
constexpr uint16_t CreditsColor  = TFT_GREEN;
constexpr uint16_t ValueColor    = TFT_WHITE;
constexpr uint16_t LabelColor    = TFT_CYAN;
constexpr uint16_t DisabledColor = TFT_DARKGREY;
constexpr uint16_t StatusColor   = TFT_DARKCYAN;

// Selection-row background colors. Cool tint is the default; warm tint is
// the EquipScreen / "shop" feel.
constexpr uint16_t RowSelBgCool = 0x10A2;
constexpr uint16_t RowSelBgWarm = 0x18C3;

// Selection text color used over a fill-rect background.
constexpr uint16_t SelTextColor = TFT_YELLOW;

// ---- Small helpers ----

// Clear the back-buffer to black. Cheap one-liner so screens can just
// call MenuUI::clearBg(g) at the top of draw().
inline void clearBg(M5Canvas& g) { g.fillSprite(TFT_BLACK); }

// "CR <whole>.<tenth>" formatter (e.g. credits = 1234 -> "CR 123.4").
inline void formatCredits(char* buf, size_t cap, int creditsTenths) {
  snprintf(buf, cap, "CR %d.%d", creditsTenths / 10, creditsTenths % 10);
}

// Width of a string at textSize 1.
inline int strWidth(const char* s) {
  return (int)strlen(s) * CharW;
}

// Right-edge x for a textSize-1 string with `pad` from the right margin.
inline int rightAlignX(const char* s, int pad = 2) {
  return Config::ScreenW - (int)strlen(s) * CharW - pad;
}

// Center-x for a textSize-1 string.
inline int centerX(const char* s) {
  return (Config::ScreenW - strWidth(s)) / 2;
}

// Center-x for a textSize-2 string.
inline int centerX2(const char* s) {
  return (Config::ScreenW - (int)strlen(s) * CharW2) / 2;
}

// Print text right-aligned (textSize 1) at the given y.
inline void printRight(M5Canvas& g, int y, const char* s, uint16_t color,
                       int pad = 2) {
  g.setTextSize(1);
  g.setTextColor(color, TFT_BLACK);
  g.setCursor(rightAlignX(s, pad), y);
  g.print(s);
}

// Print text centered (textSize 1) at the given y.
inline void printCenter(M5Canvas& g, int y, const char* s, uint16_t color) {
  g.setTextSize(1);
  g.setTextColor(color, TFT_BLACK);
  g.setCursor(centerX(s), y);
  g.print(s);
}

// Pulse luminance from a phase. Used to throb a highlighted item.
// `lo` and `hi` are the floor and ceiling for the resulting 0..255 value.
inline uint8_t pulseLum(float phase, uint8_t lo = 160, uint8_t hi = 255) {
  float t = sinf(phase * 5.0f) * 0.5f + 0.5f;
  return (uint8_t)(lo + (hi - lo) * t);
}

// ---- Toast helpers ----

// Decrement a toast countdown by dt, clamped at 0. Returns true if the
// toast is still showing.
inline bool tickToast(float& t, float dt) {
  if (t <= 0.0f) return false;
  t -= dt;
  if (t < 0.0f) t = 0.0f;
  return t > 0.0f;
}

// Draw a fading toast above the footer. `t` is the remaining time (sec);
// `fadeWindow` is the alpha-down portion of the toast's life.
//   baseR/G/B is the toast's "full alpha" color in 0..255.
inline void drawToast(M5Canvas& g, const char* msg, float t,
                      float fadeWindow = 0.6f,
                      uint8_t baseR = 220, uint8_t baseG = 220,
                      uint8_t baseB = 0,
                      int y = ToastY) {
  if (t <= 0.0f || !msg || !msg[0]) return;
  float a = t > fadeWindow ? 1.0f : (fadeWindow > 0.0f ? t / fadeWindow : 1.0f);
  uint8_t r = (uint8_t)(baseR * a);
  uint8_t gg = (uint8_t)(baseG * a);
  uint8_t b = (uint8_t)(baseB * a);
  g.setTextSize(1);
  g.setTextColor(g.color565(r, gg, b), TFT_BLACK);
  g.setCursor(centerX(msg), y);
  g.print(msg);
}

// ---- Header / footer ----

// Standard header: left title, optional right stat, horizontal rule.
// `sepColor = 0` skips the rule (some screens draw their own decoration).
inline void drawHeader(M5Canvas& g,
                       const char* leftTitle,
                       const char* rightText  = nullptr,
                       uint16_t titleColor    = TitleColor,
                       uint16_t rightColor    = CreditsColor,
                       uint16_t sepColor      = SepColor,
                       int titleX             = 2) {
  g.setTextSize(1);
  if (leftTitle && leftTitle[0]) {
    g.setTextColor(titleColor, TFT_BLACK);
    g.setCursor(titleX, HeaderY);
    g.print(leftTitle);
  }
  if (rightText && rightText[0]) {
    printRight(g, HeaderY, rightText, rightColor);
  }
  if (sepColor) {
    g.drawFastHLine(0, HeaderSepY, Config::ScreenW, sepColor);
  }
}

// Two-segment left title: e.g. "DOCKED " in one color + system name in
// another (common pattern for in-screen context labels).
inline void drawHeader2(M5Canvas& g,
                        const char* leftA, const char* leftB,
                        const char* rightText = nullptr,
                        uint16_t leftAColor   = SubColor,
                        uint16_t leftBColor   = ValueColor,
                        uint16_t rightColor   = CreditsColor,
                        uint16_t sepColor     = SepColor) {
  g.setTextSize(1);
  int x = 2;
  if (leftA && leftA[0]) {
    g.setTextColor(leftAColor, TFT_BLACK);
    g.setCursor(x, HeaderY);
    g.print(leftA);
    x += (int)strlen(leftA) * CharW;
  }
  if (leftB && leftB[0]) {
    g.setTextColor(leftBColor, TFT_BLACK);
    g.setCursor(x, HeaderY);
    g.print(leftB);
  }
  if (rightText && rightText[0]) {
    printRight(g, HeaderY, rightText, rightColor);
  }
  if (sepColor) {
    g.drawFastHLine(0, HeaderSepY, Config::ScreenW, sepColor);
  }
}

// Footer rule + centered hint.
inline void drawFooter(M5Canvas& g, const char* hint,
                       uint16_t hintColor = HintColor,
                       uint16_t sepColor  = SepColor) {
  if (sepColor) {
    g.drawFastHLine(0, FooterSepY, Config::ScreenW, sepColor);
  }
  if (hint && hint[0]) {
    printCenter(g, FooterTextY, hint, hintColor);
  }
}

// Footer with up to three slots — left stat, centered hint, right stat.
// Any text pointer may be null. Useful for MarketScreen's "CARGO X/Y …
// SELL/BUY/BACK" layout.
inline void drawFooter3(M5Canvas& g,
                        const char* leftText, uint16_t leftColor,
                        const char* centerHint, uint16_t hintColor,
                        const char* rightText, uint16_t rightColor,
                        uint16_t sepColor = SepColor) {
  if (sepColor) {
    g.drawFastHLine(0, FooterSepY, Config::ScreenW, sepColor);
  }
  g.setTextSize(1);
  if (leftText && leftText[0]) {
    g.setTextColor(leftColor, TFT_BLACK);
    g.setCursor(2, FooterTextY);
    g.print(leftText);
  }
  if (centerHint && centerHint[0]) {
    printCenter(g, FooterTextY, centerHint, hintColor);
  }
  if (rightText && rightText[0]) {
    printRight(g, FooterTextY, rightText, rightColor);
  }
}

// ---- Big menu items (TitleScreen / DockedScreen feel) ----

// Centered "> LABEL <" menu item with a sine pulse on the selected row.
// Reserves the marker space on both sides so unselected rows don't shift.
// `destructive` recolors the row red-ish (used by "NEW CMDR" or other
// warning items). Pass `textSize=1` for compact menus (TitleScreen) or
// `textSize=2` for big "station signage" menus (DockedScreen).
inline void drawBigMenuItem(M5Canvas& g, int y, const char* label,
                            bool selected, float phase,
                            bool destructive = false,
                            uint16_t idleColor = TFT_DARKGREY,
                            int textSize = 2) {
  g.setTextSize(textSize);
  int cw = (textSize >= 2) ? CharW2 : CharW;
  int len = (int)strlen(label);
  int totalW = (len + 4) * cw;       // markers reserved on both sides
  int x = (Config::ScreenW - totalW) / 2;

  if (selected) {
    uint8_t lum = pulseLum(phase, 170, 255);
    uint16_t color = destructive
                     ? g.color565(lum, 40, 40)
                     : g.color565(lum, lum, lum);
    g.setTextColor(color, TFT_BLACK);
    g.setCursor(x, y);
    g.print("> ");
    g.print(label);
    g.print(" <");
  } else {
    uint16_t color = destructive ? 0x6000 : idleColor;
    g.setTextColor(color, TFT_BLACK);
    g.setCursor(x + 2 * cw, y);
    g.print(label);
  }
}

// ---- Compact list rows ----

// Paint the selection background for a compact list row and return the
// effective background color the caller should use for any text drawn on
// the same row. Pass `selBg = RowSelBgCool` or `RowSelBgWarm` to taste.
inline uint16_t drawRowBg(M5Canvas& g, int y, int rowH, bool selected,
                          uint16_t selBg = RowSelBgCool) {
  if (selected) {
    g.fillRect(0, y - 1, Config::ScreenW, rowH, selBg);
    return selBg;
  }
  return TFT_BLACK;
}

// One-shot "label + right-aligned value" row used by StatusScreen-style
// stat panels. Caller picks vertical pitch (typically 12 px).
inline void drawStatRow(M5Canvas& g, int y, const char* key, const char* value,
                        uint16_t valueColor = ValueColor,
                        uint16_t keyColor   = LabelColor,
                        int leftPad = 8, int rightPad = 8) {
  g.setTextSize(1);
  g.setTextColor(keyColor, TFT_BLACK);
  g.setCursor(leftPad, y);
  g.print(key);
  int vw = (int)strlen(value) * CharW;
  g.setTextColor(valueColor, TFT_BLACK);
  g.setCursor(Config::ScreenW - vw - rightPad, y);
  g.print(value);
}

// ---- Scrolling list helpers ----

// Keep `scroll` in range so `cursor` stays visible within `visible` rows
// of a `total`-item virtual list.
inline void clampScroll(int& cursor, int& scroll, int visible, int total) {
  if (total <= 0) { cursor = 0; scroll = 0; return; }
  if (cursor < 0)               cursor = 0;
  if (cursor >= total)          cursor = total - 1;
  if (cursor < scroll)          scroll = cursor;
  if (cursor >= scroll + visible) scroll = cursor - visible + 1;
  if (scroll < 0)               scroll = 0;
  int maxScroll = total - visible;
  if (maxScroll < 0) maxScroll = 0;
  if (scroll > maxScroll)       scroll = maxScroll;
}

// Draw the small `^` / `v` glyphs on the right edge to hint there's more
// list above/below the visible window. Pass the same `listY0`/`rowH`/
// `visible` you used to layout the list itself.
inline void drawScrollHints(M5Canvas& g, int scroll, int visible, int total,
                            int listY0, int rowH,
                            uint16_t color = TFT_DARKCYAN) {
  if (total <= visible) return;
  g.setTextSize(1);
  g.setTextColor(color, TFT_BLACK);
  if (scroll > 0) {
    g.setCursor(Config::ScreenW - 8, listY0);
    g.print('^');
  }
  if (scroll + visible < total) {
    g.setCursor(Config::ScreenW - 8, listY0 + (visible - 1) * rowH);
    g.print('v');
  }
}

} // namespace MenuUI
