#pragma once
#include <M5GFX.h>
#include <math.h>
#include "Config.h"

// 3D wireframe ship rendering. All vertex/edge data is original to this project.

namespace Ship3D {

struct Vec3 { float x, y, z; };
struct Edge { uint8_t a, b; };

struct Model {
  const Vec3* verts;
  int nverts;
  const Edge* edges;
  int nedges;
};

// Wedge hull: flat triangular body, side wing tips, raised cockpit ridge,
// twin engine tabs at the back.
static const Vec3 wedge_verts[] = {
  { 0.00f,  0.00f,  1.10f}, // 0  nose
  { 0.60f,  0.00f, -0.20f}, // 1  right wing tip
  {-0.60f,  0.00f, -0.20f}, // 2  left wing tip
  { 0.35f,  0.00f, -0.70f}, // 3  right rear bevel
  {-0.35f,  0.00f, -0.70f}, // 4  left rear bevel
  { 0.12f,  0.00f, -0.78f}, // 5  right engine base
  {-0.12f,  0.00f, -0.78f}, // 6  left engine base
  { 0.00f,  0.18f, -0.05f}, // 7  cockpit peak
  { 0.00f,  0.10f,  0.45f}, // 8  forward cockpit ridge
  { 0.00f,  0.14f, -0.55f}, // 9  rear cockpit ridge
  { 0.00f, -0.08f, -0.40f}, // 10 belly keel
};

static const Edge wedge_edges[] = {
  // Top outline
  {0, 1}, {0, 2},
  {1, 3}, {2, 4},
  {3, 5}, {4, 6}, {5, 6},
  // Cockpit ridge (top spine)
  {0, 8}, {8, 7}, {7, 9}, {9, 5}, {9, 6},
  // Wing roots to cockpit
  {7, 1}, {7, 2},
  // Belly keel
  {0, 10}, {10, 3}, {10, 4},
};

static const Model wedge = {
  wedge_verts,
  sizeof(wedge_verts) / sizeof(wedge_verts[0]),
  wedge_edges,
  sizeof(wedge_edges) / sizeof(wedge_edges[0]),
};

// R25: heavy freighter — long boxed hull with a tapered nose. Reads big
// and slow even at small radar scale because of the parallel side rails.
static const Vec3 freighter_verts[] = {
  // Nose taper
  { 0.00f,  0.10f,  1.30f}, // 0  nose top
  { 0.00f, -0.10f,  1.30f}, // 1  nose bottom
  // Forward cargo face
  { 0.50f,  0.35f,  0.50f}, // 2  fwd TR
  {-0.50f,  0.35f,  0.50f}, // 3  fwd TL
  { 0.50f, -0.30f,  0.50f}, // 4  fwd BR
  {-0.50f, -0.30f,  0.50f}, // 5  fwd BL
  // Aft cargo face
  { 0.50f,  0.35f, -0.80f}, // 6  aft TR
  {-0.50f,  0.35f, -0.80f}, // 7  aft TL
  { 0.50f, -0.30f, -0.80f}, // 8  aft BR
  {-0.50f, -0.30f, -0.80f}, // 9  aft BL
  // Thruster nub
  { 0.00f,  0.00f, -1.00f}, // 10 thrust
};
static const Edge freighter_edges[] = {
  // Nose to forward face
  {0, 2}, {0, 3}, {1, 4}, {1, 5}, {0, 1},
  // Forward face
  {2, 3}, {2, 4}, {3, 5}, {4, 5},
  // Side rails (long axis — reads as a cargo hauler)
  {2, 6}, {3, 7}, {4, 8}, {5, 9},
  // Aft face
  {6, 7}, {6, 8}, {7, 9}, {8, 9},
  // Thruster
  {6, 10}, {7, 10}, {8, 10}, {9, 10},
};
static const Model freighter = {
  freighter_verts,
  sizeof(freighter_verts) / sizeof(freighter_verts[0]),
  freighter_edges,
  sizeof(freighter_edges) / sizeof(freighter_edges[0]),
};

// R25: interceptor — narrow dart with swept wings. Long forward needle
// and tiny twin thrusters give a pirate-fighter silhouette.
static const Vec3 interceptor_verts[] = {
  { 0.00f,  0.00f,  1.40f}, // 0 nose tip
  { 0.00f,  0.18f,  0.30f}, // 1 cockpit top
  { 0.55f, -0.05f, -0.40f}, // 2 right wing tip
  {-0.55f, -0.05f, -0.40f}, // 3 left wing tip
  { 0.12f,  0.00f, -0.85f}, // 4 right thrust
  {-0.12f,  0.00f, -0.85f}, // 5 left thrust
  { 0.00f,  0.15f, -0.55f}, // 6 spine peak rear
  { 0.20f,  0.06f,  0.10f}, // 7 right shoulder
  {-0.20f,  0.06f,  0.10f}, // 8 left shoulder
};
static const Edge interceptor_edges[] = {
  // Spine
  {0, 1}, {1, 6}, {6, 4}, {6, 5},
  // Belly nose
  {0, 4}, {0, 5},
  // Wings
  {1, 2}, {2, 4}, {1, 3}, {3, 5},
  // Wing roots
  {7, 2}, {8, 3}, {1, 7}, {1, 8},
  // Tail
  {4, 5},
};
static const Model interceptor = {
  interceptor_verts,
  sizeof(interceptor_verts) / sizeof(interceptor_verts[0]),
  interceptor_edges,
  sizeof(interceptor_edges) / sizeof(interceptor_edges[0]),
};

// R25: gunship — bulky escort with twin dorsal turret stalks and broad
// wings. Reads as a slower, heavier attacker than the interceptor.
static const Vec3 gunship_verts[] = {
  { 0.00f,  0.00f,  1.00f}, // 0  nose
  { 0.00f,  0.22f,  0.30f}, // 1  spine fwd
  { 0.45f,  0.20f,  0.10f}, // 2  right turret base
  {-0.45f,  0.20f,  0.10f}, // 3  left  turret base
  { 0.45f,  0.45f,  0.10f}, // 4  right turret tip
  {-0.45f,  0.45f,  0.10f}, // 5  left  turret tip
  { 0.80f,  0.00f, -0.20f}, // 6  right wing tip
  {-0.80f,  0.00f, -0.20f}, // 7  left  wing tip
  { 0.00f, -0.20f,  0.30f}, // 8  belly
  { 0.00f,  0.20f, -0.70f}, // 9  spine aft
  { 0.30f,  0.00f, -0.85f}, // 10 right thrust
  {-0.30f,  0.00f, -0.85f}, // 11 left  thrust
};
static const Edge gunship_edges[] = {
  // Spine
  {0, 1}, {1, 9},
  // Belly
  {0, 8}, {8, 10}, {8, 11},
  // Wings
  {1, 6}, {6, 10}, {1, 7}, {7, 11},
  // Turret stalks
  {2, 4}, {3, 5},
  // Turret bases tied to spine and wings
  {1, 2}, {1, 3}, {2, 6}, {3, 7},
  // Tail
  {9, 10}, {9, 11}, {10, 11},
};
static const Model gunship = {
  gunship_verts,
  sizeof(gunship_verts) / sizeof(gunship_verts[0]),
  gunship_edges,
  sizeof(gunship_edges) / sizeof(gunship_edges[0]),
};

// R25: mining barge — fat blocky hull with a forward scoop chin.
// Slowest-looking silhouette in the roster.
static const Vec3 barge_verts[] = {
  { 0.00f,  0.00f,  0.95f}, // 0 nose center
  { 0.45f,  0.25f,  0.55f}, // 1 front TR
  {-0.45f,  0.25f,  0.55f}, // 2 front TL
  { 0.45f, -0.25f,  0.55f}, // 3 front BR
  {-0.45f, -0.25f,  0.55f}, // 4 front BL
  { 0.55f,  0.30f, -0.85f}, // 5 rear TR
  {-0.55f,  0.30f, -0.85f}, // 6 rear TL
  { 0.55f, -0.30f, -0.85f}, // 7 rear BR
  {-0.55f, -0.30f, -0.85f}, // 8 rear BL
  { 0.00f, -0.45f,  0.75f}, // 9 scoop chin
};
static const Edge barge_edges[] = {
  // Nose to front face
  {0, 1}, {0, 2}, {0, 3}, {0, 4},
  // Front face
  {1, 2}, {1, 3}, {2, 4}, {3, 4},
  // Side rails
  {1, 5}, {2, 6}, {3, 7}, {4, 8},
  // Rear face
  {5, 6}, {5, 7}, {6, 8}, {7, 8},
  // Scoop chin
  {9, 3}, {9, 4}, {9, 0},
};
static const Model barge = {
  barge_verts,
  sizeof(barge_verts) / sizeof(barge_verts[0]),
  barge_edges,
  sizeof(barge_edges) / sizeof(barge_edges[0]),
};

// R25: alien hex — symmetric six-pointed silhouette with no clear nose.
// Stands out hard against the wedge/freighter/dart roster.
static const Vec3 alien_verts[] = {
  { 0.00f,  0.00f,  1.20f}, // 0 front tip
  { 0.00f,  0.00f, -1.00f}, // 1 rear tip
  { 0.65f,  0.00f,  0.00f}, // 2 right peak
  {-0.65f,  0.00f,  0.00f}, // 3 left peak
  { 0.00f,  0.55f,  0.00f}, // 4 top peak
  { 0.00f, -0.55f,  0.00f}, // 5 bottom peak
  { 0.30f,  0.18f,  0.45f}, // 6 inner-ring NE
  {-0.30f,  0.18f,  0.45f}, // 7 inner-ring NW
  { 0.30f, -0.18f,  0.45f}, // 8 inner-ring SE
  {-0.30f, -0.18f,  0.45f}, // 9 inner-ring SW
};
static const Edge alien_edges[] = {
  // Spine
  {0, 1},
  // Front cones — six points fan from the nose
  {0, 2}, {0, 3}, {0, 4}, {0, 5},
  // Rear cones
  {1, 2}, {1, 3}, {1, 4}, {1, 5},
  // Equator ring
  {2, 4}, {4, 3}, {3, 5}, {5, 2},
  // Forward inner ornament
  {6, 7}, {7, 9}, {9, 8}, {8, 6},
  {0, 6}, {0, 7}, {0, 8}, {0, 9},
};
static const Model alien = {
  alien_verts,
  sizeof(alien_verts) / sizeof(alien_verts[0]),
  alien_edges,
  sizeof(alien_edges) / sizeof(alien_edges[0]),
};

// R25: model registry. NPCShip stores a ModelId and looks the mesh up
// at render time so the renderer doesn't have to know about ship roles.
enum ModelId : uint8_t {
  Wedge       = 0,
  Freighter,
  Interceptor,
  Gunship,
  Barge,
  Alien,
  ModelCount,
};

inline const Model& byId(uint8_t id) {
  switch (id) {
    case Freighter:   return freighter;
    case Interceptor: return interceptor;
    case Gunship:     return gunship;
    case Barge:       return barge;
    case Alien:       return alien;
    default:          return wedge;
  }
}

// Per-model world-space scale (sysu). Larger silhouettes for slow-looking
// ships, smaller for the dart. Keeps relative size cues even at range.
inline float scaleFor(uint8_t id) {
  switch (id) {
    case Freighter:   return 320.0f;
    case Interceptor: return 170.0f;
    case Gunship:     return 240.0f;
    case Barge:       return 340.0f;
    case Alien:       return 260.0f;
    default:          return 220.0f;  // Wedge baseline
  }
}

struct Pose {
  float x, y, z;        // world position relative to camera (z forward)
  float yaw, pitch, roll;
  float scale;
};

// Darken an RGB565 color toward black by factor k ∈ [0..1]. Used for the
// filled silhouette so the wireframe edges still read as the brightest
// detail on the hull.
inline uint16_t darkenRGB565(uint16_t c, float k) {
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  int r  = (c >> 11) & 0x1F;
  int gg = (c >> 5)  & 0x3F;
  int b  =  c        & 0x1F;
  r  = (int)((float)r  * k);
  gg = (int)((float)gg * k);
  b  = (int)((float)b  * k);
  return (uint16_t)((r << 11) | (gg << 5) | b);
}

// Render a ship given its local axes (forward, up) expressed directly in
// camera space. Lets the caller compose the camera basis without going
// through Euler yaw/pitch/roll — required once the camera can roll, since
// the old pose math can't represent a banked view.
//
// Two-pass paint: first fill the convex hull of the projected vertices
// in a darkened ship color (turning the wireframe into a solid
// silhouette), then overlay the wireframe edges in the original color
// for that classic filled-polygon-plus-outline look.
inline void renderBasis(M5Canvas& g, const Model& m,
                        float cx, float cy, float cz,
                        float ffx, float ffy, float ffz,
                        float uux, float uuy, float uuz,
                        float scale, uint16_t color) {
  // right = up × forward (in camera space).
  float rrx = uuy * ffz - uuz * ffy;
  float rry = uuz * ffx - uux * ffz;
  float rrz = uux * ffy - uuy * ffx;

  const float fov = 110.0f;
  const int viewCx = Config::ViewX + Config::ViewW / 2;
  const int viewCy = Config::ViewY + Config::ViewH / 2;

  struct ProjV { int sx, sy; bool visible; };
  ProjV pv[32];

  for (int i = 0; i < m.nverts && i < 32; i++) {
    float vx = m.verts[i].x * scale;
    float vy = m.verts[i].y * scale;
    float vz = m.verts[i].z * scale;

    // Place vertex in camera space: cx + vx*right + vy*up + vz*forward.
    float x = cx + vx * rrx + vy * uux + vz * ffx;
    float y = cy + vx * rry + vy * uuy + vz * ffy;
    float z = cz + vx * rrz + vy * uuz + vz * ffz;

    if (z < 3.0f) { pv[i].visible = false; continue; }
    pv[i].sx = viewCx + (int)(x * fov / z);
    pv[i].sy = viewCy - (int)(y * fov / z);
    pv[i].visible = true;
  }

  // ---- Filled silhouette via convex hull (gift wrap) ----
  int  pxs[32], pys[32], nP = 0;
  for (int i = 0; i < m.nverts && i < 32; i++) {
    if (pv[i].visible) {
      pxs[nP] = pv[i].sx;
      pys[nP] = pv[i].sy;
      nP++;
    }
  }
  if (nP >= 3) {
    // Leftmost-then-topmost as the start vertex.
    int start = 0;
    for (int i = 1; i < nP; i++) {
      if (pxs[i] < pxs[start] ||
          (pxs[i] == pxs[start] && pys[i] < pys[start])) start = i;
    }
    int hull[32]; int nH = 0;
    int current = start;
    do {
      hull[nH++] = current;
      int next = -1;
      for (int j = 0; j < nP; j++) {
        if (j == current) continue;
        if (next == -1) { next = j; continue; }
        long crossZ =
            (long)(pxs[next] - pxs[current]) * (pys[j] - pys[current]) -
            (long)(pys[next] - pys[current]) * (pxs[j] - pxs[current]);
        if (crossZ < 0) next = j;
      }
      current = next;
    } while (current != start && nH < 32);

    if (nH >= 3) {
      uint16_t fill = darkenRGB565(color, 0.60f);
      for (int i = 1; i < nH - 1; i++) {
        g.fillTriangle(pxs[hull[0]],     pys[hull[0]],
                       pxs[hull[i]],     pys[hull[i]],
                       pxs[hull[i + 1]], pys[hull[i + 1]],
                       fill);
      }
    }
  }

  // ---- Wireframe edges on top ----
  const int x0 = Config::ViewX + 1;
  const int y0 = Config::ViewY + 1;
  const int x1 = Config::ViewX + Config::ViewW - 2;
  const int y1 = Config::ViewY + Config::ViewH - 2;

  for (int i = 0; i < m.nedges; i++) {
    const auto& e = m.edges[i];
    if (!pv[e.a].visible || !pv[e.b].visible) continue;
    int ax = pv[e.a].sx, ay = pv[e.a].sy;
    int bx = pv[e.b].sx, by = pv[e.b].sy;
    if ((ax < x0 && bx < x0) || (ax > x1 && bx > x1) ||
        (ay < y0 && by < y0) || (ay > y1 && by > y1)) continue;
    g.drawLine(ax, ay, bx, by, color);
  }
}

inline void render(M5Canvas& g, const Model& m, const Pose& p, uint16_t color) {
  const float cy = cosf(p.yaw),   sy = sinf(p.yaw);
  const float cp = cosf(p.pitch), sp = sinf(p.pitch);
  const float cr = cosf(p.roll),  sr = sinf(p.roll);

  const float fov = 110.0f;
  const int viewCx = Config::ViewX + Config::ViewW / 2;
  const int viewCy = Config::ViewY + Config::ViewH / 2;

  // Up to 32 vertices supported
  struct ProjV { int sx, sy; bool visible; };
  ProjV pv[32];

  for (int i = 0; i < m.nverts && i < 32; i++) {
    float x = m.verts[i].x * p.scale;
    float y = m.verts[i].y * p.scale;
    float z = m.verts[i].z * p.scale;

    // roll around model z
    float xr = x * cr - y * sr;
    float yr = x * sr + y * cr;
    x = xr; y = yr;
    // pitch around model x
    float yp = y * cp - z * sp;
    float zp = y * sp + z * cp;
    y = yp; z = zp;
    // yaw around model y
    float xv = x * cy + z * sy;
    float zv = -x * sy + z * cy;
    x = xv; z = zv;

    x += p.x; y += p.y; z += p.z;

    if (z < 3.0f) {
      pv[i].visible = false;
      continue;
    }
    pv[i].sx = viewCx + (int)(x * fov / z);
    pv[i].sy = viewCy - (int)(y * fov / z);
    pv[i].visible = true;
  }

  const int x0 = Config::ViewX + 1;
  const int y0 = Config::ViewY + 1;
  const int x1 = Config::ViewX + Config::ViewW - 2;
  const int y1 = Config::ViewY + Config::ViewH - 2;

  for (int i = 0; i < m.nedges; i++) {
    const auto& e = m.edges[i];
    if (!pv[e.a].visible || !pv[e.b].visible) continue;
    int ax = pv[e.a].sx, ay = pv[e.a].sy;
    int bx = pv[e.b].sx, by = pv[e.b].sy;
    if ((ax < x0 && bx < x0) || (ax > x1 && bx > x1) ||
        (ay < y0 && by < y0) || (ay > y1 && by > y1)) continue;
    g.drawLine(ax, ay, bx, by, color);
  }
}

} // namespace Ship3D
