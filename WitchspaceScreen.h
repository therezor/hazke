#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "Starfield.h"

// Day 5: hyperspace transition screen.
//
// A 3-second cinematic shown when the player jumps. The existing
// starfield is driven at max throttle plus a swirl rate so the stars
// streak past with a vortex feel; a few expanding rings pulse out from
// center, and the system names fade between FROM and TO.

namespace WitchspaceScreen {

constexpr float Duration = 3.0f;

inline void update(Starfield& stars, float phase, float dt) {
  // Roll/yaw swirl that ramps up in the middle, eases at the ends.
  float t = phase / Duration;
  if (t < 0) t = 0; if (t > 1) t = 1;
  float intensity = sinf(t * (float)M_PI);   // 0 -> 1 -> 0

  float roll = 2.4f * intensity;
  float yaw  = 1.2f * intensity;
  float thr  = 1.0f;

  stars.update(thr, 0.0f, yaw, roll, dt);
}

inline void draw(M5Canvas& g, Starfield& stars,
                 int fromIdx, int toIdx, float phase) {
  g.fillSprite(TFT_BLACK);
  stars.draw(g);

  const int cx = Config::ScreenW / 2;
  const int cy = Config::ScreenH / 2;

  // Expanding rings — four staggered pulses
  for (int i = 0; i < 4; i++) {
    float t = fmodf(phase * 0.55f + i * 0.25f, 1.0f);
    int r = 4 + (int)(t * 95);
    uint8_t fade = (uint8_t)((1.0f - t) * 220);
    uint16_t col = g.color565(fade / 3, fade / 4, fade);
    g.drawCircle(cx, cy, r, col);
  }

  // Endpoint names — top-left "FROM xxx", top-right "TO xxx"
  g.setTextSize(1);
  g.setTextColor(TFT_DARKCYAN, TFT_BLACK);
  g.setCursor(4, 4);
  g.print("FROM ");
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g.print(Galaxy::systems[fromIdx].name);

  const char* toLbl = Galaxy::systems[toIdx].name;
  char buf[24];
  snprintf(buf, sizeof(buf), "TO %s", toLbl);
  int w = (int)strlen(buf) * 6;
  g.setCursor(Config::ScreenW - w - 4, 4);
  g.setTextColor(TFT_DARKCYAN, TFT_BLACK);
  g.print("TO ");
  g.setTextColor(TFT_YELLOW, TFT_BLACK);
  g.print(toLbl);

  // Centered title, pulses with the rings
  float pulse = sinf(phase * 4.0f) * 0.5f + 0.5f;
  uint8_t lum = (uint8_t)(150 + 105 * pulse);
  uint16_t titleCol = g.color565(lum, 0, lum);
  g.setTextSize(2);
  g.setTextColor(titleCol, TFT_BLACK);
  const char* title = "HYPERSPACE";
  int tw = (int)strlen(title) * 12;
  g.setCursor((Config::ScreenW - tw) / 2, cy - 8);
  g.print(title);

  // Progress bar across the bottom
  float pct = phase / Duration;
  if (pct < 0) pct = 0; if (pct > 1) pct = 1;
  int bw = (int)(pct * (Config::ScreenW - 20));
  g.drawRect(10, Config::ScreenH - 10, Config::ScreenW - 20, 5, TFT_DARKGREY);
  if (bw > 0) g.fillRect(11, Config::ScreenH - 9, bw, 3, TFT_CYAN);
}

} // namespace WitchspaceScreen
