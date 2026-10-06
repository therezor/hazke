#pragma once
#include <M5Cardputer.h>
#include <M5GFX.h>
#include "Config.h"
#include "GameState.h"
#include "Radar.h"
#include "Rocket.h"

namespace Cockpit {

// Gauge geometry shared by both columns: 2-letter label, then an LED
// strip of Segs blocks. Rows sit RowGap apart from the top of the strip.
constexpr int LabelW = 14;
constexpr int Segs   = 10;
constexpr int SegW   = 3;    // lit block; 1 px gap after each
constexpr int BarH   = 5;
constexpr int BarW   = LabelW + Segs * (SegW + 1) - 1;
constexpr int RowGap = 9;
constexpr int Row0   = Config::HudY + 2;
constexpr int LeftX  = 4;
constexpr int RightX = Config::ScreenW - BarW - 4;
constexpr uint16_t cSegOff = 0x18E3;   // unlit block

inline bool blink(uint32_t periodMs = 250) {
  return ((millis() / periodMs) & 1u) == 0u;
}

inline void drawLabel(M5Canvas& g, int x, int y, const char* label, bool warn) {
  g.setTextSize(1);
  g.setTextColor(warn && blink() ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
  g.setCursor(x, y - 1);
  g.print(label);
}

// Segmented LED gauge. The block the value ends in lights at half
// brightness once it's a quarter full, so small changes (a laser hit on
// the shield) still show. `warn` blinks the label red.
inline void drawBar(M5Canvas& g, int x, int y, float val, uint16_t color,
                    const char* label, bool warn = false) {
  if (val < 0.0f) val = 0.0f;
  if (val > 1.0f) val = 1.0f;
  drawLabel(g, x, y, label, warn);
  float f = val * Segs;
  int full = (int)f;
  uint16_t half = (color >> 1) & 0x7BEF;
  for (int i = 0; i < Segs; i++) {
    uint16_t c = i < full                         ? color
               : (i == full && f - full >= 0.25f) ? half
               :                                    cSegOff;
    g.fillRect(x + LabelW + i * (SegW + 1), y, SegW, BarH, c);
  }
}

// Instrument bay around the scanner: the panel edge bends down into a
// recess whose walls lean in, as if the scope sits lower in the console.
inline void drawBay(M5Canvas& g) {
  const uint16_t cPanel = TFT_DARKGREY;
  const uint16_t cWall  = 0x2945;
  const int y0 = Config::HudY - 1;
  const int y1 = Config::ScreenH - 1;
  const int xl = Radar::BayX0 - 2, xr = Radar::BayX1 + 2;
  g.drawFastHLine(0, y0, xl - 2, cPanel);
  g.drawFastHLine(xr + 3, y0, Config::ScreenW - xr - 3, cPanel);
  g.drawLine(xl - 2, y0, xl, y0 + 2, cPanel);
  g.drawLine(xr + 2, y0, xr, y0 + 2, cPanel);
  g.drawFastHLine(xl + 1, y0 + 2, xr - xl - 1, cWall);
  g.drawLine(xl, y0 + 2, xl + 3, y1, cWall);
  g.drawLine(xr, y0 + 2, xr - 3, y1, cWall);
}

// Read the Cardputer's Li-Po voltage and convert via a piecewise-linear
// discharge curve. We don't rely on M5Unified's `getBatteryLevel()` —
// its built-in mapping under-reports on the Cardputer's single-cell pack.
// Returns -1 if the voltage probe isn't available (renders as "?%").
inline int batteryPercentFromMv(int32_t mV) {
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

// The footer draws every frame, but the battery only needs a look every
// couple of seconds — the ADC read isn't free, and averaging successive
// samples keeps the percentage from jittering between frames.
inline int readBatteryPercent() {
  static uint32_t lastMs = 0;
  static float    avg    = -1.0f;
  uint32_t now = millis();
  if (avg >= 0.0f && now - lastMs < 2000u) return (int)(avg + 0.5f);
  lastMs = now;
  int pct = batteryPercentFromMv(M5Cardputer.Power.getBatteryVoltage());
  if (pct < 0) { avg = -1.0f; return -1; }
  avg = (avg < 0.0f) ? (float)pct : avg * 0.7f + (float)pct * 0.3f;
  return (int)(avg + 0.5f);
}

inline void drawFooter(M5Canvas& g, const GameState& s) {
  const int y = Config::FooterY;
  // Divider under each gauge column; the radar bay runs on down between.
  g.drawFastHLine(0, y - 1, Radar::BayX0 - 1, TFT_DARKGREY);
  g.drawFastHLine(Radar::BayX1 + 2, y - 1,
                  Config::ScreenW - Radar::BayX1 - 2, TFT_DARKGREY);

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

  drawBay(g);

  // Left column: shields, hull, hull heat.
  uint16_t hullCol = s.hull > 0.66f ? TFT_GREEN
                   : s.hull > 0.33f ? TFT_YELLOW
                   :                  TFT_RED;
  drawBar(g, LeftX, Row0 + 0 * RowGap, s.shield, TFT_CYAN, "SH");
  drawBar(g, LeftX, Row0 + 1 * RowGap, s.hull,   hullCol,  "HU",
          s.hull < 0.25f);
  // Same bands as SystemFlight's sun heat: warn .25, burning .55.
  uint16_t heatCol = s.hullHeat >= 0.55f ? TFT_RED
                   : s.hullHeat >= 0.25f ? TFT_ORANGE
                   :                       0x8200;   // dull amber
  drawBar(g, LeftX, Row0 + 2 * RowGap, s.hullHeat, heatCol, "HT",
          s.hullHeat >= 0.55f);

  // Right column: throttle, missile rack, ECM.
  drawBar(g, RightX, Row0, s.speed, TFT_GREEN, "SP");

  // Missile rack — a little rocket per loaded missile (4-slot rack);
  // empty slots keep a dim silhouette so the rack reads at a glance.
  {
    int my = Row0 + RowGap;
    drawLabel(g, RightX, my, "MS", false);
    for (int i = 0; i < 4; i++) {
      Rocket::drawIcon(g, RightX + LabelW + i * 10, my, i < (int)s.missiles);
    }
  }

  // ECM: READY, seconds left on the recharge, or "--" when not fitted.
  {
    int ey = Row0 + 2 * RowGap;
    drawLabel(g, RightX, ey, "EC", false);
    g.setCursor(RightX + LabelW, ey - 1);
    int cd = (int)(s.ecmCooldown + 0.99f);
    if (!s.ecm) {
      g.setTextColor(TFT_DARKGREY, TFT_BLACK);
      g.print("--");
    } else if (cd > 0) {
      g.setTextColor(0x8400, TFT_BLACK);
      g.printf("%ds", cd);
    } else {
      g.setTextColor(TFT_YELLOW, TFT_BLACK);
      g.print("READY");
    }
  }

  Radar::drawScope(g);

  // Footer
  drawFooter(g, s);
}

} // namespace Cockpit
