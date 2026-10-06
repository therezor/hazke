#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "Galaxy.h"
#include "GameState.h"
#include "SolarSystem.h"
#include "NPCShip.h"
#include "SystemFlight.h"
#include "MenuUI.h"

// Local-system map. Shows every POI plus every active NPC ship as a
// top-down (XZ) plot, with a selection cursor that walks through the
// list. Pressing ENTER on an entry promotes it to the in-flight marker
// — POI → SystemFlight::state.targetIdx, NPC → state.lockedNPC — so the
// cockpit picks the same target up the moment the player resumes.
// F toggles a x2 zoom centred on the cursor (x1 frames the whole system).

namespace MapScreen {

enum class SelType : uint8_t { POI, NPC };

struct Item {
  SelType  type;
  int      idx;        // POI index or NPC slot
  float    wx, wz;     // cached world XZ for plotting (NPCs can roam far
                       // past int16 range now that space is open)
  uint16_t color;
};

// Cap: 12 POIs + 8 NPC slots.
constexpr int MaxItems = SolarSystem::MaxPOIs + NPCShip::MaxNPCs;

inline Item items[MaxItems];
inline int  numItems = 0;
inline int  cursor   = 0;
inline int  sysIdx   = -1;

// Map plot geometry. The plot is a square-ish box on the left using the
// full content height; the info panel sits to its right.
constexpr int PlotY0 = 13;
constexpr int PlotY1 = 122;       // exclusive
constexpr int PlotX0 = 2;
constexpr int PlotX1 = 113;       // exclusive
constexpr int PlotCX = (PlotX0 + PlotX1) / 2;
constexpr int PlotCY = (PlotY0 + PlotY1) / 2;
constexpr int PlotR  = 52;        // view radius in pixels (short half-side)
// Anything past the box edge is pinned this far inside it.
constexpr int PinHalfW = (PlotX1 - PlotX0) / 2 - 3;
constexpr int PinHalfH = (PlotY1 - PlotY0) / 2 - 3;
constexpr int PanelX = 118;

// Zoom toggle. x1 frames the whole system on the star; zoomed centres on
// the cursor item so arrowing through the list pans the view.
constexpr int ZoomMul = 2;
inline bool  zoomed = false;
inline float fitR = SolarSystem::ZoneRadius;   // world radius shown at x1

inline int zoomMul() { return zoomed ? ZoomMul : 1; }

inline void toggleZoom() { zoomed = !zoomed; }

// World→plot scale (px per sysu) at the current zoom.
inline float plotScale() {
  return (float)PlotR * (float)zoomMul() / fitR;
}

// x1 fits the farthest POI (plus a margin) instead of the whole zone —
// the zone is ~1/3 empty space, which only shrank every dot.
inline void fitToLayout() {
  float far2 = 0.0f;
  for (int i = 0; i < SystemFlight::layout.numPOIs; i++) {
    const auto& p = SystemFlight::layout.poi[i];
    float d2 = (float)p.x * (float)p.x + (float)p.z * (float)p.z;
    if (d2 > far2) far2 = d2;
  }
  fitR = sqrtf(far2) * 1.1f;
  if (fitR < 5000.0f) fitR = 5000.0f;
  if (fitR > SolarSystem::ZoneRadius) fitR = SolarSystem::ZoneRadius;
}

inline uint16_t poiColor(const SolarSystem::POI& p) {
  switch (p.type) {
    case SolarSystem::POIType::Star:     return TFT_YELLOW;
    case SolarSystem::POIType::Planet:   return SystemFlight::planetColor(p);
    case SolarSystem::POIType::JumpGate: return TFT_MAGENTA;
    default:                             return TFT_LIGHTGREY;
  }
}

inline void rebuildItems() {
  numItems = 0;
  for (int i = 0; i < SystemFlight::layout.numPOIs && numItems < MaxItems; i++) {
    const auto& p = SystemFlight::layout.poi[i];
    if (p.type == SolarSystem::POIType::Station) continue;   // stations removed
    if (p.type == SolarSystem::POIType::Star) continue;      // star drawn separately, not selectable
    items[numItems++] = { SelType::POI, i, (float)p.x, (float)p.z, poiColor(p) };
  }
  for (int i = 0; i < NPCShip::MaxNPCs && numItems < MaxItems; i++) {
    const auto& sh = NPCShip::ships[i];
    if (!sh.active) continue;
    items[numItems++] = { SelType::NPC, i, sh.wx, sh.wz, sh.color };
  }
  if (cursor >= numItems) cursor = numItems > 0 ? numItems - 1 : 0;
  if (cursor < 0)         cursor = 0;
}

// Returns true if this item is currently the in-flight marker.
inline bool isMarked(const Item& it) {
  if (it.type == SelType::POI)
    return SystemFlight::state.targetIdx == it.idx;
  return SystemFlight::state.lockedNPC == it.idx;
}

inline void enter(int sys) {
  sysIdx = sys;
  fitToLayout();
  rebuildItems();
  // Restore the cursor to whatever target is currently the in-flight
  // marker so reopening the map drops the player back on their last
  // pick. Falls back to whatever the cursor was last frame.
  for (int i = 0; i < numItems; i++) {
    if (isMarked(items[i])) { cursor = i; return; }
  }
  if (cursor >= numItems) cursor = 0;
  if (cursor < 0)         cursor = 0;
}

inline void tick(float /*dt*/) {}

inline void moveSelection(int delta) {
  if (numItems <= 0) return;
  cursor = (cursor + delta + numItems) % numItems;
}

inline void markSelected() {
  if (numItems <= 0) return;
  const Item& sel = items[cursor];
  // Re-pressing ENTER on the active marker deselects it.
  if (isMarked(sel)) {
    SystemFlight::state.targetIdx = -1;
    SystemFlight::state.lockedNPC = -1;
    return;
  }
  // Marker is exclusive. Clear both first so the cockpit only shows
  // one bracket / arrow.
  SystemFlight::state.targetIdx = -1;
  SystemFlight::state.lockedNPC = -1;
  if (sel.type == SelType::POI) {
    SystemFlight::state.targetIdx = sel.idx;
  } else {
    SystemFlight::state.lockedNPC = sel.idx;
  }
}

// Plot centre in world XZ: the star at x1, the cursor item when zoomed.
inline void viewCenter(float& cx, float& cz) {
  cx = 0.0f; cz = 0.0f;
  if (!zoomed || numItems <= 0) return;
  cx = items[cursor].wx;
  cz = items[cursor].wz;
}

// World XZ → screen pixel. At x1, anything outside the plot box is pinned
// just inside its edge along its true bearing, so far ships and a
// deep-space player still show which way they lie. Zoomed, nothing is
// pinned: off-view objects land outside the box and the plot clip hides
// them. Returns false if the point is outside the box.
inline bool worldToPlot(float wx, float wz, int& sx, int& sy) {
  float k = plotScale();
  float cx, cz;
  viewCenter(cx, cz);
  float px = (wx - cx) * k, pz = (wz - cz) * k;
  float ax = fabsf(px), az = fabsf(pz);
  bool inside = (ax <= (float)PinHalfW && az <= (float)PinHalfH);
  if (!inside && !zoomed) {
    float sxk = (ax > (float)PinHalfW) ? (float)PinHalfW / ax : 1.0f;
    float szk = (az > (float)PinHalfH) ? (float)PinHalfH / az : 1.0f;
    float m = sxk < szk ? sxk : szk;
    px *= m; pz *= m;
  }
  sx = PlotCX + (int)px;
  sy = PlotCY + (int)pz;
  return inside;
}

inline const char* npcRoleName(NPCShip::Role r) {
  switch (r) {
    case NPCShip::Role::Trader: return "TRADER";
    case NPCShip::Role::Pirate: return "PIRATE";
    case NPCShip::Role::Patrol: return "PATROL";
  }
  return "SHIP";
}

inline const char* poiTypeName(SolarSystem::POIType t) {
  switch (t) {
    case SolarSystem::POIType::Star:     return "STAR";
    case SolarSystem::POIType::Planet:   return "PLANET";
    case SolarSystem::POIType::JumpGate: return "JUMP GATE";
    case SolarSystem::POIType::Station:  return "STATION";
  }
  return "?";
}

// Build a one-line label for the selected item ("MAIA II", "TRADER", …).
inline void selectedLabel(char* out, size_t cap) {
  if (numItems <= 0) { snprintf(out, cap, "—"); return; }
  const Item& sel = items[cursor];
  if (sel.type == SelType::POI) {
    SolarSystem::displayName(sysIdx,
                             SystemFlight::layout.poi[sel.idx], out, cap);
  } else {
    const auto& sh = NPCShip::ships[sel.idx];
    snprintf(out, cap, "%s #%d", npcRoleName(sh.role), sel.idx);
  }
}

inline void selectedTypeName(char* out, size_t cap) {
  if (numItems <= 0) { snprintf(out, cap, "—"); return; }
  const Item& sel = items[cursor];
  if (sel.type == SelType::POI) {
    const auto& p = SystemFlight::layout.poi[sel.idx];
    snprintf(out, cap, "%s", poiTypeName(p.type));
  } else {
    snprintf(out, cap, "%s",
             npcRoleName(NPCShip::ships[sel.idx].role));
  }
}

inline float selectedDistance() {
  if (numItems <= 0) return 0.0f;
  const Item& sel = items[cursor];
  float wx, wy, wz;
  if (sel.type == SelType::POI) {
    const auto& p = SystemFlight::layout.poi[sel.idx];
    wx = (float)p.x; wy = (float)p.y; wz = (float)p.z;
  } else {
    const auto& sh = NPCShip::ships[sel.idx];
    wx = sh.wx; wy = sh.wy; wz = sh.wz;
  }
  float dx = wx - SystemFlight::state.px;
  float dy = wy - SystemFlight::state.py;
  float dz = wz - SystemFlight::state.pz;
  return sqrtf(dx*dx + dy*dy + dz*dz);
}

// On-plot glyph radius for a body: its true scaled size (so the sun and
// planets grow with the zoom), never smaller than `minR`. The plot clip
// keeps anything big inside the box; the cap only bounds the fill cost.
inline int bodyGlyphR(float worldR, int minR) {
  int r = (int)(worldR * plotScale() + 0.5f);
  if (r < minR)  r = minR;
  if (r > PlotR) r = PlotR;
  return r;
}

inline void draw(M5Canvas& g, const GameState& /*gs*/) {
  MenuUI::clearBg(g);

  // Header: title left, system name right.
  MenuUI::drawHeader(g, "SYSTEM MAP",
                     Galaxy::systems[sysIdx].name,
                     MenuUI::TitleColor,
                     MenuUI::ValueColor,
                     MenuUI::SepColorWarm);

  // Plot frame. Everything inside is clipped to it so zoomed bodies and
  // the zone ring can't spill into the info panel.
  g.drawRect(PlotX0, PlotY0, PlotX1 - PlotX0, PlotY1 - PlotY0, MenuUI::SepColor);
  g.setClipRect(PlotX0 + 1, PlotY0 + 1, PlotX1 - PlotX0 - 2, PlotY1 - PlotY0 - 2);

  // Faint axis crosshairs through the star, and the zone boundary (deep
  // space starts past it) — only the arcs that land inside the box show.
  {
    float k = plotScale();
    float cx, cz;
    viewCenter(cx, cz);
    int ox = PlotCX + (int)(-cx * k);
    int oz = PlotCY + (int)(-cz * k);
    g.drawFastHLine(PlotX0, oz, PlotX1 - PlotX0, 0x2104);
    g.drawFastVLine(ox, PlotY0, PlotY1 - PlotY0, 0x2104);
    int zr = (int)(SolarSystem::ZoneRadius * k);
    if (zr < 2000) g.drawCircle(ox, oz, zr, 0x2945);
  }

  // Star sits at world origin, drawn here (not part of selectable items)
  // so the cursor can never land on it.
  for (int i = 0; i < SystemFlight::layout.numPOIs; i++) {
    const auto& p = SystemFlight::layout.poi[i];
    if (p.type != SolarSystem::POIType::Star) continue;
    int sx, sy;
    worldToPlot((float)p.x, (float)p.z, sx, sy);
    g.fillCircle(sx, sy,
                 bodyGlyphR((float)p.radius * SystemFlight::StarVisualScale, 3),
                 TFT_YELLOW);
    break;
  }

  // Plot each item using its natural color — the cursor and its bracket
  // are the sole "this is the marker" signal so the glyph stays clean.
  for (int i = 0; i < numItems; i++) {
    const Item& it = items[i];
    int sx, sy;
    worldToPlot(it.wx, it.wz, sx, sy);

    if (it.type == SelType::POI) {
      const auto& p = SystemFlight::layout.poi[it.idx];
      if (p.type == SolarSystem::POIType::JumpGate) {
        // The ring is far below a pixel at true scale — keep the marker
        // and grow it with the zoom like the bodies around it.
        g.drawCircle(sx, sy, 3 * zoomMul(), it.color);
        g.drawPixel(sx, sy, TFT_WHITE);
      } else {
        g.fillCircle(sx, sy, bodyGlyphR((float)p.radius, 2), it.color);
      }
    } else {
      // Green friendly / red hostile — same rule as the flight markers.
      g.fillRect(sx - 1, sy - 1, 3, 3, SystemFlight::shipMarkerColor(it.idx));
    }
  }

  // Cursor: bracket around the currently-selected item.
  if (numItems > 0) {
    const Item& sel = items[cursor];
    int sx, sy;
    worldToPlot(sel.wx, sel.wz, sx, sy);
    int r = 6;
    if (sel.type == SelType::POI &&
        SystemFlight::layout.poi[sel.idx].type == SolarSystem::POIType::Planet) {
      r = bodyGlyphR((float)SystemFlight::layout.poi[sel.idx].radius, 2) + 3;
      if (r < 6) r = 6;
    }
    // Cursor bracket: cyan when hovering a fresh target, yellow when
    // the cursor is sitting on the currently-marked target so ENTER's
    // toggle behaviour reads at a glance.
    uint16_t cc = isMarked(sel) ? TFT_YELLOW : TFT_CYAN;
    SystemFlight::drawBracket(g, sx, sy, r, cc);
  }

  // Player position marker — a plus that blinks between bright white and
  // dim grey so it stands out from the static POI / NPC dots, with a
  // tick along the ship's heading. Off-plot it sits on the box edge in
  // the direction the player is.
  {
    int sx, sy;
    worldToPlot(SystemFlight::state.px, SystemFlight::state.pz, sx, sy);
    bool bright = ((millis() / 400u) & 1u) == 0u;
    uint16_t col = bright ? TFT_WHITE : 0x39E7;  // dim grey
    float hx = SystemFlight::state.fx, hz = SystemFlight::state.fz;
    float hl = sqrtf(hx * hx + hz * hz);
    if (hl > 0.2f) {   // skip when pointing nearly straight up / down
      g.drawLine(sx, sy, sx + (int)(hx / hl * 8.0f), sy + (int)(hz / hl * 8.0f),
                 TFT_CYAN);
    }
    g.drawFastHLine(sx - 2, sy, 5, col);
    g.drawFastVLine(sx, sy - 2, 5, col);
  }

  g.clearClipRect();

  // Info panel, one fact per line.
  char nameBuf[20], typeBuf[16], line[24];
  selectedLabel(nameBuf, sizeof(nameBuf));
  selectedTypeName(typeBuf, sizeof(typeBuf));
  g.setTextSize(1);
  int y = 16;
  auto row = [&](const char* txt, uint16_t col) {
    g.setTextColor(col, TFT_BLACK);
    g.setCursor(PanelX, y);
    g.print(txt);
    y += 10;
  };
  row(nameBuf, MenuUI::TitleColor);
  row(typeBuf, MenuUI::SubColor);

  if (numItems > 0) {
    float d = selectedDistance();
    if (d < 10000.0f) snprintf(line, sizeof(line), "DIST %.1fK", (double)(d / 1000.0f));
    else              snprintf(line, sizeof(line), "DIST %dK", (int)(d / 1000.0f));
    row(line, MenuUI::ValueColor);

    const Item& sel = items[cursor];
    if (sel.type == SelType::POI) {
      const auto& p = SystemFlight::layout.poi[sel.idx];
      if (p.type == SolarSystem::POIType::Planet) {
        snprintf(line, sizeof(line), "SIZE %.1fK", (double)(p.radius / 1000.0f));
        row(line, TFT_LIGHTGREY);
        line[0] = '\0';
        int pos = 0;
        if (p.flags & SolarSystem::PoiFlagRing)
          pos += snprintf(line + pos, sizeof(line) - pos, "RING ");
        if (p.flags & SolarSystem::PoiFlagBelt)
          pos += snprintf(line + pos, sizeof(line) - pos, "BELT ");
        int moons = SolarSystem::moonletCount(p);
        if (moons > 0)
          snprintf(line + pos, sizeof(line) - pos, "M%d", moons);
        if (line[0]) row(line, TFT_LIGHTGREY);
      } else if (p.type == SolarSystem::POIType::JumpGate) {
        row("OUTBOUND GATE", TFT_LIGHTGREY);
      }
    } else {
      const auto& sh = NPCShip::ships[sel.idx];
      snprintf(line, sizeof(line), "SHIELD %d%%", (int)(sh.shields * 100.0f));
      row(line, TFT_CYAN);
      snprintf(line, sizeof(line), "HULL   %d%%", (int)(sh.hull * 100.0f));
      row(line, sh.hull > 0.66f ? TFT_GREEN : sh.hull > 0.33f ? TFT_YELLOW : TFT_RED);
      if (SystemFlight::shipHostile(sel.idx)) row("HOSTILE", TFT_RED);
      else                                    row("FRIENDLY", TFT_GREEN);
    }
    if (isMarked(sel)) row("MARKED", TFT_YELLOW);
  }

  // Bottom of the panel: zoom level and list position.
  snprintf(line, sizeof(line), "ZOOM x%d  %d/%d", zoomMul(),
           numItems > 0 ? cursor + 1 : 0, numItems);
  g.setTextColor(MenuUI::HintColor, TFT_BLACK);
  g.setCursor(PanelX, PlotY1 - 9);
  g.print(line);

  MenuUI::drawFooter(g, "ARROWS ENTER=MARK F=ZOOM ESC=BACK");
}

} // namespace MapScreen
