#pragma once
#include <M5GFX.h>
#include <math.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "SolarSystem.h"
#include "GameState.h"
#include "Quest.h"

// Refit R13: planet surface "landed" screen.
//
// A Parkan-style still: planet horizon at the bottom, a couple of dome-
// interior struts framing the view, the planet's name as a header, and a
// menu of LOCAL MARKET / EQUIP / QUESTS / STATUS / LAUNCH. There is no
// walk-around — actions are picked from the menu and resolved by the
// outer state machine in hazke.ino.
//
// State (`sysIdx`, `planetPOIidx`) is set by `enter()` so the screen
// can keep painting after the SystemFlight layout is overwritten by
// market visits, etc.

namespace LandingScreen {

// Order matches the switch in hazke.ino's GameMode::Landed handler. With
// orbital stations gone, this menu is the only docked-services hub —
// market, equipment, quests and the commander card all hang
// off it. LAUNCH stays last so the player can always tab to it quickly.
// Repair (hull patch-up) lives inside the EQUIP shop catalog rather
// than on the landing menu so the shop owns all paid services.
enum : int {
  ItemMarket = 0,
  ItemEquip,
  ItemQuests,
  ItemStatus,
  ItemLaunch,
};
constexpr int N = 5;
inline const char* items[N] = {
  "LOCAL MARKET",
  "EQUIP",
  "QUESTS",
  "STATUS",
  "LAUNCH",
};

inline int   sysIdx       = 0;
inline int   planetPOIidx = -1;
inline int   selected     = 0;
inline float toast        = 0.0f;    // landing-menu feedback flash
inline char  toastMsg[24] = "";

inline void enter(int sys, int poi) {
  sysIdx       = sys;
  planetPOIidx = poi;
  selected     = 0;
  toast        = 0.0f;
  toastMsg[0]  = '\0';
}

inline void tick(float dt) {
  if (toast > 0.0f) {
    toast -= dt;
    if (toast < 0.0f) toast = 0.0f;
  }
  // Quest completion is now a modal popup — no per-frame fade.
}

inline void flashToast(const char* msg) {
  strncpy(toastMsg, msg, sizeof(toastMsg));
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toast = 1.4f;
}

inline void moveUp()   { selected = (selected - 1 + N) % N; }
inline void moveDown() { selected = (selected + 1) % N; }

// ---------- Per-planet visuals ----------

// Same palette + selection rule as SystemFlight::planetColor — duplicated
// here so LandingScreen doesn't have to pull in the whole flight header
// just for a color lookup. Stays in sync with the rendered planet body.
inline uint16_t planetPaletteColor(const SolarSystem::POI& p) {
  static const uint16_t pal[8] = {
    0xFD00, // amber
    0x07FF, // cyan
    0x5FE0, // pale green
    0xF81F, // magenta
    0xFFE0, // yellow
    0xFC10, // coral
    0x867F, // sky blue
    0xCE59, // tan
  };
  return pal[(uint8_t)(p.flags * 3u + p.subIdx * 5u) & 7u];
}

// Shade an RGB565 toward black by factor k ∈ [0..1].
inline uint16_t shade565(uint16_t c, float k) {
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  int r = (c >> 11) & 0x1F;
  int g = (c >> 5)  & 0x3F;
  int b = c         & 0x1F;
  r = (int)((float)r * k);
  g = (int)((float)g * k);
  b = (int)((float)b * k);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Derive sky / ground / horizon tints from the planet's actual rendered
// color, so a magenta planet has a magenta-tinted sky etc.
inline void planetPalette(const SolarSystem::POI& p,
                          uint16_t& sky, uint16_t& ground, uint16_t& horizon) {
  uint16_t base = planetPaletteColor(p);
  sky     = shade565(base, 0.22f);
  ground  = shade565(base, 0.40f);
  horizon = shade565(base, 0.70f);
}

// Sketch a horizon + low hills in the viewport's lower half.
inline void drawHorizon(M5Canvas& g, uint16_t sky, uint16_t ground, uint16_t hl) {
  const int top    = Config::ViewY;
  const int bottom = Config::ViewY + Config::ViewH;
  const int horizY = top + Config::ViewH * 2 / 3;

  // Sky and ground bands
  g.fillRect(Config::ViewX, top, Config::ViewW, horizY - top, sky);
  g.fillRect(Config::ViewX, horizY, Config::ViewW, bottom - horizY, ground);

  // Soft horizon line
  g.drawFastHLine(Config::ViewX, horizY, Config::ViewW, hl);

  // A few sine-bump hills, deterministic from planetPOIidx so repeats look
  // the same on subsequent visits.
  uint32_t seed = (uint32_t)(planetPOIidx * 2654435761u);
  for (int b = 0; b < 5; b++) {
    seed = seed * 1664525u + 1013904223u;
    int cx = Config::ViewX + (int)((seed >> 8) % Config::ViewW);
    int w  = 20 + (int)((seed >> 16) & 31);
    int h  = 4  + (int)((seed >> 20) & 7);
    for (int x = -w; x <= w; x++) {
      int px = cx + x;
      if (px < Config::ViewX || px >= Config::ViewX + Config::ViewW) continue;
      float t = (float)x / (float)w;
      int dy = (int)((1.0f - t * t) * h);
      int py = horizY - dy;
      if (py < top) py = top;
      g.drawFastVLine(px, py, bottom - py, ground);
    }
  }
  // Re-draw horizon line on top so hills meet it crisply.
  g.drawFastHLine(Config::ViewX, horizY, Config::ViewW, hl);
}

// Cockpit dome framing: angled struts at the four corners of the viewport
// suggesting a glass canopy you're looking through.
inline void drawDomeFrame(M5Canvas& g) {
  const int x0 = Config::ViewX;
  const int y0 = Config::ViewY;
  const int x1 = Config::ViewX + Config::ViewW - 1;
  const int y1 = Config::ViewY + Config::ViewH - 1;
  const uint16_t strut = TFT_DARKGREY;
  // Top-left
  g.drawLine(x0, y0, x0 + 20, y0 + 12, strut);
  g.drawLine(x0, y0, x0 + 12, y0 + 20, strut);
  // Top-right
  g.drawLine(x1, y0, x1 - 20, y0 + 12, strut);
  g.drawLine(x1, y0, x1 - 12, y0 + 20, strut);
  // Bottom-left
  g.drawLine(x0, y1, x0 + 24, y1 - 6, strut);
  // Bottom-right
  g.drawLine(x1, y1, x1 - 24, y1 - 6, strut);
  // Viewport rim
  g.drawRect(x0, y0, Config::ViewW, Config::ViewH, strut);
}

// ---------- Main draw ----------

inline void draw(M5Canvas& g, const GameState& s, float phase) {
  g.fillSprite(TFT_BLACK);

  // Backdrop — we need a planet POI to sample its palette. If the index
  // somehow drifted out of range, fall back to a plain dark frame.
  SolarSystem::Layout L;
  SolarSystem::layoutFor(sysIdx, L);
  if (planetPOIidx >= 0 && planetPOIidx < L.numPOIs &&
      L.poi[planetPOIidx].type == SolarSystem::POIType::Planet) {
    uint16_t sky, ground, hl;
    planetPalette(L.poi[planetPOIidx], sky, ground, hl);
    drawHorizon(g, sky, ground, hl);
  } else {
    g.fillRect(Config::ViewX, Config::ViewY, Config::ViewW, Config::ViewH, 0x0841);
  }
  drawDomeFrame(g);

  // Planet name header
  char nm[24];
  if (planetPOIidx >= 0 && planetPOIidx < L.numPOIs) {
    SolarSystem::displayName(sysIdx, L.poi[planetPOIidx], nm, sizeof(nm));
  } else {
    snprintf(nm, sizeof(nm), "%s", Galaxy::systems[sysIdx].name);
  }
  g.setTextSize(1);
  g.setTextColor(TFT_WHITE, TFT_BLACK);
  g.setCursor(4, 4);
  g.print("LANDED ");
  g.setTextColor(TFT_CYAN, TFT_BLACK);
  g.print(nm);

  // Credits top-right.
  char buf[24];
  snprintf(buf, sizeof(buf), "CR %d.%d", s.credits / 10, s.credits % 10);
  int w = (int)strlen(buf) * 6;
  g.setTextColor(TFT_GREEN, TFT_BLACK);
  g.setCursor(Config::ScreenW - w - 4, 4);
  g.print(buf);

  // Menu — centered. Five rows at 11 px pitch fit comfortably between
  // the planet header and the footer.
  const int firstY = 22;
  const int itemH  = 11;
  for (int i = 0; i < N; i++) {
    bool sel = (i == selected);
    const char* label = items[i];
    int len = (int)strlen(label);
    int totalW = (len + (sel ? 4 : 0)) * 6;
    int x = (Config::ScreenW - totalW) / 2;
    int y = firstY + i * itemH;
    g.setTextSize(1);
    if (sel) {
      float t = sinf(phase * 5.0f) * 0.5f + 0.5f;
      uint8_t lum = (uint8_t)(170 + 85 * t);
      uint16_t color = g.color565(lum, lum, lum);
      g.setTextColor(color, TFT_BLACK);
      g.setCursor(x, y);
      g.print("> ");
      g.print(label);
      g.print(" <");
    } else {
      g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      g.setCursor(x, y);
      g.print(label);
    }
  }

  // Footer hint
  g.drawFastHLine(0, Config::ScreenH - 12, Config::ScreenW, TFT_DARKGREY);
  g.setTextColor(TFT_DARKGREY, TFT_BLACK);
  const char* hint = "ARROWS  ENTER  ESC=LAUNCH";
  g.setCursor((Config::ScreenW - (int)strlen(hint) * 6) / 2,
              Config::ScreenH - 9);
  g.print(hint);

  // Toast (landing-menu feedback).
  if (toast > 0.0f) {
    float a = toast > 0.6f ? 1.0f : (toast / 0.6f);
    uint8_t lum = (uint8_t)(220 * a);
    uint16_t col = g.color565(lum, lum, 0);
    g.setTextColor(col, TFT_BLACK);
    int len = (int)strlen(toastMsg);
    g.setCursor((Config::ScreenW - len * 6) / 2, Config::ScreenH - 24);
    g.print(toastMsg);
  }

  // Modal completion popup — set by Quest::turnIn() when a quest
  // resolves on planetfall. Drawn last so it sits on top of the menu;
  // hazke.ino blocks Landed-mode input until ENTER/ESC dismisses it.
  if (Quest::completionPending) {
    const int boxW = 200;
    const int boxH = 60;
    const int boxX = (Config::ScreenW - boxW) / 2;
    const int boxY = (Config::ScreenH - boxH) / 2;
    uint16_t bg     = g.color565(0, 40, 0);
    uint16_t border = g.color565(80, 220, 80);
    g.fillRect(boxX, boxY, boxW, boxH, bg);
    g.drawRect(boxX, boxY, boxW, boxH, border);
    g.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, border);

    g.setTextSize(1);
    g.setTextColor(border, bg);
    const char* title = "QUEST COMPLETE";
    int tw = (int)strlen(title) * 6;
    g.setCursor(boxX + (boxW - tw) / 2, boxY + 10);
    g.print(title);

    g.setTextColor(g.color565(220, 255, 220), bg);
    int mw = (int)strlen(Quest::completionMsg) * 6;
    g.setCursor(boxX + (boxW - mw) / 2, boxY + 26);
    g.print(Quest::completionMsg);

    // Pulsing prompt so the player knows they have to act.
    uint8_t lum = (uint8_t)(140 + 100 *
                            (0.5f + 0.5f * sinf(phase * 4.0f)));
    g.setTextColor(g.color565(lum, lum, 0), bg);
    const char* prompt = "PRESS ENTER";
    int pw = (int)strlen(prompt) * 6;
    g.setCursor(boxX + (boxW - pw) / 2, boxY + 44);
    g.print(prompt);
  }
}

} // namespace LandingScreen
