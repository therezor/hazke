#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "Hyperspace.h"

// Day 2: galactic chart.
//
// Left side of the screen is the 256x256 system map, scaled into a
// rectangular viewport. Right side is a static info panel for the
// currently-targeted system. The player's current system is drawn
// with a cross; the target is drawn with animated brackets.

namespace ChartScreen {

constexpr int MapX = 2;
constexpr int MapY = 12;
constexpr int MapW = 150;
constexpr int MapH = 108;

constexpr int PanelX = MapX + MapW + 4;
constexpr int PanelY = 12;
constexpr int PanelW = Config::ScreenW - PanelX - 2;

// `distanceLY` uses a 0.1 LY-per-unit scale with y squashed 2:1. The
// 7-LY hyperspace radius is therefore an ellipse in map-pixel space:
// wider on y because dy is halved before the distance check.
constexpr float DistScale = 0.1f;

// --- coordinate mapping ----------------------------------------------------
inline int mapSx(uint8_t sx) {
  return MapX + 1 + (int)((long)sx * (MapW - 2) / 255);
}
inline int mapSy(uint8_t sy) {
  return MapY + 1 + (int)((long)sy * (MapH - 2) / 255);
}

// --- directional cursor ----------------------------------------------------
// dir: 0=up, 1=down, 2=left, 3=right. Picks the nearest system that
// lies strictly in the requested direction; ties broken by perpendicular
// offset (so left/right doesn't randomly snap up or down).
inline int nearestInDirection(int from, int dir) {
  const auto& src = Galaxy::systems[from];
  int best = -1;
  long bestScore = LONG_MAX;
  for (int i = 0; i < Galaxy::NumSystems; i++) {
    if (i == from) continue;
    int dx = (int)Galaxy::systems[i].x - (int)src.x;
    int dy = (int)Galaxy::systems[i].y - (int)src.y;
    long score;
    switch (dir) {
      case 0: if (dy >= 0) continue; score = (long)(-dy) + (long)abs(dx) * 2; break;
      case 1: if (dy <= 0) continue; score = (long)( dy) + (long)abs(dx) * 2; break;
      case 2: if (dx >= 0) continue; score = (long)(-dx) + (long)abs(dy) * 2; break;
      case 3: if (dx <= 0) continue; score = (long)( dx) + (long)abs(dy) * 2; break;
      default: continue;
    }
    if (score < bestScore) { bestScore = score; best = i; }
  }
  return best < 0 ? from : best;
}

// --- drawing helpers -------------------------------------------------------
inline void drawHeader(M5Canvas& g) {
  g.setTextSize(1);
  g.setTextColor(TFT_YELLOW, TFT_BLACK);
  g.setCursor(2, 2);
  g.print("GALACTIC CHART");

  g.drawFastHLine(0, 10, Config::ScreenW, TFT_DARKGREY);
}

inline void drawFooter(M5Canvas& g, int targetIdx) {
  g.setTextSize(1);
  g.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g.drawFastHLine(0, Config::ScreenH - 12, Config::ScreenW, TFT_DARKGREY);
  g.setCursor(2, Config::ScreenH - 9);
  g.print("ARROWS  ENTER=DATA  DEL=BACK");
  // Idx readout on the right
  char buf[16];
  snprintf(buf, sizeof(buf), "SYS %02d/%02d", targetIdx, Galaxy::NumSystems - 1);
  int w = (int)strlen(buf) * 6;
  g.setCursor(Config::ScreenW - w - 2, Config::ScreenH - 9);
  g.setTextColor(TFT_DARKCYAN, TFT_BLACK);
  g.print(buf);
}

inline void drawMap(M5Canvas& g, int currentIdx, int targetIdx, float phase) {
  // Frame
  g.drawRect(MapX, MapY, MapW, MapH, TFT_DARKGREY);

  // R15: draw the whole gate graph as faint edges so the routes are
  // visible at a glance. We render each edge once (i < j) and brighten
  // edges that touch the current system.
  for (int i = 0; i < Galaxy::NumSystems; i++) {
    int ax = mapSx(Galaxy::systems[i].x);
    int ay = mapSy(Galaxy::systems[i].y);
    for (int k = 0; k < Galaxy::gateCount[i]; k++) {
      int j = Galaxy::gates[i][k];
      if (j <= i) continue;  // draw each undirected edge once
      int bx = mapSx(Galaxy::systems[j].x);
      int by = mapSy(Galaxy::systems[j].y);
      bool touchesHome = (i == currentIdx) || (j == currentIdx);
      uint16_t col = touchesHome ? 0x05E0 : 0x10A2;  // bright vs dim green
      g.drawLine(ax, ay, bx, by, col);
    }
  }

  // Highlight the route to the targeted system if it's within jump range.
  if (currentIdx != targetIdx &&
      Hyperspace::inJumpRange(currentIdx, targetIdx)) {
    const auto& a = Galaxy::systems[currentIdx];
    const auto& b = Galaxy::systems[targetIdx];
    g.drawLine(mapSx(a.x), mapSy(a.y),
               mapSx(b.x), mapSy(b.y), TFT_YELLOW);
  }

  // System dots. Systems inside the jump radius pop bright green; the
  // rest dim out in short-range mode and stay mid-green in long-range
  // mode so the cluster is still readable.
  for (int i = 0; i < Galaxy::NumSystems; i++) {
    if (i == currentIdx || i == targetIdx) continue;
    int sx = mapSx(Galaxy::systems[i].x);
    int sy = mapSy(Galaxy::systems[i].y);
    bool reachable = Hyperspace::inJumpRange(currentIdx, i);
    uint16_t col = reachable ? 0x07E0   // bright green for direct gate links
                             : 0x0420;  // mid-green for the rest
    g.drawPixel(sx, sy, col);
  }

  // Current system: solid filled square
  {
    const auto& s = Galaxy::systems[currentIdx];
    int sx = mapSx(s.x);
    int sy = mapSy(s.y);
    g.fillRect(sx - 1, sy - 1, 3, 3, TFT_GREEN);
  }

  // Target system: animated brackets, pulsing
  {
    const auto& s = Galaxy::systems[targetIdx];
    int sx = mapSx(s.x);
    int sy = mapSy(s.y);
    float t = sinf(phase * 6.0f) * 0.5f + 0.5f;
    uint8_t lum = (uint8_t)(140 + 115 * t);
    uint16_t c = g.color565(lum, lum, 0);
    g.drawPixel(sx, sy, TFT_WHITE);
    // [ ]
    int off = 3;
    g.drawFastVLine(sx - off, sy - 2, 5, c);
    g.drawPixel(sx - off + 1, sy - 2, c);
    g.drawPixel(sx - off + 1, sy + 2, c);
    g.drawFastVLine(sx + off, sy - 2, 5, c);
    g.drawPixel(sx + off - 1, sy - 2, c);
    g.drawPixel(sx + off - 1, sy + 2, c);
  }
}

inline void drawPanel(M5Canvas& g, int currentIdx, int targetIdx) {
  const auto& t = Galaxy::systems[targetIdx];

  // Border
  g.drawRect(PanelX, PanelY, PanelW, MapH, TFT_DARKGREY);

  // Name (yellow, slightly larger)
  g.setTextSize(1);
  g.setTextColor(TFT_YELLOW, TFT_BLACK);
  g.setCursor(PanelX + 4, PanelY + 4);
  g.print(t.name);

  g.drawFastHLine(PanelX + 2, PanelY + 14, PanelW - 4, TFT_DARKGREY);

  // Stat rows (key in cyan, value in white)
  auto row = [&](int y, const char* k, const char* v, uint16_t vc = TFT_WHITE) {
    g.setTextColor(TFT_CYAN, TFT_BLACK);
    g.setCursor(PanelX + 4, y);
    g.print(k);
    g.setTextColor(vc, TFT_BLACK);
    // value right-aligned to panel edge
    int vw = (int)strlen(v) * 6;
    g.setCursor(PanelX + PanelW - vw - 4, y);
    g.print(v);
  };

  char buf[16];

  // Truncate economy/gov labels — they can run long. Use a short form.
  const char* econ = Galaxy::economyName(t.economy);
  const char* gov  = Galaxy::govName(t.government);
  row(PanelY + 19, "ECN", econ);
  row(PanelY + 30, "GOV", gov);

  snprintf(buf, sizeof(buf), "%u", (unsigned)t.techLevel);
  row(PanelY + 41, "TL",  buf);

  snprintf(buf, sizeof(buf), "%u.%uB", (unsigned)(t.population / 10),
                                       (unsigned)(t.population % 10));
  row(PanelY + 52, "POP", buf);

  snprintf(buf, sizeof(buf), "%u", (unsigned)t.productivity);
  row(PanelY + 63, "PRD", buf);

  float d = Galaxy::distanceLY(currentIdx, targetIdx);
  if (d < 0) d = 0;
  if (d > 999) d = 999;
  snprintf(buf, sizeof(buf), "%d.%d", (int)d, ((int)(d * 10)) % 10);
  uint16_t dcol = (d < 7.0f) ? TFT_GREEN : (d < 20.0f ? TFT_YELLOW : TFT_RED);
  row(PanelY + 74, "LY",  buf, dcol);

  // Status line at the bottom — direct gate neighbour = jumpable.
  g.drawFastHLine(PanelX + 2, PanelY + 88, PanelW - 4, TFT_DARKGREY);
  g.setCursor(PanelX + 4, PanelY + 93);
  if (currentIdx == targetIdx) {
    g.setTextColor(TFT_GREEN, TFT_BLACK);
    g.print("- HOME -");
  } else if (Hyperspace::inJumpRange(currentIdx, targetIdx)) {
    g.setTextColor(TFT_YELLOW, TFT_BLACK);
    g.print("IN RANGE");
  } else {
    g.setTextColor(TFT_RED, TFT_BLACK);
    g.print("OUT OF RANGE");
  }
}

inline void draw(M5Canvas& g, int currentIdx, int targetIdx, float phase) {
  g.fillSprite(TFT_BLACK);
  drawHeader(g);
  drawMap(g, currentIdx, targetIdx, phase);
  drawPanel(g, currentIdx, targetIdx);
  drawFooter(g, targetIdx);
}

} // namespace ChartScreen
