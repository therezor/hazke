#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "Audio.h"

// Screen capture to the microSD card.
//
// Ctrl+Space (handled in the main loop) snaps the fully-rendered frame and
// writes it to /hazke/shotNNNN.bmp as a 24-bit Windows bitmap — readable on
// any computer with no conversion. The capture reads the offscreen canvas
// after the frame is drawn but before the confirmation toast is overlaid, so
// the toast never ends up in the saved image.
//
// The M5Cardputer microSD slot lives on a shared SPI bus:
//   CLK 40 · MISO 39 · MOSI 14 · CS 12
// We bring it up lazily on the first capture so boot stays fast and a
// card-less unit pays nothing until the player actually presses the hotkey.

namespace Screenshot {

constexpr int PinSCK  = 40;
constexpr int PinMISO = 39;
constexpr int PinMOSI = 14;
constexpr int PinCS   = 12;

inline bool  sdReady      = false;
inline int   nextIndex    = 0;
inline bool  indexScanned = false;

// Confirmation toast shown for a moment after each attempt.
inline char  toastMsg[32] = {0};
inline float toastTimer   = 0.0f;
inline bool  toastOk      = false;
constexpr float ToastTime = 1.8f;

inline bool ensureSD() {
  if (sdReady) return true;
  SPI.begin(PinSCK, PinMISO, PinMOSI, PinCS);
  if (!SD.begin(PinCS, SPI, 25000000)) {
    sdReady = false;
    return false;
  }
  SD.mkdir("/hazke");
  sdReady = true;
  return true;
}

inline void makePath(char* out, size_t n, int idx) {
  snprintf(out, n, "/hazke/shot%04d.bmp", idx);
}

// Find the first unused filename once per boot so reboots don't clobber
// earlier captures.
inline void scanIndex() {
  if (indexScanned) return;
  char path[32];
  int i = 0;
  for (; i < 10000; i++) {
    makePath(path, sizeof(path), i);
    if (!SD.exists(path)) break;
  }
  nextIndex = i;
  indexScanned = true;
}

// Little-endian field writers for the BMP headers.
inline void wr16(File& f, uint16_t v) {
  uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
  f.write(b, 2);
}
inline void wr32(File& f, uint32_t v) {
  uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8),
                   (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
  f.write(b, 4);
}

inline bool writeBMP(M5Canvas& cv, const char* path) {
  const int W = Config::ScreenW;
  const int H = Config::ScreenH;
  const int rowBytes = W * 3;              // 240*3 = 720, already 4-byte aligned
  const uint32_t imgSize = (uint32_t)rowBytes * H;

  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

  // --- BMP file header (14 bytes) ---
  f.write((uint8_t)'B'); f.write((uint8_t)'M');
  wr32(f, 14 + 40 + imgSize);             // total file size
  wr16(f, 0); wr16(f, 0);                 // reserved
  wr32(f, 14 + 40);                       // pixel data offset

  // --- DIB header: BITMAPINFOHEADER (40 bytes) ---
  wr32(f, 40);                            // header size
  wr32(f, (uint32_t)W);
  wr32(f, (uint32_t)H);                   // positive height => bottom-up rows
  wr16(f, 1);                             // color planes
  wr16(f, 24);                            // bits per pixel
  wr32(f, 0);                             // BI_RGB, no compression
  wr32(f, imgSize);
  wr32(f, 2835); wr32(f, 2835);           // ~72 DPI (px/metre)
  wr32(f, 0); wr32(f, 0);                 // palette: none

  // --- Pixels, bottom-up, BGR, RGB565 -> RGB888 ---
  static uint8_t row[Config::ScreenW * 3];
  for (int y = H - 1; y >= 0; y--) {
    int p = 0;
    for (int x = 0; x < W; x++) {
      uint16_t c = cv.readPixel(x, y);    // RGB565
      uint8_t r5 = (c >> 11) & 0x1F;
      uint8_t g6 = (c >> 5)  & 0x3F;
      uint8_t b5 =  c        & 0x1F;
      row[p++] = (uint8_t)((b5 << 3) | (b5 >> 2));   // B
      row[p++] = (uint8_t)((g6 << 2) | (g6 >> 4));   // G
      row[p++] = (uint8_t)((r5 << 3) | (r5 >> 2));   // R
    }
    f.write(row, rowBytes);
  }

  f.close();
  return true;
}

// Capture the fully-rendered frame to the SD card and arm the toast.
inline void capture(M5Canvas& cv) {
  if (!ensureSD()) {
    snprintf(toastMsg, sizeof(toastMsg), "NO SD CARD");
    toastOk = false; toastTimer = ToastTime;
    Audio::deny();
    return;
  }
  scanIndex();

  char path[32];
  makePath(path, sizeof(path), nextIndex);
  if (writeBMP(cv, path)) {
    snprintf(toastMsg, sizeof(toastMsg), "SAVED shot%04d.bmp", nextIndex);
    nextIndex++;
    toastOk = true; toastTimer = ToastTime;
    Audio::missionAccept();
  } else {
    snprintf(toastMsg, sizeof(toastMsg), "SAVE FAILED");
    toastOk = false; toastTimer = ToastTime;
    Audio::deny();
    sdReady = false;     // card may have been pulled — re-init next time
  }
}

inline void tick(float dt) {
  if (toastTimer > 0.0f) toastTimer -= dt;
}

// Draw the confirmation banner over the frame. Called AFTER capture() so it
// is never part of the saved image.
inline void drawToast(M5Canvas& g) {
  if (toastTimer <= 0.0f) return;
  int len = (int)strlen(toastMsg);
  int w = len * 6 + 12;
  int h = 14;
  int x = (Config::ScreenW - w) / 2;
  int y = 3;
  uint16_t fg = toastOk ? TFT_GREEN : TFT_RED;
  g.fillRect(x, y, w, h, TFT_BLACK);
  g.drawRect(x, y, w, h, fg);
  g.setTextSize(1);
  g.setTextColor(fg, TFT_BLACK);
  g.setCursor(x + 6, y + 4);
  g.print(toastMsg);
}

} // namespace Screenshot
