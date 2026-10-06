#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdint.h>
#include "Config.h"

// Viewport-clipped line and triangle drawing for anything projected
// through the flight camera.
//
// M5GFX rasterizes off-screen geometry the slow way: fillTriangle walks
// every row and every pixel of horizontal travel from the top vertex
// down, clipping only at the span, and drawLine steps pixel by pixel
// until it reaches the clip rect. A vertex just past the near plane can
// project tens of thousands of pixels out (a ship corner as a pirate
// circles abeam, a sphere vertex on a close pass), which turned single
// shapes into milliseconds of work and whole frames into visible
// stutters. Clipping to the viewport first means the rasterizer only
// ever sees on-screen coordinates.

namespace Raster {

constexpr float X0 = (float)Config::ViewX;
constexpr float Y0 = (float)Config::ViewY;
constexpr float X1 = (float)(Config::ViewX + Config::ViewW - 1);
constexpr float Y1 = (float)(Config::ViewY + Config::ViewH - 1);

inline bool inside(float x, float y) {
  return x >= X0 && x <= X1 && y >= Y0 && y <= Y1;
}

inline int pix(float v) { return (int)lroundf(v); }

// Liang–Barsky. Trims the segment to the viewport; false if it misses.
inline bool clipLine(float& x0, float& y0, float& x1, float& y1) {
  float dx = x1 - x0, dy = y1 - y0;
  float t0 = 0.0f, t1 = 1.0f;
  auto edge = [&](float p, float q) -> bool {
    if (p == 0.0f) return q >= 0.0f;
    float r = q / p;
    if (p < 0.0f) { if (r > t1) return false; if (r > t0) t0 = r; }
    else          { if (r < t0) return false; if (r < t1) t1 = r; }
    return true;
  };
  if (!edge(-dx, x0 - X0) || !edge(dx, X1 - x0) ||
      !edge(-dy, y0 - Y0) || !edge(dy, Y1 - y0)) return false;
  float ax = x0 + t0 * dx, ay = y0 + t0 * dy;
  x1 = x0 + t1 * dx; y1 = y0 + t1 * dy;
  x0 = ax; y0 = ay;
  return true;
}

inline void line(M5Canvas& g, float x0, float y0, float x1, float y1,
                 uint16_t col) {
  if (!(inside(x0, y0) && inside(x1, y1)) && !clipLine(x0, y0, x1, y1)) return;
  g.drawLine(pix(x0), pix(y0), pix(x1), pix(y1), col);
}

// Filled triangle. Fully on-screen goes straight to the rasterizer;
// fully off one side is dropped; otherwise it's clipped against the four
// viewport edges (Sutherland–Hodgman, at most 7 vertices) and fanned.
inline void tri(M5Canvas& g, float ax, float ay, float bx, float by,
                float cx, float cy, uint16_t col) {
  if ((ax < X0 && bx < X0 && cx < X0) || (ax > X1 && bx > X1 && cx > X1) ||
      (ay < Y0 && by < Y0 && cy < Y0) || (ay > Y1 && by > Y1 && cy > Y1)) {
    return;
  }
  if (inside(ax, ay) && inside(bx, by) && inside(cx, cy)) {
    g.fillTriangle(pix(ax), pix(ay), pix(bx), pix(by), pix(cx), pix(cy), col);
    return;
  }

  float px[8] = { ax, bx, cx }, py[8] = { ay, by, cy };
  float qx[8], qy[8];
  int n = 3;
  for (int e = 0; e < 4; e++) {
    // Signed distance inside edge e (>= 0 keeps the point).
    auto dist = [e](float x, float y) {
      switch (e) {
        case 0:  return x - X0;
        case 1:  return X1 - x;
        case 2:  return y - Y0;
        default: return Y1 - y;
      }
    };
    int m = 0;
    for (int i = 0; i < n; i++) {
      int j = (i + n - 1) % n;   // previous vertex
      float di = dist(px[i], py[i]), dj = dist(px[j], py[j]);
      if ((di >= 0.0f) != (dj >= 0.0f)) {
        float t = dj / (dj - di);
        qx[m] = px[j] + t * (px[i] - px[j]);
        qy[m] = py[j] + t * (py[i] - py[j]);
        m++;
      }
      if (di >= 0.0f) { qx[m] = px[i]; qy[m] = py[i]; m++; }
    }
    n = m;
    if (n < 3) return;
    for (int i = 0; i < n; i++) { px[i] = qx[i]; py[i] = qy[i]; }
  }
  int x0 = pix(px[0]), y0 = pix(py[0]);
  for (int i = 1; i < n - 1; i++) {
    g.fillTriangle(x0, y0, pix(px[i]), pix(py[i]),
                   pix(px[i + 1]), pix(py[i + 1]), col);
  }
}

} // namespace Raster
