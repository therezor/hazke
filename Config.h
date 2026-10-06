#pragma once
#include <stdint.h>

namespace Config {
  // Release version — update here only; the title badge and About screen
  // both read from this so there's a single source of truth.
  constexpr const char* VersionTag = "v1.2";

  // On-disk save format version. Bump ONLY when the save payload layout
  // changes, together with a new frozen SaveDataVn struct and an
  // upgradeVn-1toVn transformer in SaveFormat.h — never edit a released
  // payload struct in place.
  constexpr uint16_t SaveFormatVersion = 1;

  constexpr int ScreenW = 240;
  constexpr int ScreenH = 135;

  // Viewport (3D world view)
  constexpr int ViewX = 0;
  constexpr int ViewY = 0;
  constexpr int ViewW = 240;
  constexpr int ViewH = 92;

  // HUD strip (bars + radar)
  constexpr int HudY = 93;
  constexpr int HudH = 31;

  // Footer strip (credits + battery)
  constexpr int FooterY = 125;
  constexpr int FooterH = 10;

  // Frame target
  constexpr uint32_t FrameUs = 16000;

  // Double-buffered display needs a second 64.8 KB frame buffer. It is
  // only kept if at least this much heap is left afterwards for the SD /
  // LittleFS mounts the save menu brings up later.
  constexpr uint32_t HeapReserve = 40 * 1024;
}

// Set to 1 to log FPS, per-phase frame times and heap to Serial every
// 2 s (flight: update / world / HUD / present).
#ifndef HAZKE_PROFILE
#define HAZKE_PROFILE 0
#endif

// Set to 0 to force the single-buffered, synchronous display push (rules
// the DMA double buffer out if the screen ever misbehaves).
#ifndef HAZKE_DOUBLE_BUFFER
#define HAZKE_DOUBLE_BUFFER 1
#endif
