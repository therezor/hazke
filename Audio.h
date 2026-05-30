#pragma once
#include <M5Cardputer.h>
#include <stdint.h>

// Refit R28: PWM sound effects.
//
// Wraps M5Cardputer's speaker (M5Unified `M5Speaker_Class`, which queues
// generated tones into a sample buffer — so multiple sequential
// `tone(freq, ms)` calls play back-to-back). Each named SFX function
// stamps out a small sequence and returns immediately; the buffer plays
// in the background. No per-frame tick is required.
//
// All effects respect `Audio::mute`. Volume is held at a comfortable
// fraction of the Cardputer's beeper range so the bridge sounds
// punctuate the action without dominating the cockpit ambience.

namespace Audio {

inline bool    muted   = false;
inline uint8_t volume  = 80;          // 0..255 (M5Speaker range)

inline void begin() {
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(volume);
}

inline void setMuted(bool m) {
  muted = m;
  M5Cardputer.Speaker.setVolume(m ? 0 : volume);
}

inline void toggleMute() { setMuted(!muted); }

// Single tone — convenience wrapper. Duration in ms.
inline void tone(uint32_t freq, uint32_t ms) {
  if (muted) return;
  M5Cardputer.Speaker.tone((float)freq, (uint32_t)ms);
}

// ---- Named effects --------------------------------------------------

// Quick high-pitched chirp on laser fire. Pitch shifts up by tier so
// the Pulse / Beam / Military upgrades sound progressively meaner.
inline void laserZap(uint8_t tier) {
  if (muted) return;
  uint32_t f = (tier == 0) ? 1500
             : (tier == 1) ? 1850
                           : 2200;
  M5Cardputer.Speaker.tone((float)f, 22);
}

// Short low thump when something hits the player's shields. Calm enough
// not to overlap rapidly during sustained laser fire — pirates have a
// 1.25-s cooldown on R20's lasers so this rarely doubles up.
inline void shieldHit() {
  if (muted) return;
  M5Cardputer.Speaker.tone(140.0f, 70);
  M5Cardputer.Speaker.tone(90.0f,  50);
}

// Descending growl for missile launch. Sounds heavy + threatening.
inline void missileLaunch() {
  if (muted) return;
  M5Cardputer.Speaker.tone(620.0f, 35);
  M5Cardputer.Speaker.tone(460.0f, 35);
  M5Cardputer.Speaker.tone(330.0f, 35);
  M5Cardputer.Speaker.tone(240.0f, 50);
}

// Sharp double-burst when ECM detonates incoming missiles.
inline void ecmBurst() {
  if (muted) return;
  M5Cardputer.Speaker.tone(2600.0f, 35);
  M5Cardputer.Speaker.tone(1800.0f, 35);
  M5Cardputer.Speaker.tone(2600.0f, 45);
}

// Ascending three-tone chime on a successful station / planet dock.
inline void dockChime() {
  if (muted) return;
  M5Cardputer.Speaker.tone(600.0f,  90);
  M5Cardputer.Speaker.tone(750.0f,  90);
  M5Cardputer.Speaker.tone(900.0f, 130);
}

// Long rising sweep when warp / hyperspace fires.
inline void warpWhoosh() {
  if (muted) return;
  M5Cardputer.Speaker.tone(140.0f,  40);
  M5Cardputer.Speaker.tone(240.0f,  40);
  M5Cardputer.Speaker.tone(380.0f,  45);
  M5Cardputer.Speaker.tone(560.0f,  50);
  M5Cardputer.Speaker.tone(820.0f,  60);
  M5Cardputer.Speaker.tone(1100.0f, 80);
}

// Two-tone siren for HOSTILE / INCOMING moments. Caller decides how
// often to ring it — alarms call this from the HUD precedence layer.
inline void alarm() {
  if (muted) return;
  M5Cardputer.Speaker.tone(1400.0f, 90);
  M5Cardputer.Speaker.tone( 850.0f, 90);
}

// Two-tone bell when a contract or arc step is accepted.
inline void missionAccept() {
  if (muted) return;
  M5Cardputer.Speaker.tone(700.0f,  80);
  M5Cardputer.Speaker.tone(1000.0f, 90);
}

// Three-tone triad on contract payout.
inline void missionComplete() {
  if (muted) return;
  M5Cardputer.Speaker.tone(700.0f,  90);
  M5Cardputer.Speaker.tone(1000.0f, 90);
  M5Cardputer.Speaker.tone(1300.0f, 130);
}

// Heroic four-note fanfare on rank promotion.
inline void rankPromote() {
  if (muted) return;
  M5Cardputer.Speaker.tone(800.0f,  80);
  M5Cardputer.Speaker.tone(1000.0f, 80);
  M5Cardputer.Speaker.tone(1200.0f, 80);
  M5Cardputer.Speaker.tone(1500.0f, 140);
}

// Heavy metal-on-metal clang when the player's hull rams another ship.
// Two short low blasts give it the percussive feel of structural
// damage, distinct from the softer shieldHit() thump.
inline void collisionThump() {
  if (muted) return;
  M5Cardputer.Speaker.tone(180.0f, 60);
  M5Cardputer.Speaker.tone(110.0f, 80);
  M5Cardputer.Speaker.tone( 70.0f, 90);
}

// Soft "no" buzz for refused actions (low credits, slot full, etc.).
inline void deny() {
  if (muted) return;
  M5Cardputer.Speaker.tone(220.0f, 60);
  M5Cardputer.Speaker.tone(180.0f, 80);
}

} // namespace Audio
