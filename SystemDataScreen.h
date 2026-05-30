#pragma once
#include <M5GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "GameState.h"
#include "Hyperspace.h"

// Day 3: full data card for a single system.
//
// Reached by pressing ENTER on the galactic chart. Shows everything the
// chart panel shows plus average planet radius and a procedural flavor
// line built from the system's seed.

namespace SystemDataScreen {

inline void drawWrapped(M5Canvas& g, const char* text, int x, int y,
                        int maxChars, int lineHeight, int maxLines,
                        uint16_t color) {
  int textLen = (int)strlen(text);
  int lineStart = 0;
  int line = 0;
  g.setTextSize(1);
  g.setTextColor(color, TFT_BLACK);
  while (lineStart < textLen && line < maxLines) {
    int lineEnd = lineStart + maxChars;
    if (lineEnd >= textLen) {
      lineEnd = textLen;
    } else {
      int sp = lineEnd;
      while (sp > lineStart && text[sp] != ' ') sp--;
      if (sp > lineStart) lineEnd = sp;
    }
    g.setCursor(x, y + line * lineHeight);
    for (int i = lineStart; i < lineEnd; i++) g.print((char)text[i]);
    lineStart = lineEnd;
    while (lineStart < textLen && text[lineStart] == ' ') lineStart++;
    line++;
  }
}

// Decorative planet + station icon shown between flavor and footer.
inline void drawInSystemInset(M5Canvas& g, int sysIdx, int cx, int cy) {
  const auto& s = Galaxy::systems[sysIdx];
  bool agri = (uint8_t)s.economy >= 4;
  uint16_t pcol = agri ? g.color565(60, 150, 80)
                       : g.color565(120, 130, 160);

  // Planet body
  g.fillCircle(cx, cy, 7, pcol);
  // Terminator (sunward face is bright, dark side is shaded)
  g.fillCircle(cx + 3, cy + 1, 5, g.color565(15, 15, 25));
  // Rim
  g.drawCircle(cx, cy, 7, TFT_LIGHTGREY);

  // Orbit (saturn-ring look) — only the front half so it doesn't crash
  // through the planet's far side.
  g.drawEllipse(cx, cy, 14, 5, 0x4208);

  // Station — a small yellow square on the orbit's left tip
  int stx = cx - 14;
  int sty = cy;
  g.fillRect(stx - 1, sty - 1, 3, 3, TFT_YELLOW);

  // Sun glint to the far left
  g.fillCircle(cx - 28, cy - 1, 2, TFT_YELLOW);
  g.drawPixel(cx - 32, cy - 1, 0x8400);
  g.drawPixel(cx - 25, cy - 1, 0x8400);
}

inline void drawStat(M5Canvas& g, int x, int y, const char* k, const char* v,
                     uint16_t vc = TFT_WHITE) {
  g.setTextSize(1);
  g.setTextColor(TFT_CYAN, TFT_BLACK);
  g.setCursor(x, y);
  g.print(k);
  g.setTextColor(vc, TFT_BLACK);
  g.setCursor(x + 7 * 6, y);  // key padded to 7 chars
  g.print(v);
}

inline void draw(M5Canvas& g, int currentIdx, int targetIdx,
                 const GameState& state) {
  g.fillSprite(TFT_BLACK);

  const auto& s = Galaxy::systems[targetIdx];

  // Header
  g.setTextSize(1);
  g.setTextColor(TFT_YELLOW, TFT_BLACK);
  g.setCursor(2, 2);
  g.print("DATA ON ");
  g.setTextColor(TFT_WHITE, TFT_BLACK);
  g.print(s.name);
  g.drawFastHLine(0, 11, Config::ScreenW, TFT_DARKGREY);

  char buf[32];

  // Two-column grid. Left column starts at x=4, right at x=124.
  const int leftX  = 4;
  const int rightX = 124;
  const int row0   = 16;
  const int rowH   = 11;

  // Left column
  drawStat(g, leftX, row0 + 0 * rowH, "ECONOMY",
           Galaxy::economyName(s.economy));
  drawStat(g, leftX, row0 + 1 * rowH, "GOV    ",
           Galaxy::govName(s.government));
  snprintf(buf, sizeof(buf), "%u", (unsigned)s.techLevel);
  drawStat(g, leftX, row0 + 2 * rowH, "TECH LV", buf);
  snprintf(buf, sizeof(buf), "%u.%uB",
           (unsigned)(s.population / 10), (unsigned)(s.population % 10));
  drawStat(g, leftX, row0 + 3 * rowH, "POP    ", buf);

  // Right column
  snprintf(buf, sizeof(buf), "%ukm", (unsigned)Galaxy::systemRadius(targetIdx));
  drawStat(g, rightX, row0 + 0 * rowH, "RADIUS ", buf);
  snprintf(buf, sizeof(buf), "%u MCr", (unsigned)s.productivity);
  drawStat(g, rightX, row0 + 1 * rowH, "PRODUCE", buf);
  float d = Galaxy::distanceLY(currentIdx, targetIdx);
  if (d < 0) d = 0;
  snprintf(buf, sizeof(buf), "%d.%dLY", (int)d, ((int)(d * 10)) % 10);
  uint16_t dcol = (d < 7.0f) ? TFT_GREEN : (d < 20.0f ? TFT_YELLOW : TFT_RED);
  drawStat(g, rightX, row0 + 2 * rowH, "DISTANCE", buf);
  // overwrite value with the colored variant (drawStat used WHITE)
  // — simpler: just reprint over with the right color
  g.setTextColor(dcol, TFT_BLACK);
  g.setCursor(rightX + 7 * 6, row0 + 2 * rowH);
  g.print(buf);

  const char* status = (currentIdx == targetIdx) ? "HOME" : "TARGET";
  uint16_t sc = (currentIdx == targetIdx) ? TFT_GREEN : TFT_ORANGE;
  drawStat(g, rightX, row0 + 3 * rowH, "STATUS ", status, sc);

  // Flavor section
  int flavorY = row0 + 4 * rowH + 6;
  g.drawFastHLine(2, flavorY - 4, Config::ScreenW - 4, TFT_DARKGREY);

  char flavor[96];
  Galaxy::buildFlavor(targetIdx, flavor, sizeof(flavor));
  drawWrapped(g, flavor, 4, flavorY, /*maxChars=*/38, /*lineHeight=*/9,
              /*maxLines=*/2, TFT_LIGHTGREY);

  // In-system inset between flavor and footer
  drawInSystemInset(g, targetIdx, Config::ScreenW / 2 + 8, 110);

  // Footer hint — JUMP is only highlighted when we can actually go there.
  g.drawFastHLine(0, Config::ScreenH - 12, Config::ScreenW, TFT_DARKGREY);
  bool reachable = Hyperspace::canJump(state, currentIdx, targetIdx);
  int cost = Hyperspace::jumpCostTenths(currentIdx, targetIdx);

  g.setTextColor(reachable ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  g.setCursor(2, Config::ScreenH - 9);
  g.print("ENTER=JUMP");

  // Jump cost in credits — red if the wallet can't cover it.
  char costBuf[24];
  snprintf(costBuf, sizeof(costBuf), "%d.%dCR",
           cost / 10, cost % 10);
  int fw = (int)strlen(costBuf) * 6;
  bool affordable = state.credits >= cost;
  g.setTextColor(affordable ? TFT_LIGHTGREY : TFT_RED, TFT_BLACK);
  g.setCursor((Config::ScreenW - fw) / 2, Config::ScreenH - 9);
  g.print(costBuf);

  // Back hint right`
  const char* hint = "ESC=BACK";
  int hw = (int)strlen(hint) * 6;
  g.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g.setCursor(Config::ScreenW - hw - 2, Config::ScreenH - 9);
  g.print(hint);
}

} // namespace SystemDataScreen
