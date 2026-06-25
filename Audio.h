#pragma once
#include <M5Cardputer.h>
#include <math.h>
#include <stdint.h>

// Refit R28 / reworked R32: PCM sound effects.
//
// All SFX are pre-rendered once at boot into an 8-bit signed PCM arena
// and each effect plays with a single Speaker.playRaw() call. The old
// implementation stacked sequential Speaker.tone() calls — but
// M5Unified's tone() defaults to stop_current_sound=true on an
// auto-picked channel, so a "melody" actually played its first three
// notes as one chord and dropped the rest. Pre-rendered PCM gives real
// frequency sweeps, amplitude envelopes and noise, and never blocks the
// game loop.
//
// Three mixer channels keep effects from cutting each other off:
//   ChCombat — lasers, thumps, missiles (rapid-fire, replaceable)
//   ChEvent  — chimes, fanfares, alarms, warp (should play out)
//   ChHit    — impact feedback (target hits / player hits), separate so
//              landing a shot doesn't swallow the fire sound
//
// All effects respect `Audio::muted`.

namespace Audio {

inline bool    muted   = false;
inline uint8_t volume  = 80;          // 0..255 (M5Speaker range)

constexpr int SfxRate  = 11025;       // Hz, mono, int8
constexpr int ChCombat = 0;
constexpr int ChEvent  = 1;
constexpr int ChHit    = 2;

enum class Wave : uint8_t { Sine, Square, Saw, Triangle, Noise };

struct Fx { uint16_t off; uint16_t len; };

inline int8_t arena[40960];
inline int    arenaUsed = 0;

inline Fx fxLaser[3];
inline Fx fxMissile, fxEcm, fxDock, fxWarp, fxAlarm;
inline Fx fxAccept, fxComplete, fxPromote, fxThump, fxDeny, fxBoom;
inline Fx fxHitTarget, fxPlayerHit, fxAlert;

// Append one swept, enveloped segment to the arena. Frequency runs
// f0→f1 Hz and amplitude a0→a1 (0..1) linearly across `ms`. Noise is
// sample-and-hold updated at the swept frequency, so high f = hiss and
// low f = rumble.
inline void seg(Wave w, float f0, float f1, int ms, float a0, float a1) {
  static uint32_t ns    = 0x1F2E3D4Cu;
  static float    nheld = 0.0f;
  int n = ms * SfxRate / 1000;
  float phase = 0.0f;
  for (int i = 0; i < n && arenaUsed < (int)sizeof(arena); i++) {
    float t = (n > 1) ? (float)i / (float)(n - 1) : 0.0f;
    float f = f0 + (f1 - f0) * t;
    float a = a0 + (a1 - a0) * t;
    phase += f / (float)SfxRate;
    bool wrapped = (phase >= 1.0f);
    if (wrapped) phase -= (float)(int)phase;
    float v = 0.0f;
    switch (w) {
      case Wave::Sine:     v = sinf(phase * 6.2831853f); break;
      case Wave::Square:   v = (phase < 0.5f) ? 1.0f : -1.0f; break;
      case Wave::Saw:      v = 2.0f * phase - 1.0f; break;
      case Wave::Triangle: v = (phase < 0.5f) ? (4.0f * phase - 1.0f)
                                              : (3.0f - 4.0f * phase); break;
      case Wave::Noise:
        if (wrapped || i == 0) {
          ns = ns * 1664525u + 1013904223u;
          nheld = (float)((ns >> 8) & 0xFFFFu) / 32768.0f - 1.0f;
        }
        v = nheld;
        break;
    }
    arena[arenaUsed++] = (int8_t)(v * a * 120.0f);
  }
}

// Short articulation gap between notes.
inline void rest(int ms) { seg(Wave::Sine, 100.0f, 100.0f, ms, 0.0f, 0.0f); }

// Close out an effect that started at arena offset `o`.
inline Fx fxFrom(int o) { return { (uint16_t)o, (uint16_t)(arenaUsed - o) }; }

inline void renderAll() {
  arenaUsed = 0;
  int o;

  // Laser — sharp descending "pew"; start pitch climbs per tier so the
  // Pulse / Beam / Military upgrades sound progressively meaner.
  for (int t = 0; t < 3; t++) {
    o = arenaUsed;
    float f0 = 1500.0f + (float)t * 350.0f;
    seg(Wave::Square, f0, f0 * 0.18f, 70, 0.8f, 0.03f);
    fxLaser[t] = fxFrom(o);
  }

  // Target hit — bright confirmation tick when the player's laser
  // connects. Short and trebly so it stays distinct from the fire pew
  // playing on the combat channel.
  o = arenaUsed;
  seg(Wave::Noise,  3000.0f, 1400.0f, 30, 0.8f, 0.5f);
  seg(Wave::Square, 1000.0f,  550.0f, 35, 0.6f, 0.05f);
  fxHitTarget = fxFrom(o);

  // Player hit — punchy mid-range crunch. The old version was a low
  // sine thump whose energy sat below what the Cardputer's tiny
  // speaker can reproduce, so getting shot was nearly silent.
  o = arenaUsed;
  seg(Wave::Noise,  2000.0f, 500.0f,  50, 1.0f, 0.6f);
  seg(Wave::Square,  380.0f, 140.0f, 100, 0.85f, 0.05f);
  fxPlayerHit = fxFrom(o);

  // Hostile alert — double rising whoop when a ship turns on the
  // player and starts its attack run.
  o = arenaUsed;
  seg(Wave::Saw, 400.0f, 1000.0f, 80, 0.55f, 0.6f);
  rest(35);
  seg(Wave::Saw, 400.0f, 1000.0f, 80, 0.6f, 0.45f);
  fxAlert = fxFrom(o);

  // Missile launch — hissing ignition into a falling growl.
  o = arenaUsed;
  seg(Wave::Noise, 1200.0f, 500.0f, 70, 0.55f, 0.30f);
  seg(Wave::Saw,    560.0f, 150.0f, 210, 0.75f, 0.08f);
  fxMissile = fxFrom(o);

  // ECM — bright triple zap.
  o = arenaUsed;
  seg(Wave::Square, 2600.0f, 2200.0f, 40, 0.6f, 0.4f);
  rest(15);
  seg(Wave::Square, 1800.0f, 1500.0f, 40, 0.6f, 0.4f);
  rest(15);
  seg(Wave::Square, 2600.0f, 2900.0f, 55, 0.6f, 0.1f);
  fxEcm = fxFrom(o);

  // Dock chime — three soft rising sines, each ringing down.
  o = arenaUsed;
  seg(Wave::Sine, 600.0f, 600.0f,  85, 0.70f, 0.25f);
  rest(12);
  seg(Wave::Sine, 750.0f, 750.0f,  85, 0.70f, 0.25f);
  rest(12);
  seg(Wave::Sine, 900.0f, 900.0f, 140, 0.75f, 0.0f);
  fxDock = fxFrom(o);

  // Warp — long rising whoosh that swells as it climbs.
  o = arenaUsed;
  seg(Wave::Triangle, 130.0f, 1150.0f, 420, 0.30f, 0.85f);
  fxWarp = fxFrom(o);

  // Alarm — two-tone siren for HOSTILE / INCOMING moments. Caller
  // decides how often to ring it.
  o = arenaUsed;
  seg(Wave::Square, 1300.0f, 1300.0f, 90, 0.5f, 0.5f);
  seg(Wave::Square,  880.0f,  880.0f, 90, 0.5f, 0.5f);
  fxAlarm = fxFrom(o);

  // Mission accept — two-tone bell.
  o = arenaUsed;
  seg(Wave::Sine,  700.0f,  700.0f,  80, 0.7f, 0.35f);
  rest(12);
  seg(Wave::Sine, 1000.0f, 1000.0f, 120, 0.75f, 0.0f);
  fxAccept = fxFrom(o);

  // Mission complete — rising triad on contract payout.
  o = arenaUsed;
  seg(Wave::Sine,  700.0f,  700.0f,  90, 0.7f, 0.35f);
  rest(12);
  seg(Wave::Sine, 1000.0f, 1000.0f,  90, 0.7f, 0.35f);
  rest(12);
  seg(Wave::Sine, 1300.0f, 1300.0f, 150, 0.8f, 0.0f);
  fxComplete = fxFrom(o);

  // Rank promotion — heroic four-note fanfare.
  o = arenaUsed;
  seg(Wave::Triangle,  800.0f,  800.0f, 80, 0.8f, 0.5f);
  rest(10);
  seg(Wave::Triangle, 1000.0f, 1000.0f, 80, 0.8f, 0.5f);
  rest(10);
  seg(Wave::Triangle, 1200.0f, 1200.0f, 80, 0.8f, 0.5f);
  rest(10);
  seg(Wave::Triangle, 1500.0f, 1500.0f, 170, 0.9f, 0.0f);
  fxPromote = fxFrom(o);

  // Hull collision — metallic crack into a low structural groan,
  // distinct from the softer shield thump.
  o = arenaUsed;
  seg(Wave::Noise, 400.0f, 120.0f,  90, 1.0f, 0.5f);
  seg(Wave::Sine,   90.0f,  45.0f, 140, 0.9f, 0.0f);
  fxThump = fxFrom(o);

  // Deny — soft "no" double-buzz for refused actions.
  o = arenaUsed;
  seg(Wave::Square, 210.0f, 210.0f, 70, 0.45f, 0.45f);
  rest(10);
  seg(Wave::Square, 165.0f, 165.0f, 90, 0.45f, 0.1f);
  fxDeny = fxFrom(o);

  // Explosion — crack of the blast into a long decaying rumble. Plays
  // on ship kills and the player's own death.
  o = arenaUsed;
  seg(Wave::Noise, 2400.0f, 900.0f,  90, 0.95f, 0.80f);
  seg(Wave::Noise,  900.0f,  60.0f, 320, 0.80f, 0.0f);
  fxBoom = fxFrom(o);
}

inline void begin() {
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(volume);
  renderAll();
}

inline void setMuted(bool m) {
  muted = m;
  M5Cardputer.Speaker.setVolume(m ? 0 : volume);
}

inline void toggleMute() { setMuted(!muted); }

inline void play(const Fx& fx, int ch) {
  if (muted || fx.len == 0) return;
  M5Cardputer.Speaker.playRaw(&arena[fx.off], (size_t)fx.len,
                              (uint32_t)SfxRate, false, 1, ch, true);
}

// ---- Named effects --------------------------------------------------

inline void laserZap(uint8_t tier)  { play(fxLaser[tier > 2 ? 2 : tier], ChCombat); }
inline void hitTarget()             { play(fxHitTarget, ChHit); }
inline void playerHit()             { play(fxPlayerHit, ChHit); }
inline void hostileAlert()          { play(fxAlert,    ChEvent); }
inline void missileLaunch()         { play(fxMissile,  ChCombat); }
inline void ecmBurst()              { play(fxEcm,      ChCombat); }
inline void collisionThump()        { play(fxThump,    ChCombat); }
inline void explosion()             { play(fxBoom,     ChCombat); }
inline void dockChime()             { play(fxDock,     ChEvent); }
inline void warpWhoosh()            { play(fxWarp,     ChEvent); }
inline void alarm()                 { play(fxAlarm,    ChEvent); }
inline void missionAccept()         { play(fxAccept,   ChEvent); }
inline void missionComplete()       { play(fxComplete, ChEvent); }
inline void rankPromote()           { play(fxPromote,  ChEvent); }
inline void deny()                  { play(fxDeny,     ChEvent); }

} // namespace Audio
