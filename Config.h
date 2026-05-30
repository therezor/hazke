#pragma once

namespace Config {
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
}
