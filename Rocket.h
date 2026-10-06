#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdint.h>

// Missile art: the HUD rack icon and the in-flight rocket sprite.

namespace Rocket {

constexpr uint16_t cBody  = 0xC618;   // light grey
constexpr uint16_t cShade = 0x8410;   // body's shadow side
constexpr uint16_t cEmpty = 0x2965;   // empty rack slot silhouette

// 8×5 rack rocket pointing right: nozzle, fins, body, red nose cone.
// An empty slot keeps a dim silhouette so the rack reads at a glance.
inline void drawIcon(M5Canvas& g, int x, int y, bool loaded) {
  static const char* const art[5] = {
    "FF......",
    ".FBBBBN.",
    "EBBBBBNN",
    ".FBBBBN.",
    "FF......",
  };
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 8; c++) {
      uint16_t col;
      switch (art[r][c]) {
        case 'B': col = cBody;      break;
        case 'N': col = TFT_RED;    break;
        case 'F': col = TFT_YELLOW; break;
        case 'E': col = 0x6B4D;     break;   // dark nozzle
        default:  continue;
      }
      g.drawPixel(x + c, y + r, loaded ? col : cEmpty);
    }
  }
}

// In-flight rocket from its projected nose (hx, hy) and tail (tx, ty):
// a fat shaded body (that's what tells it from an arrow), tapered nose
// cone and stubby fins in the side colour, and a short flickering plume.
// The body is held to 5..22 px so it reads at range without ballooning
// up close; nose-on it's a blob in its plume. `tick` steps the flicker
// (e.g. millis()/50 plus a per-missile offset).
inline void draw(M5Canvas& g, int hx, int hy, int tx, int ty,
                 uint16_t side, uint32_t tick) {
  float dx = (float)(tx - hx), dy = (float)(ty - hy);
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) {
    g.drawRect(hx - 1, hy - 1, 3, 3, (tick & 1u) ? TFT_ORANGE : TFT_YELLOW);
    g.drawPixel(hx, hy, side);
    return;
  }
  float ux = dx / len, uy = dy / len;   // nose → tail
  if (len < 5.0f)  len = 5.0f;
  if (len > 22.0f) len = 22.0f;
  float px = -uy, py = ux;              // perpendicular
  float ex = hx + ux * len, ey = hy + uy * len;   // engine end
  bool big = len >= 12.0f;
  float half = big ? 1.0f : 0.5f;       // body half-width
  auto line = [&](float x0, float y0, float x1, float y1, uint16_t c) {
    g.drawLine((int)lroundf(x0), (int)lroundf(y0),
               (int)lroundf(x1), (int)lroundf(y1), c);
  };

  // Plume first so the body sits on top: orange flame, yellow core.
  float pl = len * 0.3f + 1.0f + (float)(tick % 3u);
  line(ex, ey, ex + ux * pl, ey + uy * pl, TFT_ORANGE);
  if (big) {
    line(ex + px, ey + py, ex + px + ux * pl * 0.6f,
         ey + py + uy * pl * 0.6f, TFT_ORANGE);
  }
  line(ex, ey, ex + ux * pl * 0.5f, ey + uy * pl * 0.5f, TFT_YELLOW);

  // Fins: short stubs swept back off the last quarter of the body.
  float span = half + (big ? 2.0f : 1.5f);
  float fx = hx + ux * len * 0.75f, fy = hy + uy * len * 0.75f;
  line(fx, fy, ex + px * span + ux, ey + py * span + uy, side);
  line(fx, fy, ex - px * span + ux, ey - py * span + uy, side);

  // Body as parallel strokes — lit centre, shaded flanks — with the nose
  // cone painted over the front. Flank strokes start a pixel further back
  // so the nose comes to a point.
  float nose = big ? 3.0f : 2.0f;
  for (float o = -half; o <= half + 0.01f; o += 1.0f) {
    bool flank = big ? (o != 0.0f) : (o > 0.0f);
    float back = flank ? 1.0f : 0.0f;
    float sx = hx + px * o + ux * back, sy = hy + py * o + uy * back;
    line(sx, sy, ex + px * o, ey + py * o, flank ? cShade : cBody);
    line(sx, sy, sx + ux * nose, sy + uy * nose, side);
  }
  g.drawPixel(hx, hy, TFT_WHITE);
}

} // namespace Rocket
