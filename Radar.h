#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdint.h>
#include "Config.h"

// Elite-style 3D scanner, drawn as a real disk seen in perspective.
//
// The scope is a unit disk in the ship's own x-z plane (x right, z ahead)
// looked at by a tiny virtual camera tilted down onto it, so the far half
// comes out narrower and flatter than the near half and every ring, spoke
// and blip goes through the same projection. Contacts sit at the tip of a
// stalk rising (or dropping) from their spot on the plane, with a short
// tick at the foot as the shadow — the stalk is what makes it read as 3D.
//
// Range is logarithmic: combat happens inside ~4K while planets sit out
// to the 30K zone edge, and a linear scale piled every pirate and missile
// onto the center pixel.
//
// Usage per frame: drawScope() under the HUD, then clear() / add() for
// each contact / drawBlips() on top.

namespace Radar {

// ---- Geometry (screen px) ----
constexpr int CX    = Config::ScreenW / 2;   // own ship
constexpr int CY    = Config::HudY + 15;     // disk center row
constexpr int HalfW = 46;                    // disk half-width abeam

// Bay the scope may draw into: the gap between the gauge columns, from
// the HUD divider down through the (empty) middle of the footer, so
// stalks never bleed into the viewport or over the gauges.
constexpr int BayX0 = 64;
constexpr int BayX1 = Config::ScreenW - 65;
constexpr int BayY0 = Config::HudY;
constexpr int BayY1 = Config::ScreenH - 1;

// ---- Virtual camera ----
constexpr float Tilt  = 0.27f;   // rad below horizontal (~15°)
constexpr float Dist  = 3.2f;    // camera distance, in disk radii
const     float CosT  = cosf(Tilt);
const     float SinT  = sinf(Tilt);
const     float Focal = HalfW * Dist;

// ---- Range scale ----
constexpr float Range  = 30000.0f;  // sysu at the rim (= SolarSystem::ZoneRadius)
constexpr float Knee   = 1000.0f;   // log knee — linear-ish inside, compressed past
constexpr float AltMax = 0.22f;     // stalk soft limit, disk radii (~9 px)
const     float LogR   = logf(1.0f + Range / Knee);

// Ring radii (disk units) for the 2.5K and 10K marks.
const float Ring1 = logf(1.0f + 2500.0f  / Knee) / LogR;
const float Ring2 = logf(1.0f + 10000.0f / Knee) / LogR;

// Horizontal half-FOV of the viewport (ViewW/2 over FocalLen 110) — the
// wedge shows what's on screen right now.
const float FovHalf = atanf((Config::ViewW * 0.5f) / 110.0f);

// ---- Palette ----
constexpr uint16_t cDiskFar  = 0x00C0;  // plane fill, far half
constexpr uint16_t cDiskNear = 0x0120;  // near half catches more light
constexpr uint16_t cWedge    = 0x01A0;  // forward FOV wedge fill
constexpr uint16_t cEdge     = 0x0200;  // disk thickness under the near rim
constexpr uint16_t cGrid     = 0x0300;  // inner rings / spokes
constexpr uint16_t cRimFar = 0x0420;
constexpr uint16_t cRim    = 0x07E0;

// Disk-space point → screen.
inline void project(float x, float y, float z, int& sx, int& sy) {
  float depth = Dist + z * CosT - y * SinT;
  float k = Focal / depth;
  sx = CX + (int)lroundf(x * k);
  sy = CY - (int)lroundf((y * CosT + z * SinT) * k);
}

// ---- Precomputed outlines ----
constexpr int RimN   = 48;
constexpr int WedgeN = 10;
struct Pt { int16_t x, y; };
inline Pt   rim[RimN], ring1[RimN], ring2[RimN];
inline Pt   wedge[WedgeN + 1];
inline bool built = false;

inline void build() {
  if (built) return;
  for (int i = 0; i < RimN; i++) {
    // Angle from dead ahead, clockwise (toward +x).
    float a = i * 6.2831853f / RimN;
    float s = sinf(a), c = cosf(a);
    int x, y;
    project(s, 0, c, x, y);                 rim[i]   = { (int16_t)x, (int16_t)y };
    project(s * Ring1, 0, c * Ring1, x, y); ring1[i] = { (int16_t)x, (int16_t)y };
    project(s * Ring2, 0, c * Ring2, x, y); ring2[i] = { (int16_t)x, (int16_t)y };
  }
  for (int i = 0; i <= WedgeN; i++) {
    float a = -FovHalf + 2.0f * FovHalf * i / WedgeN;
    int x, y;
    project(sinf(a), 0, cosf(a), x, y);
    wedge[i] = { (int16_t)x, (int16_t)y };
  }
  built = true;
}

inline void polyline(M5Canvas& g, const Pt* p, int n, uint16_t col) {
  for (int i = 0; i < n; i++) {
    const Pt& a = p[i];
    const Pt& b = p[(i + 1) % n];
    g.drawLine(a.x, a.y, b.x, b.y, col);
  }
}

// Disk-space unit-vector spoke from the center out to radius 1.
inline void spoke(M5Canvas& g, float a, uint16_t col) {
  int x, y;
  project(sinf(a), 0, cosf(a), x, y);
  g.drawLine(CX, CY, x, y, col);
}

// The empty scope: filled plane, thickness lip, forward wedge, range
// rings, rim (bright near half, dim far half) and own-ship chevron.
inline void drawScope(M5Canvas& g) {
  build();
  g.setClipRect(BayX0, BayY0, BayX1 - BayX0 + 1, BayY1 - BayY0 + 1);

  // Lip: the near half of the rim pushed down 1–2 px reads as the edge
  // of a solid disk catching the light.
  for (int dy = 2; dy >= 1; dy--) {
    for (int i = RimN / 4; i < RimN * 3 / 4; i++) {
      const Pt& a = rim[i];
      const Pt& b = rim[i + 1];
      g.drawLine(a.x, a.y + dy, b.x, b.y + dy, dy == 2 ? cGrid : cEdge);
    }
  }

  for (int i = 0; i < RimN; i++) {
    const Pt& a = rim[i];
    const Pt& b = rim[(i + 1) % RimN];
    bool near = i >= RimN / 4 && i < RimN * 3 / 4;
    g.fillTriangle(CX, CY, a.x, a.y, b.x, b.y, near ? cDiskNear : cDiskFar);
  }
  for (int i = 0; i < WedgeN; i++) {
    g.fillTriangle(CX, CY, wedge[i].x, wedge[i].y,
                   wedge[i + 1].x, wedge[i + 1].y, cWedge);
  }

  polyline(g, ring1, RimN, cGrid);
  polyline(g, ring2, RimN, cGrid);
  spoke(g, -FovHalf, cGrid);
  spoke(g,  FovHalf, cGrid);
  spoke(g,  3.1415927f, cGrid);   // astern

  // Rim: far half dim, near half bright.
  for (int i = 0; i < RimN; i++) {
    const Pt& a = rim[i];
    const Pt& b = rim[(i + 1) % RimN];
    bool near = i >= RimN / 4 && i < RimN * 3 / 4;
    g.drawLine(a.x, a.y, b.x, b.y, near ? cRim : cRimFar);
  }

  // Own ship: tiny chevron pointing ahead.
  g.drawPixel(CX, CY - 1, cRim);
  g.drawFastHLine(CX - 1, CY, 3, cRim);
  g.drawPixel(CX - 2, CY + 1, cRim);
  g.drawPixel(CX + 2, CY + 1, cRim);

  g.clearClipRect();
}

// ---- Contacts ----
enum Kind : uint8_t { Body, Ship, Missile, Home };
enum : uint8_t { FLocked = 1, FMarked = 2 };

struct Blip {
  float    px, pz, alt;   // disk units
  uint16_t col;
  uint8_t  kind, flags;
};

constexpr int MaxBlips = 40;
inline Blip blips[MaxBlips];
inline int  numBlips = 0;

inline void clear() { numBlips = 0; }

// Add a contact by its camera-space offset (sysu; x right, y up, z ahead).
// The plane position is log-compressed; altitude uses the same local
// scale so the elevation angle survives the compression.
inline void add(float cx, float cy, float cz, uint16_t col,
                uint8_t kind, uint8_t flags = 0) {
  if (numBlips >= MaxBlips) return;
  float h = sqrtf(cx * cx + cz * cz);
  float r = logf(1.0f + h / Knee) / LogR;
  if (r > 1.0f) r = 1.0f;
  float s = (h > 1.0f) ? r / h : 1.0f / (Knee * LogR);
  Blip& b = blips[numBlips++];
  b.px  = cx * s;
  b.pz  = cz * s;
  // Soft limit: true scale for low stalks, easing toward AltMax so a
  // contact high overhead still out-ranks a merely high one.
  float a = cy * s;
  b.alt = AltMax * a / (AltMax + fabsf(a));
  b.col   = col;
  b.kind  = kind;
  b.flags = flags;
}

inline uint16_t dim565(uint16_t c) { return (c >> 1) & 0x7BEF; }

inline void drawBlips(M5Canvas& g) {
  // Far-to-near so a close contact paints over the one behind it.
  for (int i = 1; i < numBlips; i++) {
    Blip t = blips[i];
    int j = i - 1;
    while (j >= 0 && blips[j].pz < t.pz) { blips[j + 1] = blips[j]; j--; }
    blips[j + 1] = t;
  }

  g.setClipRect(BayX0, BayY0, BayX1 - BayX0 + 1, BayY1 - BayY0 + 1);
  bool blinkOn = ((millis() / 160u) & 1u) == 0u;
  for (int i = 0; i < numBlips; i++) {
    const Blip& b = blips[i];
    int fx, fy, hx, hy;
    project(b.px, 0.0f, b.pz, fx, fy);
    project(b.px, b.alt, b.pz, hx, hy);
    hx = fx;   // keep the stalk dead vertical
    uint16_t stalk = dim565(b.col);

    if (hy != fy) {
      g.drawFastHLine(fx - 1, fy, 3, stalk);   // shadow on the plane
      if (hy < fy) {
        g.drawFastVLine(fx, hy, fy - hy, stalk);
      } else {
        // Below the plane: dotted, like it's seen through the disk.
        for (int y = fy + 1; y < hy; y += 2) g.drawPixel(fx, y, stalk);
      }
    }

    // Near contacts (front half of the disk) get one size up.
    bool big = b.pz < 0.0f;
    switch (b.kind) {
      case Missile:
        if (blinkOn) g.fillRect(hx - 1, hy, 2, 1, b.col);
        break;
      case Ship:
        if (big) g.fillRect(hx - 1, hy - 1, 3, 3, b.col);
        else     g.fillRect(hx - 1, hy - 1, 2, 2, b.col);
        break;
      case Home:
        g.fillRect(hx - 1, hy - 1, 3, 3, b.col);
        if (blinkOn) g.drawRect(hx - 2, hy - 2, 5, 5, b.col);
        break;
      default:  // Body
        g.fillRect(hx - 1, hy - 1, 3, 3, b.col);
        break;
    }
    if (b.flags & FLocked) g.drawRect(hx - 3, hy - 3, 7, 7, TFT_WHITE);
    if (b.flags & FMarked) {
      g.drawPixel(hx - 3, hy, TFT_WHITE);
      g.drawPixel(hx + 3, hy, TFT_WHITE);
    }
  }
  g.clearClipRect();
}

} // namespace Radar
