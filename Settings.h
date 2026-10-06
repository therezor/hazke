#pragma once
#include <Preferences.h>
#include <stdint.h>
#include "Audio.h"
#include "Gyro.h"

// Device-wide options (SOUND, GYRO), kept in NVS so they survive reboots
// and apply to every commander. They live outside the save slots on
// purpose: the save format stays untouched.

namespace Settings {

constexpr const char* Ns = "hazke";

// Values last written to (or read from) NVS.
inline uint8_t storedSound = 0xFF;
inline uint8_t storedGyro  = 0xFF;

// Boot: read stored values over the defaults. Call before Audio::begin()
// so the speaker starts at the stored level.
inline void load() {
  Preferences p;
  // Read-only open fails on first boot (no namespace yet): keep defaults.
  if (p.begin(Ns, /*readOnly=*/true)) {
    uint8_t s = p.getUChar("sound", Audio::level);
    uint8_t g = p.getUChar("gyro",  Gyro::mode);
    p.end();
    if (s < Audio::LevelCount) Audio::level = s;
    if (g < Gyro::ModeCount)   Gyro::mode   = g;
  }
  storedSound = Audio::level;
  storedGyro  = Gyro::mode;
}

// Once per frame: write whatever changed since the last write.
inline void sync() {
  if (Audio::level == storedSound && Gyro::mode == storedGyro) return;
  Preferences p;
  if (!p.begin(Ns, /*readOnly=*/false)) return;
  p.putUChar("sound", Audio::level);
  p.putUChar("gyro",  Gyro::mode);
  p.end();
  storedSound = Audio::level;
  storedGyro  = Gyro::mode;
}

} // namespace Settings
