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

// Map plot geometry. Star at plot center.
constexpr int PlotY0 = 14;
constexpr int PlotY1 = 100;       // exclusive
constexpr int PlotCX = Config::ScreenW / 2;
constexpr int PlotCY = (PlotY0 + PlotY1) / 2;
constexpr int PlotR  = 40;        // half-width of the plot area in pixels

// World→plot scale. The plot disk is the system's zone (ZoneRadius), so
// every POI sits inside it and the circle marks where deep space begins.
inline float plotScale() {
  return (float)PlotR / SolarSystem::ZoneRadius;
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

// World XZ → screen pixel. Anything beyond the zone is pinned to the
// zone circle along its true bearing, so far ships and a deep-space
// player still show which way they lie. Returns false if it was pinned.
inline bool worldToPlot(float wx, float wz, int& sx, int& sy) {
  float k = plotScale();
  float px = wx * k, pz = wz * k;
  float r = sqrtf(px * px + pz * pz);
  bool inside = (r <= (float)PlotR);
  if (!inside) { px *= (float)PlotR / r; pz *= (float)PlotR / r; }
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

inline void draw(M5Canvas& g, const GameState& /*gs*/) {
  MenuUI::clearBg(g);

  // Header: title left, system name right.
  MenuUI::drawHeader(g, "SYSTEM MAP",
                     Galaxy::systems[sysIdx].name,
                     MenuUI::TitleColor,
                     MenuUI::ValueColor,
                     MenuUI::SepColorWarm);

  // Plot frame.
  g.drawRect(PlotCX - PlotR - 2, PlotY0,
             (PlotR + 2) * 2, PlotY1 - PlotY0, MenuUI::SepColor);

  // Faint axis crosshairs through the star, and the zone boundary.
  g.drawFastHLine(PlotCX - PlotR, PlotCY,
                  PlotR * 2 + 1, 0x2104);
  g.drawFastVLine(PlotCX, PlotY0 + 2,
                  PlotY1 - PlotY0 - 4, 0x2104);
  g.drawCircle(PlotCX, PlotCY, PlotR, 0x2945);

  // Star sits at world origin, drawn here (not part of selectable items)
  // so the cursor can never land on it.
  for (int i = 0; i < SystemFlight::layout.numPOIs; i++) {
    const auto& p = SystemFlight::layout.poi[i];
    if (p.type != SolarSystem::POIType::Star) continue;
    int sx, sy;
    worldToPlot((float)p.x, (float)p.z, sx, sy);
    g.fillCircle(sx, sy, 2, TFT_YELLOW);
    break;
  }

  // Plot each item using its natural color and size — the cursor and
  // its bracket are the sole "this is the marker" signal so the dot
  // itself stays clean.
  for (int i = 0; i < numItems; i++) {
    const Item& it = items[i];
    int sx, sy;
    worldToPlot(it.wx, it.wz, sx, sy);

    if (it.type == SelType::POI) {
      g.fillRect(sx - 1, sy - 1, 3, 3, it.color);
    } else {
      g.drawPixel(sx, sy, it.color);
    }
  }

  // Cursor: animated bracket around the currently-selected item.
  if (numItems > 0) {
    const Item& sel = items[cursor];
    int sx, sy;
    worldToPlot(sel.wx, sel.wz, sx, sy);
    int r = 4;
    // Cursor bracket: cyan when hovering a fresh target, yellow when
    // the cursor is sitting on the currently-marked target so ENTER's
    // toggle behaviour reads at a glance.
    uint16_t cc = isMarked(sel) ? TFT_YELLOW : TFT_CYAN;
    g.drawLine(sx - r, sy - r, sx - r + 2, sy - r, cc);
    g.drawLine(sx - r, sy - r, sx - r,     sy - r + 2, cc);
    g.drawLine(sx + r, sy - r, sx + r - 2, sy - r, cc);
    g.drawLine(sx + r, sy - r, sx + r,     sy - r + 2, cc);
    g.drawLine(sx - r, sy + r, sx - r + 2, sy + r, cc);
    g.drawLine(sx - r, sy + r, sx - r,     sy + r - 2, cc);
    g.drawLine(sx + r, sy + r, sx + r - 2, sy + r, cc);
    g.drawLine(sx + r, sy + r, sx + r,     sy + r - 2, cc);
  }

  // Player position marker — a plus that blinks between bright white and
  // dim grey so it stands out from the static POI / NPC dots, with a
  // short tick along the ship's heading. In deep space it sits on the
  // zone circle in the direction the player is.
  {
    int sx, sy;
    worldToPlot(SystemFlight::state.px, SystemFlight::state.pz, sx, sy);
    bool bright = ((millis() / 400u) & 1u) == 0u;
    uint16_t col = bright ? TFT_WHITE : 0x39E7;  // dim grey
    float hx = SystemFlight::state.fx, hz = SystemFlight::state.fz;
    float hl = sqrtf(hx * hx + hz * hz);
    if (hl > 0.2f) {   // skip when pointing nearly straight up / down
      g.drawLine(sx, sy, sx + (int)(hx / hl * 6.0f), sy + (int)(hz / hl * 6.0f),
                 TFT_CYAN);
    }
    g.drawPixel(sx,     sy,     col);
    g.drawPixel(sx - 1, sy,     col);
    g.drawPixel(sx + 1, sy,     col);
    g.drawPixel(sx,     sy - 1, col);
    g.drawPixel(sx,     sy + 1, col);
  }

  // Info strip.
  const int infoY = 104;
  char nameBuf[24], typeBuf[16], distBuf[20], extraBuf[24];
  selectedLabel(nameBuf, sizeof(nameBuf));
  selectedTypeName(typeBuf, sizeof(typeBuf));
  float d = selectedDistance();
  if (d < 10000.0f) snprintf(distBuf, sizeof(distBuf), "%.1fK sysu",
                              (double)(d / 1000.0));
  else              snprintf(distBuf, sizeof(distBuf), "%dK sysu",
                              (int)(d / 1000.0f));

  extraBuf[0] = '\0';
  if (numItems > 0) {
    const Item& sel = items[cursor];
    if (sel.type == SelType::POI) {
      const auto& p = SystemFlight::layout.poi[sel.idx];
      if (p.type == SolarSystem::POIType::Planet) {
        snprintf(extraBuf, sizeof(extraBuf), "r=%u  flags=%02X",
                 (unsigned)p.radius, (unsigned)p.flags);
      } else if (p.type == SolarSystem::POIType::JumpGate) {
        snprintf(extraBuf, sizeof(extraBuf), "outbound gate");
      }
    } else {
      const auto& sh = NPCShip::ships[sel.idx];
      snprintf(extraBuf, sizeof(extraBuf),
               "shd %d%%  hull %d%%",
               (int)(sh.shields * 100.0f), (int)(sh.hull * 100.0f));
    }
  }

  g.setTextSize(1);
  g.setTextColor(MenuUI::TitleColor, TFT_BLACK);
  g.setCursor(4, infoY);
  g.print(nameBuf);
  g.setTextColor(MenuUI::SubColor, TFT_BLACK);
  int nameW = (int)strlen(nameBuf) * MenuUI::CharW;
  g.setCursor(4 + nameW + 6, infoY);
  g.print(typeBuf);
  g.setTextColor(MenuUI::ValueColor, TFT_BLACK);
  g.setCursor(4, infoY + 10);
  g.print(distBuf);
  if (extraBuf[0]) {
    g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    int ew = (int)strlen(extraBuf) * MenuUI::CharW;
    g.setCursor(Config::ScreenW - ew - 4, infoY + 10);
    g.print(extraBuf);
  }

  MenuUI::drawFooter(g, "ARROWS  ENTER=MARK  ESC=BACK");
}

} // namespace MapScreen
