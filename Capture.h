#pragma once
#include <M5Cardputer.h>
#include "Config.h"

// Frame capture over the USB serial console, for recording gameplay
// clips and screenshots from a host script (see the cap / keys / step /
// rec / shot console commands in hazke.ino).
//
// Lockstep mode pauses the game between console commands and advances
// it with a fixed time step, so a recording plays back at exactly
// 1 / StepDt frames per second however long each frame takes to send.
//
// Frame wire format: the line "FRM 240 135\n", then the RGB565 pixels
// (M5GFX byte order: high byte first) packed PackBits-style in 16-bit
// units. A header byte h < 128 is followed by h+1 literal pixels; h >= 128
// is followed by one pixel repeated h-126 times. The host stops after
// 240*135 pixels.

namespace Capture {

constexpr float StepDt = 1.0f / 25.0f;

inline bool lockstep = false;
inline int  pending  = 0;      // lockstep frames left to run
inline bool recording = false; // dump each lockstep frame as it finishes
inline M5Canvas* lastFrame = nullptr;

inline void sendFrame(M5Canvas& c) {
  const uint16_t* px = (const uint16_t*)c.getBuffer();
  if (!px) return;
  const int n = Config::ScreenW * Config::ScreenH;

  // Block (briefly) on a full USB buffer instead of dropping bytes.
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(250);
#endif
  Serial.printf("FRM %d %d\n", Config::ScreenW, Config::ScreenH);

  static uint8_t out[1024];
  int o = 0;
  auto flush = [&] { Serial.write(out, o); o = 0; };
  auto put16 = [&](uint16_t v) {
    out[o++] = ((const uint8_t*)&v)[0];
    out[o++] = ((const uint8_t*)&v)[1];
  };

  int i = 0;
  while (i < n) {
    int run = 1;
    while (i + run < n && run < 129 && px[i + run] == px[i]) run++;
    if (o > (int)sizeof(out) - 260) flush();
    if (run >= 2) {
      out[o++] = (uint8_t)(run + 126);
      put16(px[i]);
      i += run;
      continue;
    }
    // Literal span: up to 128 pixels, stopping where a run of 2+ starts.
    int len = 1;
    while (i + len < n && len < 128 &&
           !(i + len + 1 < n && px[i + len] == px[i + len + 1])) len++;
    out[o++] = (uint8_t)(len - 1);
    for (int k = 0; k < len; k++) put16(px[i + k]);
    i += len;
  }
  flush();
  Serial.flush();
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
}

} // namespace Capture
