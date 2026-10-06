#pragma once
#include <M5Cardputer.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "Capture.h"

// Gyro aiming from the Cardputer ADV's BMI270 IMU (the original Cardputer
// has none; M5Unified brings it up in M5Cardputer.begin()).
//
// The ship turns as the Cardputer turns, times the chosen gain: rotate
// it 20 degrees left and, at 1X, the ship yaws 40 degrees left (a true
// 1:1, now 0.5X, felt sluggish on the small screen); hold still and the
// ship holds its heading. Keys add on top, and holding ALT
// freezes the gyro so the player can re-centre their hands.
//
// Rotations are taken in "player space", so the holding angle doesn't
// matter: yaw is turning about the real vertical, pitch is tipping the
// top edge toward / away, roll is turning it like a steering wheel. The
// vertical comes from a complementary filter on the accelerometer.

namespace Gyro {

// ---- Option -----------------------------------------------------------

enum Mode : uint8_t { Off = 0, GainHalf, Gain1, Gain2, Gain3, ModeCount };
inline uint8_t mode = Off;
inline const char* const ModeNames[ModeCount] = { "OFF", "0.5X", "1X", "2X", "3X" };
// Ship degrees per device degree.
constexpr float ModeGain[ModeCount] = { 0.0f, 1.0f, 2.0f, 4.0f, 6.0f };

inline bool available() { return M5.Imu.isEnabled(); }
inline bool active() { return mode != Off && available() && !Capture::lockstep; }

// Menu label for the GYRO row, styled like Audio::soundLabel.
inline const char* label(bool arrows = false) {
  static char buf[20];
  if (!available()) return "GYRO: N/A";
  uint8_t m = mode < ModeCount ? mode : (uint8_t)Off;
  if (arrows) {
    snprintf(buf, sizeof(buf), "%s GYRO: %s %s", m > 0 ? "<" : " ",
             ModeNames[m], m + 1 < ModeCount ? ">" : " ");
  } else {
    snprintf(buf, sizeof(buf), "GYRO: %s", ModeNames[m]);
  }
  return buf;
}

// ---- Device frame -------------------------------------------------------

// X points right across the screen, Y up the screen, Z out of the screen.
// Map[i] is the chip axis feeding device axis i, Sign[i] its direction.
// A remap must stay a proper rotation (even permutation, even number of
// sign flips) — the chip is right-handed and so is this frame.
constexpr int   Map[3]  = { 0, 1, 2 };
constexpr float Sign[3] = { 1.0f, 1.0f, 1.0f };

// ---- Tuning -------------------------------------------------------------

constexpr float DegToRad = 0.017453293f;
constexpr float Tau      = 0.4f;               // s, accel pull on the vertical
constexpr float ShakeG   = 0.25f;              // skip accel off 1 g by more
constexpr float StillRad = 2.0f * DegToRad;    // below this the device is "still"
constexpr float BiasTau  = 2.0f;               // s, drift tracking while still
constexpr float DeadRad  = 0.5f * DegToRad;    // per-axis noise floor
constexpr float MaxRate  = 8.0f;               // rad/s cap on each ship axis

// SystemFlight::update turns the ship by pitchRate * 0.9, yawRate * 0.9
// and rollRate * 1.4 rad/s; dividing by these makes ModeGain exact.
constexpr float ShipPitchK = 0.9f;
constexpr float ShipYawK   = 0.9f;
constexpr float ShipRollK  = 1.4f;

// ---- State --------------------------------------------------------------

inline float acc[3], gyr[3];          // last sample: g, rad/s (device frame)
inline float bias[3];                 // tracked gyro drift, rad/s
inline float up[3] = { 0, 0, 1 };     // filtered vertical, unit length
inline float outPitch, outYaw, outRoll;   // last ship rates, rad/s

// Reads one sample into acc / gyr. False without an IMU.
inline bool sample() {
  if (!available()) return false;
  M5.Imu.update();
  const auto& d = M5.Imu.getImuData();
  for (int i = 0; i < 3; i++) {
    acc[i] = Sign[i] * d.accel.value[Map[i]];
    gyr[i] = Sign[i] * d.gyro.value[Map[i]] * DegToRad;
  }
  return true;
}

inline float accNorm() {
  return sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
}

// Snap the vertical to the last accelerometer sample.
inline void level() {
  float n = accNorm();
  if (n > 0.1f) {
    for (int i = 0; i < 3; i++) up[i] = acc[i] / n;
  }
}

// Once per flight frame while active(). `reprime` on entering flight,
// since the filter hasn't run in the menus.
inline void update(float dt, bool reprime) {
  if (!sample()) return;
  if (reprime) { level(); return; }
  float n = accNorm();

  // Drift: whatever the gyro reads while the hand is still.
  float w[3];
  bool still = true;
  for (int i = 0; i < 3; i++) {
    w[i] = gyr[i] - bias[i];
    if (fabsf(w[i]) > StillRad) still = false;
  }
  if (still) {
    float k = dt / (BiasTau + dt);
    for (int i = 0; i < 3; i++) bias[i] += w[i] * k;
  }

  // A world-fixed vector seen from the rotating body: dv/dt = v x w.
  float v0 = up[0], v1 = up[1], v2 = up[2];
  up[0] += (v1 * w[2] - v2 * w[1]) * dt;
  up[1] += (v2 * w[0] - v0 * w[2]) * dt;
  up[2] += (v0 * w[1] - v1 * w[0]) * dt;
  if (fabsf(n - 1.0f) < ShakeG) {
    float k = dt / (Tau + dt);
    for (int i = 0; i < 3; i++) up[i] += (acc[i] / n - up[i]) * k;
  }
  float len = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
  if (len > 1e-3f) {
    for (int i = 0; i < 3; i++) up[i] /= len;
  }
}

inline float deadband(float v) {
  if (v >  DeadRad) return v - DeadRad;
  if (v < -DeadRad) return v + DeadRad;
  return 0.0f;
}

inline float capRate(float v) {
  return v > MaxRate ? MaxRate : (v < -MaxRate ? -MaxRate : v);
}

// Player-space turn of the device, rad/s, drift removed: [0] pitch
// (+ top edge toward the player), [1] yaw (+ right), [2] roll (+ right
// side down).
inline void playerRates(float out[3]) {
  float w[3];
  for (int i = 0; i < 3; i++) w[i] = gyr[i] - bias[i];

  // Player-space axes: right = screen X flattened against the vertical,
  // toward = horizontal line of sight back at the player.
  float d  = up[0];
  float r0 = 1.0f - d * up[0], r1 = -d * up[1], r2 = -d * up[2];
  float rl = sqrtf(r0 * r0 + r1 * r1 + r2 * r2);
  if (rl < 1e-3f) { r0 = 1.0f; r1 = r2 = 0.0f; rl = 1.0f; }
  r0 /= rl; r1 /= rl; r2 /= rl;
  float t0 = r1 * up[2] - r2 * up[1];
  float t1 = r2 * up[0] - r0 * up[2];
  float t2 = r0 * up[1] - r1 * up[0];

  // Right-hand rule: about right = nose up, about up = nose left, about
  // toward-the-player = right side up (roll left).
  out[0] =  (w[0] * r0    + w[1] * r1    + w[2] * r2);
  out[1] = -(w[0] * up[0] + w[1] * up[1] + w[2] * up[2]);
  out[2] = -(w[0] * t0    + w[1] * t1    + w[2] * t2);
}

// Ship turn rates from the last sample, in GameState rate units (+pitch
// nose up, +yaw nose right, +roll right wing down).
inline void read(float& pitch, float& yaw, float& roll) {
  float pr[3];
  playerRates(pr);
  float gain = ModeGain[mode < ModeCount ? mode : 0];
  outPitch = capRate(deadband(pr[0]) * gain / ShipPitchK);
  outYaw   = capRate(deadband(pr[1]) * gain / ShipYawK);
  outRoll  = capRate(deadband(pr[2]) * gain / ShipRollK);
  pitch = outPitch;
  yaw   = outYaw;
  roll  = outRoll;
}

// ---- Menu steps -------------------------------------------------------

// Step OFF -> 0.5X -> 1X -> 2X -> 3X -> OFF. False without an IMU.
inline bool cycleMode() {
  if (!available()) return false;
  mode = (uint8_t)((mode + 1) % ModeCount);
  return true;
}

// LEFT / RIGHT on the GYRO row, no wrap. False at either end.
inline bool stepMode(int delta) {
  if (!available()) return false;
  int m = (int)mode + delta;
  if (m < 0 || m >= (int)ModeCount) return false;
  mode = (uint8_t)m;
  return true;
}

} // namespace Gyro
