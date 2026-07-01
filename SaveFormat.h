#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "Config.h"

// On-disk save format: frozen versioned structs + chained transformers.
//
// A save file is a 16-byte header followed by a packed payload struct:
//
//   offset  size  field
//   0       4     magic 'H''Z''S''V'
//   4       2     uint16 formatVersion (little-endian)
//   6       2     uint16 payloadSize   (= sizeof(SaveDataVn) for that version)
//   8       4     uint32 CRC-32 of the payload bytes (poly 0xEDB88320)
//   12      4     reserved, written as 0
//   16      N     payload
//
// The header layout is frozen forever — it is the version-independent
// part every firmware can parse. Each released payload version gets a
// frozen `#pragma pack(1)` struct (SaveDataV1, SaveDataV2, …) whose
// exact byte size is pinned by a static_assert. NEVER edit a released
// SaveDataVn: layout changes create SaveDataVn+1 plus a pure
// upgradeVntoVn+1() transformer, and `migrate()` chains transformers so
// any old file upgrades to the current version in RAM. Only this
// firmware reads these files (both backends are written by the same
// little-endian IEEE-754 chip), so raw struct bytes are safe.
//
// This header is deliberately Arduino-free so the format logic can be
// compiled and exercised on a desktop host.

namespace SaveFormat {

constexpr uint32_t Magic = 0x56535A48u;   // 'HZSV' little-endian
constexpr uint16_t CurrentVersion = Config::SaveFormatVersion;

// Scratch bound for migration buffers. Every frozen payload must fit;
// bump if a future version outgrows it (existing files stay valid).
constexpr uint16_t MaxPayload = 512;

#pragma pack(push, 1)
struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t payloadCrc;
  uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 16, "save header layout is frozen");

// Bitwise CRC-32 (no table — payloads are ~100 bytes, speed irrelevant).
inline uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// ---- Version 1 (game release v1.2) ------------------------------------
//
// Mirrors the persistent subset of GameState + Quest + world location.
// Constants are hard-coded here on purpose: the struct must not drift
// when game-side constants change. SaveStore.h cross-checks them against
// the live game headers at compile time.

#pragma pack(push, 1)
struct SaveQuestSlotV1 {       // packed mirror of Quest::Slot
  uint8_t  type;               // Quest::Type
  uint8_t  fromSys;
  uint8_t  fromPOI;
  uint8_t  toPOI;              // 0xFF if N/A
  uint8_t  commodity;          // 0xFF if N/A
  uint8_t  qty;
  uint8_t  faction;
  int8_t   factionDelta;
  int16_t  rewardTenthsCR;
  uint16_t progress;
  uint8_t  difficulty;
};

struct SaveDataV1 {
  // Commander (persistent subset of GameState — flight transients like
  // speed/input rates/ecmCooldown are rebuilt by GameState::reset()).
  char     commanderName[12];  // GameState::NameCap
  int32_t  credits;            // tenths of CR
  int32_t  kills;
  float    shield;
  float    hull;
  float    hullHeat;
  uint8_t  cargoMax;
  uint8_t  cargo[17];          // Market::N
  uint8_t  missiles;
  uint8_t  ecmOwned;
  uint8_t  laserTier;
  int8_t   standing[4];        // GameState::NumFactions
  uint8_t  lastSeenRank;
  uint8_t  arcStage;
  uint8_t  arcSide;
  // Quest
  SaveQuestSlotV1 quest;
  uint8_t  questStatus;        // Quest::Status
  uint8_t  pirateSpawnPending;
  // World / location (saves happen landed on a planet)
  uint8_t  currentSystem;
  uint8_t  targetSystem;
  uint8_t  landedPOI;
  uint32_t marketEpoch;
};
#pragma pack(pop)
static_assert(sizeof(SaveQuestSlotV1) == 13, "V1 layout is frozen");
static_assert(sizeof(SaveDataV1) == 82, "V1 layout is frozen");

// Alias always naming the CURRENT payload struct. Retarget when a new
// version ships.
using SaveData = SaveDataV1;
static_assert(sizeof(SaveData) <= MaxPayload, "bump MaxPayload");

// Exact payload size a given on-disk version must have; 0 = unknown
// version (newer firmware or garbage).
inline uint16_t frozenSizeFor(uint16_t ver) {
  switch (ver) {
    case 1:  return (uint16_t)sizeof(SaveDataV1);
    default: return 0;
  }
}

// ---- Transformers ------------------------------------------------------
//
// In-place chained migration over a scratch buffer. Entry: `buf` holds
// `size` bytes of version `ver` payload (already CRC-checked and
// size-matched against frozenSizeFor). Exit: true iff `buf` now holds a
// current-version SaveData.
//
// When version N+1 ships, add one block ABOVE the return:
//   if (ver == N) {
//     SaveDataVN vn;  memcpy(&vn, buf, sizeof vn);
//     SaveDataVN1 v;  upgradeVNtoVN1(vn, v);
//     memcpy(buf, &v, sizeof v);  size = sizeof v;  ver = N + 1;
//   }
// where upgradeVNtoVN1 is a pure function filling defaults for new
// fields (append-only versions can prefix-memcpy; reordered versions
// assign field by field).
inline bool migrate(uint8_t* buf, uint16_t& ver, uint16_t& size) {
  (void)buf;
  // v1 is current — no upgrade steps yet.
  return ver == CurrentVersion && size == sizeof(SaveData);
}

} // namespace SaveFormat
