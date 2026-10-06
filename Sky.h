#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdlib.h>
#include "Config.h"
#include "SystemFlight.h"

// World-consistent sky for in-system flight.
//
// Two layers, both projected through SystemFlight's camera so they can
// never disagree with the planets:
//   - distant stars: fixed world *directions*. They swing only when the
//     ship rotates and never drift as it travels — a stable backdrop to
//     judge turns against, and an endless sky out in deep space.
//   - dust: motes anchored in world space, in a cube that wraps around
//     the player. They stream past only when the ship actually moves, so
//     the sense of speed is honest (stopped = still).
//
// The old Starfield stays for the hyperspace tunnel, where there's no
// world to agree with.

namespace Sky {

constexpr int   NumStars = 220;     // ~10% fall inside the viewport at once
constexpr int   NumDust  = 56;
constexpr float DustHalf = 700.0f;  // half-size of the dust cube, sysu
constexpr float DustNear = 20.0f;   // motes closer than this are skipped
constexpr int   MaxStreak = 48;     // px — longer jumps draw as a dot

struct Star { float x, y, z; uint16_t color; };
struct Mote { float x, y, z; int16_t psx, psy; bool hasPrev; };

inline Star     stars[NumStars];
inline Mote     dust[NumDust];
inline bool     ready = false;
inline uint32_t rng   = 0x5EEDF00Du;
inline float    lastX = 0.0f, lastY = 0.0f, lastZ = 0.0f;

inline float frand() {   // [0, 1)
  rng = rng * 1664525u + 1013904223u;
  return (float)(rng >> 8) * (1.0f / 16777216.0f);
}

inline void placeMote(Mote& m, float cx, float cy, float cz) {
  m.x = cx + (frand() * 2.0f - 1.0f) * DustHalf;
  m.y = cy + (frand() * 2.0f - 1.0f) * DustHalf;
  m.z = cz + (frand() * 2.0f - 1.0f) * DustHalf;
  m.hasPrev = false;
}

inline void init() {
  for (auto& s : stars) {
    // Uniform on the unit sphere.
    float z   = frand() * 2.0f - 1.0f;
    float phi = frand() * 6.2831853f;
    float r   = sqrtf(1.0f - z * z);
    s.x = r * cosf(phi);
    s.y = z;
    s.z = r * sinf(phi);
    // Mostly faint, a few bright — reads as depth on a 1-px star.
    float b = frand();
    s.color = (b < 0.55f) ? 0x3186
            : (b < 0.82f) ? 0x632C
            : (b < 0.95f) ? 0xA514
            :               0xE73C;
  }
  const auto& st = SystemFlight::state;
  for (auto& m : dust) placeMote(m, st.px, st.py, st.pz);
  lastX = st.px; lastY = st.py; lastZ = st.pz;
  ready = true;
}

// Keep the dust cube centered on the player. Motes that fall out of one
// face re-enter at the opposite one; a big jump (spawn, launch, gate
// bump) re-scatters them so nothing streaks across the screen.
inline void update() {
  if (!ready) init();
  const auto& st = SystemFlight::state;
  float dx = st.px - lastX, dy = st.py - lastY, dz = st.pz - lastZ;
  bool jumped = (dx * dx + dy * dy + dz * dz) > DustHalf * DustHalf;
  lastX = st.px; lastY = st.py; lastZ = st.pz;

  auto wrap = [](float& v, float c, bool& moved) {
    if (v - c >  DustHalf) { v -= 2.0f * DustHalf; moved = true; }
    else if (v - c < -DustHalf) { v += 2.0f * DustHalf; moved = true; }
  };
  for (auto& m : dust) {
    if (jumped) { placeMote(m, st.px, st.py, st.pz); continue; }
    bool moved = false;
    wrap(m.x, st.px, moved);
    wrap(m.y, st.py, moved);
    wrap(m.z, st.pz, moved);
    if (moved) m.hasPrev = false;
  }
}

inline void draw(M5Canvas& g) {
  if (!ready) init();
  SystemFlight::syncCamera();
  const auto& st  = SystemFlight::state;
  const auto& cam = SystemFlight::cam;
  const int x0 = Config::ViewX + 1;
  const int y0 = Config::ViewY + 1;
  const int x1 = Config::ViewX + Config::ViewW - 2;
  const int y1 = Config::ViewY + Config::ViewH - 2;

  // Distant stars — rotation only, so project the direction itself.
  for (const auto& s : stars) {
    float cz = s.x * st.fx + s.y * st.fy + s.z * st.fz;
    if (cz < 0.05f) continue;
    float cx = s.x * cam.rx + s.y * cam.ry + s.z * cam.rz;
    float cy = s.x * st.ux  + s.y * st.uy  + s.z * st.uz;
    int sx, sy;
    SystemFlight::toScreen(cx, cy, cz, sx, sy);
    if (sx < x0 || sx > x1 || sy < y0 || sy > y1) continue;
    g.drawPixel(sx, sy, s.color);
  }

  // Dust — streak from where the mote was last frame.
  for (auto& m : dust) {
    float cx, cy, cz;
    SystemFlight::camSpace(m.x - st.px, m.y - st.py, m.z - st.pz, cx, cy, cz);
    if (cz <= DustNear) { m.hasPrev = false; continue; }
    int sx, sy;
    SystemFlight::toScreen(cx, cy, cz, sx, sy);
    if (sx < x0 || sx > x1 || sy < y0 || sy > y1) { m.hasPrev = false; continue; }
    uint16_t col = (cz < 200.0f) ? TFT_WHITE
                 : (cz < 450.0f) ? 0xCE79      // light grey
                 :                 0x8410;     // dim grey
    if (m.hasPrev && abs(sx - m.psx) + abs(sy - m.psy) < MaxStreak) {
      g.drawLine(m.psx, m.psy, sx, sy, col);
    } else {
      g.drawPixel(sx, sy, col);
    }
    m.psx = (int16_t)sx;
    m.psy = (int16_t)sy;
    m.hasPrev = true;
  }
}

} // namespace Sky
