#pragma once
#include <M5Cardputer.h>
#include <M5GFX.h>
#include "Config.h"
#include "GameState.h"

namespace Cockpit {

// Compact labeled bar: small label on the left, gauge on the right.
inline void drawBar(M5Canvas& g, int x, int y, int w, int h,
                    float val, uint16_t color, const char* label) {
  if (val < 0.0f) val = 0.0f;
  if (val > 1.0f) val = 1.0f;
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g.setTextSize(1);
  g.setCursor(x, y - 1);
  g.print(label);
  int gx = x + 14;
  int gw = w - 14;
  g.drawRect(gx, y, gw, h, TFT_DARKGREY);
  int fill = (int)(val * (gw - 2));
  if (fill > 0) g.fillRect(gx + 1, y + 1, fill, h - 2, color);
}

// Elite-style 3D scanner: an elliptical "scope plane" with concentric
// distance rings, suggesting a tilted disk floating in front of the pilot.
inline void drawRadar3D(M5Canvas& g, int cx, int cy, int rx, int ry) {
  const uint16_t cRim   = 0x07E0; // bright green
  const uint16_t cRing  = 0x0420; // mid green
  const uint16_t cCross = 0x0260; // dim green

  // Concentric rings (outer-to-inner)
  g.drawEllipse(cx, cy, rx,         ry,         cRim);
  g.drawEllipse(cx, cy, rx * 2 / 3, ry * 2 / 3, cRing);
  g.drawEllipse(cx, cy, rx / 3,     ry / 3,     cCross);

  // Crosshair lines across the scope plane
  g.drawFastHLine(cx - rx, cy, 2 * rx + 1, cCross);
  g.drawFastVLine(cx, cy - ry, 2 * ry + 1, cCross);

  // Cardinal tick marks (brighter so the rim reads as a real edge)
  g.drawPixel(cx, cy - ry - 1, TFT_GREEN);
  g.drawPixel(cx, cy + ry + 1, TFT_GREEN);
  g.drawPixel(cx - rx - 1, cy, TFT_GREEN);
  g.drawPixel(cx + rx + 1, cy, TFT_GREEN);

  // Center dot (own ship reference)
  g.fillRect(cx - 1, cy - 1, 3, 3, TFT_GREEN);
}

// Read the Cardputer's Li-Po voltage and convert via a piecewise-linear
// discharge curve. We don't rely on M5Unified's `getBatteryLevel()` —
// its built-in mapping under-reports on the Cardputer's single-cell pack.
// Returns -1 if the voltage probe isn't available (renders as "?%").
inline int readBatteryPercent() {
  int32_t mV = M5Cardputer.Power.getBatteryVoltage();
  if (mV <= 0) return -1;

  static const struct { int16_t mV; int8_t pct; } curve[] = {
    {4200, 100}, {4100, 90}, {4000, 75}, {3900, 60},
    {3800, 45},  {3700, 30}, {3600, 15}, {3500,  5}, {3300, 0},
  };
  constexpr int CN = sizeof(curve) / sizeof(curve[0]);

  if (mV >= curve[0].mV)       return curve[0].pct;
  if (mV <= curve[CN - 1].mV)  return curve[CN - 1].pct;
  for (int i = 0; i < CN - 1; i++) {
    if (mV <= curve[i].mV && mV >= curve[i + 1].mV) {
      int dV = curve[i].mV - curve[i + 1].mV;
      int dP = curve[i].pct - curve[i + 1].pct;
      return curve[i + 1].pct + (mV - curve[i + 1].mV) * dP / dV;
    }
  }
  return 0;
}

inline void drawFooter(M5Canvas& g, const GameState& s) {
  const int y = Config::FooterY;
  g.drawFastHLine(0, y - 1, Config::ScreenW, TFT_DARKGREY);

  // Credits, left-aligned (Elite-style decicredits)
  g.setTextSize(1);
  g.setTextColor(TFT_YELLOW, TFT_BLACK);
  g.setCursor(2, y + 1);
  g.printf("CR %4d.%d", s.credits / 10, s.credits % 10);

  // Battery, right-aligned. Color tints toward red as it drops.
  int bat = readBatteryPercent();
  uint16_t col = bat < 0          ? TFT_DARKGREY
               : bat > 50          ? TFT_GREEN
               : bat > 20          ? TFT_YELLOW : TFT_RED;
  char buf[12];
  if (bat < 0) snprintf(buf, sizeof(buf), "BAT   ?%%");
  else         snprintf(buf, sizeof(buf), "BAT %3d%%", bat);
  int len = (int)strlen(buf);
  g.setTextColor(col, TFT_BLACK);
  g.setCursor(Config::ScreenW - len * 6 - 2, y + 1);
  g.print(buf);
}

inline void draw(M5Canvas& g, const GameState& s) {
  // Viewport frame
  g.drawRect(Config::ViewX, Config::ViewY,
             Config::ViewW, Config::ViewH, TFT_DARKGREY);

  // Crosshair (gun reticle)
  int cx = Config::ViewX + Config::ViewW / 2;
  int cy = Config::ViewY + Config::ViewH / 2;
  g.drawFastHLine(cx - 6, cy, 5, TFT_DARKGREEN);
  g.drawFastHLine(cx + 2, cy, 5, TFT_DARKGREEN);
  g.drawFastVLine(cx, cy - 6, 5, TFT_DARKGREEN);
  g.drawFastVLine(cx, cy + 2, 5, TFT_DARKGREEN);

  // HUD divider
  g.drawFastHLine(0, Config::HudY - 1, Config::ScreenW, TFT_DARKGREY);

  // Bar layout — 3 bars left, 2 bars right, radar centered.
  const int barH = 5;
  const int gap  = 9;
  const int barW = 58;

  // Left column: SH (shield) and HU (hull). One shield only now.
  const int lx = 4;
  const int ly = Config::HudY + 2;
  uint16_t hullCol = s.hull > 0.66f ? TFT_GREEN
                   : s.hull > 0.33f ? TFT_YELLOW
                   :                  TFT_RED;
  drawBar(g, lx, ly + 0 * gap, barW, barH, s.shield, TFT_CYAN, "SH");
  drawBar(g, lx, ly + 1 * gap, barW, barH, s.hull,   hullCol,  "HU");

  // Right column: SP bar + missile counter row, vertically aligned to
  // mirror the two-bar left column.
  const int rx = Config::ScreenW - barW - 4;
  const int ry = Config::HudY + 2;
  drawBar(g, rx, ry, barW, barH, s.speed, TFT_GREEN, "SP");

  // Missile rack directly under SP — one small dart icon per loaded
  // missile (4-slot rack). Lit yellow when loaded, dim outline when the
  // slot is empty, so the rack reads at a glance.
  {
    int my = ry + gap;
    g.setTextSize(1);
    g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    g.setCursor(rx, my - 1);
    g.print("MS");
    // Icon: 5×5 right-pointing dart. Base on the left, tip on the right.
    const int iconW = 5, iconH = 5, gapX = 3;
    int sx = rx + 14;
    int sy = my;
    for (int i = 0; i < 4; i++) {
      int ix = sx + i * (iconW + gapX);
      if (i < (int)s.missiles) {
        g.fillTriangle(ix,            sy,
                       ix,            sy + iconH - 1,
                       ix + iconW - 1, sy + iconH / 2,
                       TFT_YELLOW);
      } else {
        g.drawTriangle(ix,            sy,
                       ix,            sy + iconH - 1,
                       ix + iconW - 1, sy + iconH / 2,
                       TFT_DARKGREY);
      }
    }
  }

  // 3D scanner in the middle
  drawRadar3D(g, Config::ScreenW / 2, Config::HudY + 14, 26, 11);

  // Footer
  drawFooter(g, s);
}

} // namespace Cockpit
