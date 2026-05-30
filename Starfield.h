#pragma once
#include <M5GFX.h>
#include <math.h>
#include <esp_random.h>
#include "Config.h"

class Starfield {
public:
  static constexpr int NSTARS = 72;

  struct Star {
    float x, y, z;
    int prevSx, prevSy;
    bool hasPrev;
  };

  Star stars[NSTARS];

  void init() {
    for (auto& s : stars) respawn(s, true);
  }

  static float frand(float lo, float hi) {
    float u = esp_random() / (float)UINT32_MAX;
    return lo + (hi - lo) * u;
  }

  void respawn(Star& s, bool anywhereZ) {
    s.x = frand(-2.0f, 2.0f);
    s.y = frand(-1.4f, 1.4f);
    s.z = anywhereZ ? frand(0.15f, 1.6f) : frand(1.2f, 1.6f);
    s.hasPrev = false;
  }

  void update(float throttle, float pitchRate, float yawRate, float rollRate, float dt) {
    // Forward velocity (z shrinks toward 0). Pure throttle — zero stick
    // means zero translation, so a hovering ship has stars locked in
    // place. Rotation still updates the field from pitch/yaw/roll input.
    float vz = -1.95f * throttle;

    // Rotation deltas this frame
    float ap = pitchRate * dt;
    float ay = yawRate   * dt;
    float ar = rollRate  * dt;
    float cp = cosf(ap), sp = sinf(ap);
    float cy = cosf(ay), sy = sinf(ay);
    float cr = cosf(ar), sr = sinf(ar);

    for (auto& s : stars) {
      // Translate
      s.z += vz * dt;

      // Roll around view axis (z)
      float xr = s.x * cr - s.y * sr;
      float yr = s.x * sr + s.y * cr;
      s.x = xr; s.y = yr;

      // Pitch (around x): mixes y,z
      float yp = s.y * cp - s.z * sp;
      float zp = s.y * sp + s.z * cp;
      s.y = yp; s.z = zp;

      // Yaw (around y): mixes x,z
      float xv = s.x * cy + s.z * sy;
      float zv = -s.x * sy + s.z * cy;
      s.x = xv; s.z = zv;

      if (s.z <= 0.08f || fabsf(s.x) > 6.0f || fabsf(s.y) > 6.0f) {
        respawn(s, false);
      }
    }
  }

  void draw(M5Canvas& g) {
    const float fov = 110.0f;
    const int cx = Config::ViewX + Config::ViewW / 2;
    const int cy = Config::ViewY + Config::ViewH / 2;
    const int xMin = Config::ViewX + 1;
    const int yMin = Config::ViewY + 1;
    const int xMax = Config::ViewX + Config::ViewW - 2;
    const int yMax = Config::ViewY + Config::ViewH - 2;

    for (auto& s : stars) {
      int sx = cx + (int)(s.x * fov / s.z);
      int sy = cy + (int)(s.y * fov / s.z);

      if (sx < xMin || sx > xMax || sy < yMin || sy > yMax) {
        s.hasPrev = false;
        continue;
      }

      uint16_t color;
      if (s.z < 0.35f)      color = TFT_WHITE;
      else if (s.z < 0.75f) color = 0xCE79; // light gray
      else                  color = 0x8410; // dim gray

      if (s.hasPrev) {
        g.drawLine(s.prevSx, s.prevSy, sx, sy, color);
      } else {
        g.drawPixel(sx, sy, color);
      }
      s.prevSx = sx;
      s.prevSy = sy;
      s.hasPrev = true;
    }
  }
};
