#pragma once
#include <M5Cardputer.h>
#include <esp_random.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sound effects (R28, reworked R32, rebuilt v1.3).
//
// Every effect is synthesized once at boot into an 8-bit PCM arena and
// played with a single Speaker.playRaw() call, so playback never blocks
// the game loop.
//
// Synthesis is layer-based. An effect mixes a few voices, each one of:
//   - band-limited (PolyBLEP) saw / square, sine, triangle
//   - white noise through a 12 dB/oct low-pass (cutoff can glide) and an
//     optional high-pass — rumbles, roars, cracks, fizz
//   - sparse crackle bursts (explosion debris)
// with an exponential pitch glide, optional vibrato / tremolo, and an
// attack + exponential-decay envelope, which is what makes things ring
// out naturally instead of stopping on a linear ramp. Bells and metal
// hits are stacks of sine partials whose upper partials die faster.
//
// The finished mix is soft-clipped, normalized to a common peak (so the
// tiny speaker gets a consistent loudness) and given short fade-in /
// fade-out ramps so nothing clicks. Energy is kept above ~180 Hz — the
// Cardputer speaker reproduces almost nothing below that.
//
// The arena is sized by a measuring pass and heap-allocated, so adding an
// effect can never silently overflow it. Rumbly effects render at 8 kHz,
// bright ones at 11 kHz, to keep the arena small.
//
// Mixer channels keep effects from cutting each other off:
//   ChCombat — laser fire (rapid, replaceable)
//   ChEvent  — chimes, fanfares, warp, launch, zone crossings
//   ChHit    — impact feedback (target hits / player hits)
//   ChUI     — menu ticks, cash, deny
//   ChWarn   — alarms, lock tones, incoming-missile beeper
//   ChEngine — electric drive whine, synthesized live so it can glide
//   ChBoom   — explosions, missile launch, collisions, ECM

namespace Audio {

// ---- Output level ---------------------------------------------------

enum Level : uint8_t { LevelOff = 0, LevelLow, LevelMed, LevelHigh, LevelCount };
inline uint8_t level = LevelMed;
constexpr uint8_t LevelVolume[LevelCount] = { 0, 64, 120, 200 };

inline bool muted() { return level == LevelOff; }

// Menu label for the SOUND row. With `arrows` (row selected) it shows
// which way LEFT / RIGHT can still step: "< SOUND: MED >".
inline const char* soundLabel(bool arrows = false) {
  static const char* const names[LevelCount] = { "OFF", "LOW", "MED", "HIGH" };
  static char buf[20];
  uint8_t l = level < LevelCount ? level : (uint8_t)LevelMed;
  if (arrows) {
    snprintf(buf, sizeof(buf), "%s SOUND: %s %s", l > 0 ? "<" : " ",
             names[l], l + 1 < LevelCount ? ">" : " ");
  } else {
    snprintf(buf, sizeof(buf), "SOUND: %s", names[l]);
  }
  return buf;
}

// ---- Channels -------------------------------------------------------

constexpr int ChCombat = 0;
constexpr int ChEvent  = 1;
constexpr int ChHit    = 2;
constexpr int ChUI     = 3;
constexpr int ChWarn   = 4;
constexpr int ChEngine = 5;
constexpr int ChBoom   = 7;

// Per-channel trims (0..255). Lasers fire up to ~7 shots/s, so they sit
// well under the one-off events.
inline void applyChannelTrims() {
  auto& spk = M5Cardputer.Speaker;
  spk.setChannelVolume(ChCombat, 150);
  spk.setChannelVolume(ChEvent,  230);
  spk.setChannelVolume(ChHit,    210);
  spk.setChannelVolume(ChUI,     130);
  spk.setChannelVolume(ChWarn,   220);
  spk.setChannelVolume(ChBoom,   255);
  spk.setChannelVolume(ChEngine, 40);    // background bed — keep it very quiet
}

// ---- Arena ----------------------------------------------------------

constexpr int RateHi = 11025;   // Hz — bright effects
constexpr int RateLo = 8000;    // Hz — rumbles, roars, alarms

struct Fx { uint32_t off; uint32_t len; uint16_t rate; };

inline int8_t*  arena     = nullptr;
inline uint32_t arenaUsed = 0;

inline Fx fxLaser, fxLaserMil, fxHit, fxShieldHit, fxHullHit;
inline Fx fxAlert, fxMissile, fxEcm, fxDock, fxLaunch, fxWarp, fxAlarm;
inline Fx fxAccept, fxComplete, fxPromote, fxThump, fxDeny, fxBoom;
inline Fx fxTick, fxSelect, fxBack, fxCash;
inline Fx fxLockOn, fxLockFail, fxIncoming, fxShieldDown, fxHullCrit;
inline Fx fxZoneOut;

// ---- Synth ----------------------------------------------------------

enum class Wave : uint8_t { Sine, Tri, Saw, Square, Noise, Crackle };

// One layer of an effect. Times are in ms relative to the effect start.
// Field order matters for the designated initializers used below.
struct Voice {
  Wave  w      = Wave::Sine;
  float f0     = 440.0f;   // start pitch, Hz (ignored by noise)
  float f1     = 0.0f;     // end pitch, exponential glide (0 = hold f0)
  float at     = 0.0f;     // start time, ms
  float dur    = 100.0f;   // length, ms
  float amp    = 1.0f;
  float atk    = 2.0f;     // attack ramp, ms
  float decay  = 0.0f;     // exponential decay time constant, ms (0 = hold)
  float rel    = 8.0f;     // linear release at the end, ms
  float lp0    = 0.0f;     // low-pass cutoff glide lp0 -> lp1, Hz (0 = off)
  float lp1    = 0.0f;
  float hp     = 0.0f;     // high-pass cutoff, Hz (0 = off)
  float vibHz  = 0.0f;     // vibrato rate, Hz
  float vib    = 0.0f;     // vibrato depth, fraction of pitch
  float trmHz  = 0.0f;     // tremolo rate, Hz
  float trm    = 0.0f;     // tremolo depth, 0..1
  float detune = 0.0f;     // >0 adds a second oscillator at f * (1 + detune)
};

namespace Synth {
  inline bool     measuring = false;
  inline float*   mix       = nullptr;   // scratch for the effect being built
  inline uint32_t maxN      = 0;         // largest effect, samples (measuring)
  inline uint32_t n         = 0;         // samples in the current effect
  inline int      rate      = RateHi;
  inline uint32_t rng       = 0x1F2E3D4Cu;

