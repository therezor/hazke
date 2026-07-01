#pragma once
#include <SD.h>
#include <SPI.h>

// Shared microSD bring-up. The card is used by two subsystems —
// Screenshot (BMP captures) and SaveStore (save slots) — so the SPI init
// and "is the card up" flag live here rather than in either client.
//
// The M5Cardputer microSD slot lives on a shared SPI bus:
//   CLK 40 · MISO 39 · MOSI 14 · CS 12
// We bring it up lazily on first use so boot stays fast and a card-less
// unit pays nothing until a feature actually touches the card.

namespace SDCard {

constexpr int PinSCK  = 40;
constexpr int PinMISO = 39;
constexpr int PinMOSI = 14;
constexpr int PinCS   = 12;

inline bool ready = false;

inline bool ensure() {
  if (ready) return true;
  SPI.begin(PinSCK, PinMISO, PinMOSI, PinCS);
  if (!SD.begin(PinCS, SPI, 25000000)) {
    ready = false;
    return false;
  }
  SD.mkdir("/hazke");
  ready = true;
  return true;
}

// Call after a write failure — the card may have been pulled, so the next
// ensure() re-runs the full init instead of trusting the stale handle.
inline void markFailed() { ready = false; }

} // namespace SDCard