  // Uniform in [-1, 1).
  inline float frand() {
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
  }

  // PolyBLEP residual — removes the aliasing step from saw / square
  // edges, which is what makes naive square waves sound harsh.
  inline float polyblep(float t, float dt) {
    if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
    return 0.0f;
  }

  inline float osc(Wave w, float ph, float dt) {
    switch (w) {
      case Wave::Sine:   return sinf(ph * 6.2831853f);
      case Wave::Tri:    return 1.0f - 4.0f * fabsf(ph - 0.5f);
      case Wave::Saw:    return 2.0f * ph - 1.0f - polyblep(ph, dt);
      case Wave::Square: {
        float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
        return (ph < 0.5f ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(ph2, dt);
      }
      default:           return 0.0f;
    }
  }
}

// Mix one voice into the current effect.
inline void voice(const Voice& v) {
  using namespace Synth;
  if (measuring || mix == nullptr) return;
  const float sr = (float)rate;
  uint32_t s0  = (uint32_t)(v.at  * sr / 1000.0f);
  uint32_t len = (uint32_t)(v.dur * sr / 1000.0f);
  if (s0 >= n || len == 0) return;
  if (s0 + len > n) len = n - s0;

  const float twoPi = 6.2831853f;
  float f   = v.f0;
  float fEnd = (v.f1 > 0.0f) ? v.f1 : v.f0;
  float fk  = powf(fEnd / v.f0, 1.0f / (float)len);   // per-sample glide
  float lpc = v.lp0;
  float lpEnd = (v.lp1 > 0.0f) ? v.lp1 : v.lp0;
  float lk  = (v.lp0 > 0.0f) ? powf(lpEnd / v.lp0, 1.0f / (float)len) : 1.0f;
  float hpW = twoPi * v.hp / sr;
  float hpA = hpW / (1.0f + hpW);
  float dk  = (v.decay > 0.0f) ? expf(-1000.0f / (v.decay * sr)) : 1.0f;
  uint32_t atkN = (uint32_t)(v.atk * sr / 1000.0f);
  uint32_t relN = (uint32_t)(v.rel * sr / 1000.0f);
  if (relN > len) relN = len;

  float ph = 0.0f, ph2 = 0.0f, env = 1.0f, burst = 0.0f;
  float lpA = 0.0f, lpB = 0.0f, hpS = 0.0f;
  const bool tonal = (v.w != Wave::Noise && v.w != Wave::Crackle);

  for (uint32_t i = 0; i < len; i++) {
    float t  = (float)i / sr;
    float fi = f;
    if (v.vib > 0.0f) fi *= 1.0f + v.vib * sinf(twoPi * v.vibHz * t);
    float dt = fi / sr;

    float x;
    if (tonal) {
      x = osc(v.w, ph, dt);
      ph += dt; if (ph >= 1.0f) ph -= 1.0f;
      if (v.detune > 0.0f) {
        float dt2 = dt * (1.0f + v.detune);
        x = 0.5f * (x + osc(v.w, ph2, dt2));
        ph2 += dt2; if (ph2 >= 1.0f) ph2 -= 1.0f;
      }
    } else if (v.w == Wave::Noise) {
      x = frand();
    } else {
      // Crackle: random sharp bursts that die in a couple of ms.
      if (frand() > 0.992f) burst = 1.0f;
      burst *= 0.93f;
      x = frand() * burst;
    }

    if (v.lp0 > 0.0f) {
      // Two cascaded one-poles. Low cutoffs lose energy, so give noise
      // some of it back — a darkening rumble keeps its body.
      float w = twoPi * lpc / sr;
      float a = w / (1.0f + w);
      lpA += a * (x - lpA);
      lpB += a * (lpA - lpB);
      x = lpB;
      if (!tonal) {
        float comp = sqrtf(sr / (3.14159f * lpc));
        x *= (comp < 1.0f) ? 1.0f : (comp > 3.0f ? 3.0f : comp);
      }
      lpc *= lk;
    }
    if (v.hp > 0.0f) { hpS += hpA * (x - hpS); x -= hpS; }

    float e = env;
    env *= dk;
    if (i < atkN) e *= (float)i / (float)atkN;
    if (relN > 0 && len - i <= relN) e *= (float)(len - i) / (float)relN;
    if (v.trm > 0.0f) e *= 1.0f - v.trm * (0.5f + 0.5f * sinf(twoPi * v.trmHz * t));

    mix[s0 + i] += x * e * v.amp;
    f *= fk;
  }
}

// Struck bell / chime: harmonic sine partials, the upper ones quieter
// and shorter-lived. `metal` swaps in inharmonic ratios for clanks and
// tinks.
inline void bell(float f, float at, float dur, float amp, float decay,
                 bool metal = false) {
  static const float harmonic[3][2]   = { {1.0f, 1.0f}, {2.0f, 0.42f}, {3.0f, 0.16f} };
  static const float inharmonic[4][2] = { {1.0f, 1.0f}, {2.76f, 0.62f},
                                          {5.40f, 0.36f}, {8.93f, 0.18f} };
  const float (*p)[2] = metal ? inharmonic : harmonic;
  int np = metal ? 4 : 3;
  for (int k = 0; k < np; k++) {
    float fk = f * p[k][0];
    if (fk > (float)Synth::rate * 0.45f) break;   // above Nyquist — skip
    voice({ .w = Wave::Sine, .f0 = fk, .at = at, .dur = dur,
            .amp = amp * p[k][1], .atk = 1.0f,
            .decay = decay / (1.0f + 0.7f * (float)k), .rel = dur * 0.25f });
  }
}

// Start a new effect `ms` long at sample rate `r`.
inline void beginFx(float ms, int r) {
  using namespace Synth;
  rate = r;
  n = (uint32_t)(ms * (float)r / 1000.0f);
  if (measuring) { if (n > maxN) maxN = n; return; }
  memset(mix, 0, n * sizeof(float));
}

// Finish the current effect: soft-clip, normalize, de-click, and append
// to the arena. `drive` > 1 pushes the soft clipper harder (louder,
// grittier). For a loop, the last `xfadeMs` are crossfaded into the head
// so the loop point is seamless (the effect must have been begun that
// much longer than the loop).
inline Fx endFx(float drive = 1.3f, float loopXfadeMs = 0.0f) {
  using namespace Synth;
  uint32_t xf  = (uint32_t)(loopXfadeMs * (float)rate / 1000.0f);
  uint32_t len = (n > xf) ? n - xf : n;
  Fx fx = { arenaUsed, len, (uint16_t)rate };
  if (measuring) { arenaUsed += len; return fx; }

  for (uint32_t i = 0; i < xf; i++) {
    float w = (float)i / (float)xf;
    mix[i] = mix[i] * w + mix[len + i] * (1.0f - w);
  }

  float peak = 0.0f;
  for (uint32_t i = 0; i < len; i++) {
    float a = fabsf(mix[i]);
    if (a > peak) peak = a;
  }
  float g = (peak > 1e-6f) ? drive / peak : 0.0f;
  uint32_t fadeIn  = xf ? 0 : (uint32_t)(0.002f * (float)rate);
  uint32_t fadeOut = xf ? 0 : (uint32_t)(0.004f * (float)rate);
  for (uint32_t i = 0; i < len; i++) {
    float y = mix[i] * g;
    // Padé tanh approximation — gentle saturation, exact ±1 at |y|=3.
    if (y >  3.0f) y =  3.0f;
    if (y < -3.0f) y = -3.0f;
    y = y * (27.0f + y * y) / (27.0f + 9.0f * y * y);
    if (i < fadeIn)        y *= (float)i / (float)fadeIn;
    if (len - i <= fadeOut) y *= (float)(len - i) / (float)fadeOut;
    arena[arenaUsed + i] = (int8_t)lrintf(y * 124.0f);
  }
  arenaUsed += len;
  return fx;
}

// ---- Effect definitions ---------------------------------------------

inline void renderAll() {
  Synth::rng = 0x1F2E3D4Cu;
  arenaUsed = 0;

  // Laser — detuned saw + square pair diving in pitch, with a short
  // noise crack on the front. Pulse plays it as-is, Beam pitched up.
  beginFx(100, RateHi);
  voice({ .w = Wave::Saw, .f0 = 1700, .f1 = 260, .dur = 95, .amp = 0.7f,
          .atk = 0.5f, .decay = 42, .detune = 0.012f });
  voice({ .w = Wave::Square, .f0 = 850, .f1 = 140, .dur = 90, .amp = 0.4f,
          .atk = 0.5f, .decay = 36, .lp0 = 3500, .lp1 = 900 });
  voice({ .w = Wave::Noise, .dur = 14, .amp = 0.5f, .atk = 0.3f, .decay = 4,
          .rel = 3, .hp = 2200 });
  fxLaser = endFx(1.25f);

  // Military laser — heavier: lower body, a ringing "zing" on top.
  beginFx(125, RateHi);
  voice({ .w = Wave::Square, .f0 = 2300, .f1 = 330, .dur = 115, .amp = 0.55f,
          .atk = 0.5f, .decay = 55, .lp0 = 5000, .lp1 = 1400, .detune = 0.008f });
  voice({ .w = Wave::Saw, .f0 = 1150, .f1 = 190, .dur = 120, .amp = 0.55f,
          .atk = 0.5f, .decay = 60 });
  voice({ .w = Wave::Sine, .f0 = 3300, .f1 = 2500, .dur = 45, .amp = 0.25f,
          .atk = 0.5f, .decay = 14 });
  voice({ .w = Wave::Noise, .dur = 16, .amp = 0.55f, .atk = 0.3f, .decay = 5,
          .rel = 3, .hp = 1800 });
  fxLaserMil = endFx(1.3f);

  // Target hit — a laser striking a hull: a short zap diving off the top
  // over a crackling sizzle, with a little body under it. No bell
  // partials (they rang like glass); the sizzle is what sets it apart
  // from the fire sound.
  beginFx(95, RateHi);
  voice({ .w = Wave::Saw, .f0 = 2400, .f1 = 700, .dur = 60, .amp = 0.55f,
          .atk = 0.3f, .decay = 22, .lp0 = 5000, .lp1 = 1800,
          .detune = 0.02f });
  voice({ .w = Wave::Noise, .dur = 90, .amp = 0.65f, .atk = 0.5f, .decay = 28,
          .rel = 15, .lp0 = 5000, .lp1 = 2000, .hp = 1200, .trmHz = 140,
          .trm = 0.5f });
  voice({ .w = Wave::Square, .f0 = 900, .f1 = 250, .dur = 45, .amp = 0.35f,
          .atk = 0.5f, .decay = 18, .lp0 = 2500 });
  fxHit = endFx(1.4f);

  // Shield hit — a dull punch absorbed by the field: a sine kick diving
  // 520 → 110 Hz, a low detuned buzz that thrums and closes down, and a
  // short dark crack. Everything sits under ~2 kHz — the bright fizz and
  // ringing partials it had before read as breaking glass.
  beginFx(190, RateLo);
  voice({ .w = Wave::Sine, .f0 = 520, .f1 = 110, .dur = 95, .amp = 0.9f,
          .atk = 0.5f, .decay = 35 });
  voice({ .w = Wave::Saw, .f0 = 180, .f1 = 120, .dur = 180, .amp = 0.45f,
          .atk = 2, .decay = 75, .rel = 40, .lp0 = 1800, .lp1 = 450,
          .trmHz = 32, .trm = 0.45f, .detune = 0.03f });
  voice({ .w = Wave::Noise, .dur = 35, .amp = 0.5f, .atk = 0.5f, .decay = 10,
          .rel = 6, .lp0 = 2400, .lp1 = 900 });
  fxShieldHit = endFx(1.5f);

  // Hull hit — heavy thud into a crunch: a deep sine kick, low-passed
  // noise whose cutoff falls away, a little debris crackle and a short
  // structural groan. No bell partials (they ring like glass).
  beginFx(250, RateLo);
  voice({ .w = Wave::Sine, .f0 = 300, .f1 = 60, .dur = 150, .amp = 1.0f,
          .atk = 0.5f, .decay = 55 });
  voice({ .w = Wave::Noise, .dur = 210, .amp = 0.9f, .atk = 0.5f, .decay = 50,
          .rel = 40, .lp0 = 1800, .lp1 = 250 });
  voice({ .w = Wave::Crackle, .at = 10, .dur = 170, .amp = 0.4f, .atk = 2,
          .decay = 60, .rel = 40, .lp0 = 2500 });
  voice({ .w = Wave::Square, .f0 = 140, .f1 = 95, .dur = 170, .amp = 0.3f,
          .atk = 2, .decay = 70, .rel = 40, .lp0 = 700 });
  fxHullHit = endFx(1.8f);

  // Hostile alert — two rising whoops with a little vibrato.
  beginFx(230, RateLo);
  for (int k = 0; k < 2; k++) {
    float at = (float)k * 115.0f;
    voice({ .w = Wave::Saw, .f0 = 450, .f1 = 1050, .at = at, .dur = 95,
            .amp = 0.4f, .atk = 6, .rel = 18, .lp0 = 3000,
            .vibHz = 18, .vib = 0.015f });
    voice({ .w = Wave::Sine, .f0 = 450, .f1 = 1050, .at = at, .dur = 95,
            .amp = 0.5f, .atk = 6, .rel = 18 });
  }
  fxAlert = endFx(1.2f);

  // Missile launch — ignition crack into a falling filtered roar with a
  // faint motor growl underneath.
  beginFx(380, RateLo);
  voice({ .w = Wave::Noise, .dur = 25, .amp = 0.9f, .atk = 0.3f, .decay = 8,
          .rel = 4, .hp = 1200 });
  voice({ .w = Wave::Noise, .at = 8, .dur = 370, .amp = 0.9f, .atk = 15,
          .decay = 160, .rel = 60, .lp0 = 2600, .lp1 = 450 });
  voice({ .w = Wave::Saw, .f0 = 300, .f1 = 160, .at = 20, .dur = 330,
          .amp = 0.25f, .atk = 25, .decay = 150, .rel = 60, .lp0 = 1200 });
  fxMissile = endFx(1.5f);

  // ECM — three warbling zaps with crackle on the attack.
  beginFx(200, RateHi);
  for (int k = 0; k < 3; k++) {
    float at = (float)k * 60.0f;
    voice({ .w = Wave::Square, .f0 = 2200.0f - 400.0f * k, .f1 = 1400.0f - 300.0f * k,
            .at = at, .dur = 50, .amp = 0.45f, .atk = 0.5f, .decay = 25,
            .lp0 = 4500, .vibHz = 90, .vib = 0.08f });
    voice({ .w = Wave::Noise, .at = at, .dur = 25, .amp = 0.3f, .atk = 0.3f,
            .decay = 8, .hp = 2500 });
  }
  fxEcm = endFx(1.3f);

  // Landing chime — rising bell arpeggio (E5, A5, E6).
  beginFx(380, RateLo);
  bell(659, 0,   200, 0.6f, 110);
  bell(880, 95,  200, 0.6f, 110);
  bell(1319, 190, 190, 0.65f, 120);
  fxDock = endFx(1.1f);

  // Launch — engines spooling up: rising roar plus a climbing growl.
  beginFx(430, RateLo);
  voice({ .w = Wave::Noise, .dur = 420, .amp = 0.9f, .atk = 110, .rel = 150,
          .lp0 = 350, .lp1 = 2400, .hp = 90 });
  voice({ .w = Wave::Saw, .f0 = 150, .f1 = 330, .dur = 420, .amp = 0.25f,
          .atk = 140, .rel = 150, .lp0 = 900, .lp1 = 1800 });
  fxLaunch = endFx(1.4f);

  // Hyperspace — rising filtered whoosh with a fluttering tremolo.
  beginFx(620, RateLo);
  voice({ .w = Wave::Noise, .dur = 610, .amp = 0.8f, .atk = 180, .rel = 180,
          .lp0 = 300, .lp1 = 3500, .hp = 120, .trmHz = 9, .trm = 0.35f });
  voice({ .w = Wave::Saw, .f0 = 170, .f1 = 900, .dur = 610, .amp = 0.3f,
          .atk = 220, .rel = 170, .lp0 = 1500, .lp1 = 3500 });
  voice({ .w = Wave::Sine, .f0 = 340, .f1 = 1800, .dur = 610, .amp = 0.3f,
          .atk = 220, .rel = 170 });
  fxWarp = endFx(1.3f);

  // Heat alarm — gliding two-tone siren.
  beginFx(260, RateLo);
  voice({ .w = Wave::Square, .f0 = 1150, .f1 = 850, .dur = 120, .amp = 0.35f,
          .atk = 4, .rel = 20, .lp0 = 3000 });
  voice({ .w = Wave::Sine, .f0 = 1150, .f1 = 850, .dur = 120, .amp = 0.4f,
          .atk = 4, .rel = 20 });
  voice({ .w = Wave::Square, .f0 = 850, .f1 = 1150, .at = 130, .dur = 120,
          .amp = 0.35f, .atk = 4, .rel = 20, .lp0 = 3000 });
  voice({ .w = Wave::Sine, .f0 = 850, .f1 = 1150, .at = 130, .dur = 120,
          .amp = 0.4f, .atk = 4, .rel = 20 });
  fxAlarm = endFx(1.2f);

  // Mission accept — two-note bell (G5, D6). Doubles as the zone
  // re-entry cue and the sound-level preview.
  beginFx(260, RateLo);
  bell(784, 0, 150, 0.6f, 90);
  bell(1175, 90, 170, 0.65f, 100);
  fxAccept = endFx(1.1f);

  // Mission complete — rising four-note bell run.
  beginFx(380, RateLo);
  bell(784, 0, 120, 0.55f, 80);
  bell(988, 75, 120, 0.55f, 80);
  bell(1175, 150, 120, 0.6f, 85);
  bell(1568, 225, 155, 0.65f, 100);
  fxComplete = endFx(1.1f);

  // Rank promotion — brassy fanfare: saws opening up through a sweeping
  // low-pass, which is what reads as a horn's "blat".
  beginFx(500, RateLo);
  {
    const float notes[4] = { 523.0f, 659.0f, 784.0f, 1047.0f };
    for (int k = 0; k < 4; k++) {
      bool last = (k == 3);
      voice({ .w = Wave::Saw, .f0 = notes[k], .at = (float)k * 95.0f,
              .dur = last ? 210.0f : 90.0f, .amp = 0.6f, .atk = 12,
              .decay = last ? 160.0f : 0.0f, .rel = last ? 70.0f : 18.0f,
              .lp0 = 700, .lp1 = 3200, .vibHz = last ? 6.0f : 0.0f,
              .vib = last ? 0.006f : 0.0f, .detune = 0.006f });
    }
  }
  fxPromote = endFx(1.2f);

  // Collision — structural crunch; energy sits at 150–500 Hz where the
  // speaker can actually move air.
  beginFx(260, RateLo);
  voice({ .w = Wave::Noise, .dur = 220, .amp = 1.0f, .atk = 0.5f, .decay = 65,
          .lp0 = 2200, .lp1 = 300 });
  bell(190, 0, 240, 0.7f, 90, /*metal=*/true);
  voice({ .w = Wave::Square, .f0 = 170, .f1 = 100, .dur = 200, .amp = 0.4f,
          .atk = 1, .decay = 80, .lp0 = 900 });
  fxThump = endFx(1.7f);

  // Deny — soft, muffled "uh-uh".
  beginFx(170, RateLo);
  voice({ .w = Wave::Square, .f0 = 300, .f1 = 290, .dur = 60, .amp = 0.45f,
          .atk = 3, .rel = 12, .lp0 = 1300 });
  voice({ .w = Wave::Square, .f0 = 225, .f1 = 215, .at = 80, .dur = 85,
          .amp = 0.45f, .atk = 3, .rel = 20, .lp0 = 1100 });
  fxDeny = endFx(1.1f);

  // Explosion — bright crack, then a rumble whose low-pass cutoff falls
  // from 2.5 kHz to ~200 Hz, with debris crackle scattered through it.
  beginFx(640, RateLo);
  voice({ .w = Wave::Noise, .dur = 40, .amp = 1.0f, .atk = 0.3f, .decay = 14,
          .rel = 6, .hp = 800 });
  voice({ .w = Wave::Noise, .dur = 635, .amp = 1.0f, .atk = 2, .decay = 240,
          .rel = 120, .lp0 = 2500, .lp1 = 200 });
  voice({ .w = Wave::Noise, .at = 15, .dur = 480, .amp = 0.5f, .atk = 20,
          .decay = 190, .rel = 90, .lp0 = 900, .lp1 = 220, .hp = 140 });
  voice({ .w = Wave::Crackle, .at = 30, .dur = 450, .amp = 0.55f, .atk = 5,
          .decay = 180, .rel = 80, .lp0 = 3000 });
  fxBoom = endFx(1.7f);

  // UI tick — tiny soft click for moving through menus.
  beginFx(16, RateHi);
  voice({ .w = Wave::Sine, .f0 = 1900, .f1 = 1500, .dur = 15, .amp = 0.6f,
          .atk = 0.3f, .decay = 4, .rel = 3 });
  fxTick = endFx(1.0f);

  // UI select — short upward blip.
  beginFx(38, RateHi);
  voice({ .w = Wave::Sine, .f0 = 1250, .f1 = 1750, .dur = 35, .amp = 0.6f,
          .atk = 0.5f, .decay = 14, .rel = 6 });
  voice({ .w = Wave::Tri, .f0 = 2500, .dur = 30, .amp = 0.2f, .atk = 0.5f,
          .decay = 9, .rel = 5 });
  fxSelect = endFx(1.0f);

  // UI back — short downward blip.
  beginFx(38, RateHi);
  voice({ .w = Wave::Sine, .f0 = 1500, .f1 = 950, .dur = 35, .amp = 0.6f,
          .atk = 0.5f, .decay = 14, .rel = 6 });
  fxBack = endFx(1.0f);

  // Cash — two quick coin pings for a completed trade.
  beginFx(115, RateHi);
  bell(2093, 0, 60, 0.6f, 28);
  bell(3136, 35, 80, 0.55f, 32);
  fxCash = endFx(1.1f);

  // Lock acquired — crisp double beep.
  beginFx(130, RateHi);
  voice({ .w = Wave::Square, .f0 = 1760, .dur = 42, .amp = 0.35f, .atk = 1,
          .rel = 6, .lp0 = 4200 });
  voice({ .w = Wave::Square, .f0 = 2350, .at = 62, .dur = 50, .amp = 0.35f,
          .atk = 1, .rel = 10, .lp0 = 4200 });
  fxLockOn = endFx(1.0f);

  // Lock failed — nothing in the cone.
  beginFx(65, RateLo);
  voice({ .w = Wave::Square, .f0 = 330, .f1 = 290, .dur = 60, .amp = 0.4f,
          .atk = 2, .rel = 14, .lp0 = 1300 });
  fxLockFail = endFx(1.0f);

  // Incoming missile — one sharp beep; the caller repeats it faster as
  // the missile closes.
  beginFx(50, RateHi);
  voice({ .w = Wave::Square, .f0 = 1480, .dur = 45, .amp = 0.4f, .atk = 1,
          .rel = 8, .lp0 = 3600 });
  voice({ .w = Wave::Sine, .f0 = 2960, .dur = 45, .amp = 0.15f, .atk = 1,
          .rel = 8 });
  fxIncoming = endFx(1.1f);

  // Shield down — power-failure sweep.
  beginFx(320, RateLo);
  voice({ .w = Wave::Saw, .f0 = 1400, .f1 = 130, .dur = 300, .amp = 0.6f,
          .atk = 2, .rel = 70, .lp0 = 3800, .lp1 = 500, .vibHz = 25, .vib = 0.03f });
  voice({ .w = Wave::Noise, .dur = 200, .amp = 0.3f, .atk = 1, .decay = 70,
          .lp0 = 3000, .lp1 = 300 });
  fxShieldDown = endFx(1.3f);

  // Hull critical — urgent two-pulse klaxon.
  beginFx(260, RateLo);
  for (int k = 0; k < 2; k++) {
    float at = (float)k * 135.0f;
    voice({ .w = Wave::Square, .f0 = 620, .f1 = 560, .at = at, .dur = 105,
            .amp = 0.4f, .atk = 4, .rel = 15, .lp0 = 2400 });
    voice({ .w = Wave::Sine, .f0 = 620, .f1 = 560, .at = at, .dur = 105,
            .amp = 0.35f, .atk = 4, .rel = 15 });
  }
  fxHullCrit = endFx(1.25f);

  // Leaving the system — soft descending two-note bell.
  beginFx(280, RateLo);
  bell(880, 0, 150, 0.55f, 90);
  bell(587, 110, 165, 0.55f, 110);
  fxZoneOut = endFx(1.0f);
}

// Build the arena: measure, allocate exactly, render. Returns false if
// the allocation failed (sound stays silent; the game runs on).
inline bool build() {
  if (arena) return true;
  Synth::measuring = true;
  Synth::maxN = 0;
  renderAll();
  Synth::measuring = false;
  uint32_t total = arenaUsed;

  arena = (int8_t*)malloc(total);
  if (!arena) return false;
  Synth::mix = (float*)malloc(Synth::maxN * sizeof(float));
  if (!Synth::mix) { free(arena); arena = nullptr; return false; }
  renderAll();
  free(Synth::mix);
  Synth::mix = nullptr;
  Serial.printf("[audio] SFX arena %u bytes\n", (unsigned)arenaUsed);
  return true;
}

inline void applyLevel() {
  M5Cardputer.Speaker.setVolume(LevelVolume[level]);
}

inline bool begin() {
  M5Cardputer.Speaker.begin();
  applyLevel();
  applyChannelTrims();
  return build();
}

inline void stopAll() { M5Cardputer.Speaker.stop(); }

// Step OFF -> LOW -> MED -> HIGH -> OFF.
inline void cycleLevel() {
  level = (uint8_t)((level + 1) % LevelCount);
  applyLevel();
  if (level == LevelOff) stopAll();
}

// Step the level down (-1) or up (+1) without wrapping — LEFT / RIGHT
// on the SOUND row. Returns false if already at that end.
inline bool stepLevel(int delta) {
  int l = (int)level + delta;
  if (l < 0 || l >= (int)LevelCount) return false;
  level = (uint8_t)l;
  applyLevel();
  if (level == LevelOff) stopAll();
  return true;
}

// Random pitch factor in [1 - pct, 1 + pct] — small per-play variation
// keeps repeated sounds from sounding mechanical.
inline float jitter(float pct) {
  float u = (float)(esp_random() & 0xFFFFu) / 32767.5f - 1.0f;
  return 1.0f + pct * u;
}

inline void play(const Fx& fx, int ch, float pitch = 1.0f) {
  if (level == LevelOff || !arena || fx.len == 0) return;
  M5Cardputer.Speaker.playRaw(&arena[fx.off], (size_t)fx.len,
                              (uint32_t)((float)fx.rate * pitch + 0.5f),
                              false, 1, ch, true);
}

// ---- Engine: electric drive whine ---------------------------------

// The drive sounds like an electric motor: a clean whine whose pitch
// rises smoothly with speed, an inverter overtone 2.5x above it, and a
// soft hum an octave below for body — no noise (noise is what makes a
// drive read as a vacuum cleaner or a combustion engine).
//
// A looped sample can only change pitch in steps, so the drive is
// synthesized live: one 40 ms block at a time, with pitch and loudness
// gliding sample-by-sample inside each block, queued on ChEngine. The
// speaker holds two blocks (playing + queued); a ring of three lets the
// next one be written without touching either.
constexpr int   EngRate  = 16000;   // Hz
constexpr int   EngBlock = 640;     // samples = 40 ms
constexpr int   EngBufs  = 3;
constexpr float EngIdleHz = 160.0f; // motor tone at zero speed
constexpr float EngTopHz  = 820.0f; // motor tone at full speed

inline int8_t engBuf[EngBufs][EngBlock];
inline int    engNext  = 0;
inline bool   engOn    = false;
inline float  engFreq  = EngIdleHz;   // current motor tone, Hz
inline float  engAmp   = 0.0f;        // current loudness 0..1
inline float  engPhA = 0.0f, engPhB = 0.0f, engPhC = 0.0f;   // cycles, 0..1
inline float  engWave[257];           // one cycle of the motor tone (+ wrap)
inline float  engSine[257];
inline bool   engReady = false;

inline void engineTables() {
  // Motor tone: fundamental with a quickly falling set of harmonics —
  // bright enough to read as a whine, smooth enough not to buzz.
  static const float h[5] = { 1.0f, 0.30f, 0.12f, 0.05f, 0.02f };
  float peak = 0.0f;
  for (int i = 0; i <= 256; i++) {
    float t = (float)i / 256.0f * 6.2831853f;
    float v = 0.0f;
    for (int k = 0; k < 5; k++) v += h[k] * sinf((float)(k + 1) * t);
    engWave[i] = v;
    engSine[i] = sinf(t);
    if (fabsf(v) > peak) peak = fabsf(v);
  }
  for (float& v : engWave) v /= peak;
  engReady = true;
}

inline float engLookup(const float* tbl, float ph) {
  float x = ph * 256.0f;
  int   i = (int)x;
  float f = x - (float)i;
  return tbl[i] + (tbl[i + 1] - tbl[i]) * f;
}

// Call once per frame. `level01` = loudness (0 = off), `speed01` drives
// the pitch; both 0..1.
inline void engine(float level01, float speed01) {
  auto& spk = M5Cardputer.Speaker;
  if (level == LevelOff) level01 = 0.0f;
  if (!engReady) engineTables();
  float targetHz = EngIdleHz + (EngTopHz - EngIdleHz) * speed01;

  if (!engOn && level01 <= 0.0f) { engFreq = targetHz; return; }
  if (engOn && level01 <= 0.0f && engAmp < 0.003f) {
    spk.stop(ChEngine);
    engOn = false;
    engAmp = 0.0f;
    return;
  }

  size_t queued = spk.isPlaying(ChEngine);
  if (queued >= 2) return;   // playing + queued — nothing to do yet

  // Glide toward the targets (~0.15 s time constant), linearly within
  // the block so there are no steps.
  float f0 = engFreq, a0 = engAmp;
  float f1 = f0 + (targetHz - f0) * 0.25f;
  float a1 = a0 + (level01  - a0) * 0.25f;
  if (level01 <= 0.0f && a1 < 0.003f) a1 = 0.0f;
  int8_t* out = engBuf[engNext];
  const float invN = 1.0f / (float)EngBlock;
  for (int i = 0; i < EngBlock; i++) {
    float t = (float)i * invN;
    float f = f0 + (f1 - f0) * t;
    float a = a0 + (a1 - a0) * t;
    engPhA += f / (float)EngRate;          if (engPhA >= 1.0f) engPhA -= 1.0f;
    engPhB += f * 2.5f / (float)EngRate;   if (engPhB >= 1.0f) engPhB -= 1.0f;
    engPhC += f * 0.5f / (float)EngRate;   if (engPhC >= 1.0f) engPhC -= 1.0f;
    float x = 0.62f * engLookup(engWave, engPhA)
            + 0.10f * engLookup(engSine, engPhB)
            + 0.32f * engLookup(engSine, engPhC);
    out[i] = (int8_t)lrintf(x * a * 105.0f);
  }
  engFreq = f1;
  engAmp  = a1;

  spk.playRaw(out, EngBlock, EngRate, false, 1, ChEngine, queued == 0);
  engNext = (engNext + 1) % EngBufs;
  engOn = true;
}

// ---- Named effects --------------------------------------------------

inline void laserZap(uint8_t tier) {
  if (tier >= 2) play(fxLaserMil, ChCombat, jitter(0.03f));
  else           play(fxLaser,    ChCombat, (tier == 1 ? 1.15f : 1.0f) * jitter(0.04f));
}
inline void hitTarget()      { play(fxHit,        ChHit,  jitter(0.05f)); }
inline void shieldHit()      { play(fxShieldHit,  ChHit,  jitter(0.04f)); }
inline void hullHit()        { play(fxHullHit,    ChHit,  jitter(0.05f)); }
inline void hostileAlert()   { play(fxAlert,      ChWarn); }
inline void missileLaunch()  { play(fxMissile,    ChBoom, jitter(0.03f)); }
inline void ecmBurst()       { play(fxEcm,        ChBoom); }
inline void collisionThump() { play(fxThump,      ChBoom, jitter(0.05f)); }
inline void explosion()      { play(fxBoom,       ChBoom, jitter(0.06f)); }
inline void dockChime()      { play(fxDock,       ChEvent); }
inline void launchRoar()     { play(fxLaunch,     ChEvent); }
inline void warpWhoosh()     { play(fxWarp,       ChEvent); }
inline void warpArrive()     { play(fxThump,      ChEvent, 0.8f); }
inline void alarm()          { play(fxAlarm,      ChWarn); }
inline void missionAccept()  { play(fxAccept,     ChEvent); }
inline void missionComplete(){ play(fxComplete,   ChEvent); }
inline void rankPromote()    { play(fxPromote,    ChEvent); }
inline void zoneOut()        { play(fxZoneOut,    ChEvent); }
inline void zoneIn()         { play(fxAccept,     ChEvent, 0.9f); }
inline void deny()           { play(fxDeny,       ChUI); }
inline void uiMove()         { play(fxTick,       ChUI); }
inline void uiSelect()       { play(fxSelect,     ChUI); }
inline void uiBack()         { play(fxBack,       ChUI); }
inline void cash()           { play(fxCash,       ChUI, jitter(0.02f)); }
inline void lockOn()         { play(fxLockOn,     ChWarn); }
inline void lockFail()       { play(fxLockFail,   ChWarn); }
inline void incomingBeep()   { play(fxIncoming,   ChWarn); }
inline void shieldDown()     { play(fxShieldDown, ChWarn); }
inline void hullCritical()   { play(fxHullCrit,   ChWarn); }

} // namespace Audio
