#pragma once
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include "SaveFormat.h"
#include "SDCard.h"
#include "GameState.h"
#include "Quest.h"
#include "Galaxy.h"
#include "SolarSystem.h"
#include "Rank.h"

// Save slots on two interchangeable backends:
//   Internal — LittleFS on the stock `spiffs` data partition (1.375 MB),
//              formatted on first use.
//   SDCard   — the same microSD card the screenshot feature writes to.
// Both expose the fs::FS File API, so every slot operation below is
// written once against fs::FS& and the backup/restore COPY action is a
// symmetric raw byte copy. Files live at /hazke/saves/slotN.sav on both.

// ---- Game <-> payload marshalling --------------------------------------

namespace SaveGame {

// SaveDataV1 hard-codes these game constants. If one changes, the save
// layer must NOT silently reinterpret old files — fail the build here and
// ship a SaveDataV2 + transformer instead.
static_assert((int)Market::N == 17,           "commodity count changed — needs a new save version");
static_assert(GameState::NumFactions == 4,    "faction count changed — needs a new save version");
static_assert(GameState::NameCap == 12,       "name capacity changed — needs a new save version");
static_assert((int)Quest::Type::Count <= 255, "quest type must fit a byte");
static_assert(Galaxy::NumSystems <= 255,      "system index must fit a byte");

inline void capture(SaveFormat::SaveData& d, const GameState& g,
                    int currentSystem, int targetSystem, int landedPOI) {
  memset(&d, 0, sizeof d);
  memcpy(d.commanderName, g.commanderName, sizeof d.commanderName);
  d.credits  = g.credits;
  d.kills    = g.kills;
  d.shield   = g.shield;
  d.hull     = g.hull;
  d.hullHeat = g.hullHeat;
  d.cargoMax = g.cargoMax;
  memcpy(d.cargo, g.cargo, sizeof d.cargo);
  d.missiles  = g.missiles;
  d.ecmOwned  = g.ecm ? 1 : 0;
  d.laserTier = g.laserTier;
  memcpy(d.standing, g.standing, sizeof d.standing);
  d.lastSeenRank = g.lastSeenRank;
  d.arcStage     = g.arcStage;
  d.arcSide      = g.arcSide;

  const Quest::Slot& q = Quest::active;
  d.quest.type           = (uint8_t)q.type;
  d.quest.fromSys        = q.fromSys;
  d.quest.fromPOI        = q.fromPOI;
  d.quest.toPOI          = q.toPOI;
  d.quest.commodity      = q.commodity;
  d.quest.qty            = q.qty;
  d.quest.faction        = q.faction;
  d.quest.factionDelta   = q.factionDelta;
  d.quest.rewardTenthsCR = q.rewardTenthsCR;
  d.quest.progress       = q.progress;
  d.quest.difficulty     = q.difficulty;
  d.questStatus          = (uint8_t)Quest::status;
  d.pirateSpawnPending   = Quest::pirateSpawnPending ? 1 : 0;

  d.currentSystem = (uint8_t)currentSystem;
  d.targetSystem  = (uint8_t)targetSystem;
  d.landedPOI     = (uint8_t)(landedPOI < 0 ? 0xFF : landedPOI);
  d.marketEpoch   = Galaxy::marketEpoch;
}

// Restore a (already migrated) payload into the live game. Starts from a
// full GameState::reset() so every transient (speed, input rates, ECM
// cooldown) comes out exactly like a fresh commander, then overwrites the
// persistent fields. Every value is clamped: a CRC-valid file can still
// carry nonsense (hand-edited saves), and nothing here may index out of
// bounds. The caller finishes the mode transition (LandingScreen::enter,
// SystemFlight/MarketScreen/Rank cache resets).
inline void apply(const SaveFormat::SaveData& d, GameState& g,
                  int& currentSystem, int& targetSystem, int& landedPOI) {
  g.reset();
  memcpy(g.commanderName, d.commanderName, GameState::NameCap);
  g.commanderName[GameState::NameCap - 1] = '\0';
  g.credits  = d.credits < 0 ? 0 : d.credits;
  g.kills    = d.kills   < 0 ? 0 : d.kills;
  g.shield   = GameState::clamp01(d.shield);
  g.hull     = GameState::clamp01(d.hull);
  g.hullHeat = GameState::clamp01(d.hullHeat);
  g.cargoMax = (d.cargoMax == GameState::CargoMaxLarge)
               ? GameState::CargoMaxLarge : GameState::CargoMaxDefault;
  memcpy(g.cargo, d.cargo, sizeof g.cargo);
  g.missiles  = d.missiles > 4 ? 4 : d.missiles;
  g.ecm       = d.ecmOwned != 0;
  g.laserTier = d.laserTier > 2 ? 2 : d.laserTier;
  for (int i = 0; i < GameState::NumFactions; i++) {
    int8_t s = d.standing[i];
    g.standing[i] = s < -100 ? -100 : (s > 100 ? 100 : s);
  }
  g.lastSeenRank = d.lastSeenRank >= Rank::N ? (uint8_t)(Rank::N - 1)
                                             : d.lastSeenRank;
  g.arcStage = d.arcStage > 5 ? 5 : d.arcStage;
  g.arcSide  = d.arcSide  > 2 ? 0 : d.arcSide;

  currentSystem = d.currentSystem < Galaxy::NumSystems ? d.currentSystem : 0;
  targetSystem  = d.targetSystem  < Galaxy::NumSystems ? d.targetSystem
                                                       : currentSystem;
  Galaxy::marketEpoch = d.marketEpoch;

  // Quest — restore only if every enum/index survives validation;
  // otherwise the commander simply has no active contract.
  Quest::resetAll();
  bool questValid =
      d.quest.type > 0 && d.quest.type < (uint8_t)Quest::Type::Count &&
      d.questStatus > 0 && d.questStatus <= (uint8_t)Quest::Status::ReadyToTurnIn &&
      d.quest.fromSys < Galaxy::NumSystems &&
      (d.quest.commodity == 0xFF || d.quest.commodity < (uint8_t)Market::N);
  if (questValid) {
    Quest::active.type           = (Quest::Type)d.quest.type;
    Quest::active.fromSys        = d.quest.fromSys;
    Quest::active.fromPOI        = d.quest.fromPOI;
    Quest::active.toPOI          = d.quest.toPOI;
    Quest::active.commodity      = d.quest.commodity;
    Quest::active.qty            = d.quest.qty;
    Quest::active.faction        = d.quest.faction;
    Quest::active.factionDelta   = d.quest.factionDelta;
    Quest::active.rewardTenthsCR = d.quest.rewardTenthsCR;
    Quest::active.progress       = d.quest.progress;
    Quest::active.difficulty     = d.quest.difficulty;
    Quest::status = (Quest::Status)d.questStatus;
    Quest::pirateSpawnPending = d.pirateSpawnPending != 0 &&
                                Quest::active.type == Quest::Type::Patrol;
  }

  // The docked planet must be a real Planet POI in the restored system —
  // fall back to the first planet if the file disagrees with procgen.
  SolarSystem::Layout L;
  SolarSystem::layoutFor(currentSystem, L);
  landedPOI = -1;
  if (d.landedPOI < L.numPOIs &&
      L.poi[d.landedPOI].type == SolarSystem::POIType::Planet) {
    landedPOI = d.landedPOI;
  } else {
    for (int i = 0; i < L.numPOIs; i++) {
      if (L.poi[i].type == SolarSystem::POIType::Planet) { landedPOI = i; break; }
    }
  }
}

} // namespace SaveGame

// ---- Slot IO ------------------------------------------------------------

namespace SaveStore {

enum class Backend : uint8_t { Internal = 0, SDCard = 1 };
constexpr int NumSlots = 5;

enum class SlotState : uint8_t {
  Empty,           // no file
  Ok,              // loaded + migrated, data valid
  Corrupt,         // bad magic / size / CRC / truncated
  TooNew,          // written by a newer firmware — never parsed or deleted implicitly
  BackendMissing,  // backend failed to mount (no card / FS error)
};

struct SlotInfo {
  SlotState state = SlotState::Empty;
  uint16_t  fileVersion = 0;        // as found on disk, pre-migration
  SaveFormat::SaveData data{};      // valid iff state == Ok (migrated, RAM-only)
};

inline bool lfsReady = false;

inline Backend other(Backend b) {
  return b == Backend::Internal ? Backend::SDCard : Backend::Internal;
}

inline const char* backendName(Backend b) {
  return b == Backend::Internal ? "INTERNAL" : "SD CARD";
}

inline fs::FS& fsFor(Backend b) {
  return b == Backend::Internal ? (fs::FS&)LittleFS : (fs::FS&)SD;
}

// Lazy mount — the game never pays for a backend it doesn't touch.
inline bool ensureBackend(Backend b) {
  if (b == Backend::Internal) {
    if (lfsReady) return true;
    if (!LittleFS.begin(/*formatOnFail=*/true)) return false;
    LittleFS.mkdir("/hazke");
    LittleFS.mkdir("/hazke/saves");
    lfsReady = true;
    return true;
  }
  if (!SDCard::ensure()) return false;
  SD.mkdir("/hazke/saves");
  return true;
}

inline void slotPath(char* out, size_t n, int slot) {
  snprintf(out, n, "/hazke/saves/slot%d.sav", slot);
}
inline void tmpPath(char* out, size_t n, int slot) {
  snprintf(out, n, "/hazke/saves/slot%d.tmp", slot);
}

// Read + validate + migrate one slot. Never writes, never crashes on a
// corrupt file — the worst outcome is state == Corrupt.
inline void readSlot(Backend b, int slot, SlotInfo& out) {
  out = SlotInfo{};
  if (!ensureBackend(b)) { out.state = SlotState::BackendMissing; return; }
  fs::FS& fs = fsFor(b);

  char path[40];
  slotPath(path, sizeof(path), slot);
  if (!fs.exists(path)) { out.state = SlotState::Empty; return; }

  File f = fs.open(path, FILE_READ);
  if (!f) { out.state = SlotState::Corrupt; return; }

  SaveFormat::Header h;
  if (f.read((uint8_t*)&h, sizeof h) != sizeof h ||
      h.magic != SaveFormat::Magic) {
    f.close(); out.state = SlotState::Corrupt; return;
  }
  out.fileVersion = h.version;
  if (h.version > SaveFormat::CurrentVersion) {
    f.close(); out.state = SlotState::TooNew; return;
  }
  uint16_t frozen = SaveFormat::frozenSizeFor(h.version);
  if (frozen == 0 || h.payloadSize != frozen ||
      h.payloadSize > SaveFormat::MaxPayload) {
    f.close(); out.state = SlotState::Corrupt; return;
  }

  uint8_t buf[SaveFormat::MaxPayload];
  if (f.read(buf, h.payloadSize) != h.payloadSize) {
    f.close(); out.state = SlotState::Corrupt; return;
  }
  f.close();
  if (SaveFormat::crc32(buf, h.payloadSize) != h.payloadCrc) {
    out.state = SlotState::Corrupt; return;
  }

  uint16_t ver = h.version, size = h.payloadSize;
  if (!SaveFormat::migrate(buf, ver, size)) {
    out.state = SlotState::Corrupt; return;
  }
  memcpy(&out.data, buf, sizeof out.data);
  out.state = SlotState::Ok;
}

// Shared tail of writeSlot/copySlot: verify the freshly-written temp file
// end-to-end, then swap it in over the final name. On any failure the old
// save is untouched (worst interruption case: death between remove and
// rename leaves the new data in slotN.tmp — recoverable, never corrupt).
inline bool commitTmp(fs::FS& fs, const char* tmp, const char* fin) {
  File f = fs.open(tmp, FILE_READ);
  if (!f) return false;
  SaveFormat::Header h;
  bool ok = f.read((uint8_t*)&h, sizeof h) == sizeof h &&
            h.magic == SaveFormat::Magic &&
            h.payloadSize <= SaveFormat::MaxPayload;
  if (ok) {
    uint8_t buf[SaveFormat::MaxPayload];
    ok = f.read(buf, h.payloadSize) == h.payloadSize &&
         SaveFormat::crc32(buf, h.payloadSize) == h.payloadCrc;
  }
  f.close();
  if (!ok) return false;
  fs.remove(fin);                 // FAT rename won't clobber an existing name
  return fs.rename(tmp, fin);
}

inline void cleanupFailure(Backend b, fs::FS& fs, const char* tmp) {
  fs.remove(tmp);
  if (b == Backend::SDCard) SDCard::markFailed();  // card may have been pulled
}

// Write the CURRENT-version payload into a slot (temp file + verify +
// rename, so a mid-write power cut never destroys the previous save).
inline bool writeSlot(Backend b, int slot, const SaveFormat::SaveData& d) {
  if (!ensureBackend(b)) return false;
  fs::FS& fs = fsFor(b);

  char tmp[40], fin[40];
  tmpPath(tmp, sizeof(tmp), slot);
  slotPath(fin, sizeof(fin), slot);

  SaveFormat::Header h{};
  h.magic       = SaveFormat::Magic;
  h.version     = SaveFormat::CurrentVersion;
  h.payloadSize = (uint16_t)sizeof d;
  h.payloadCrc  = SaveFormat::crc32((const uint8_t*)&d, sizeof d);

  fs.remove(tmp);
  File f = fs.open(tmp, FILE_WRITE);
  if (!f) { cleanupFailure(b, fs, tmp); return false; }
  bool ok = f.write((const uint8_t*)&h, sizeof h) == sizeof h &&
            f.write((const uint8_t*)&d, sizeof d) == sizeof d;
  f.close();

  if (!ok || !commitTmp(fs, tmp, fin)) { cleanupFailure(b, fs, tmp); return false; }
  return true;
}

inline bool deleteSlot(Backend b, int slot) {
  if (!ensureBackend(b)) return false;
  char path[40];
  slotPath(path, sizeof(path), slot);
  return fsFor(b).remove(path);
}

// Backup/restore: copy a slot to the other backend as RAW BYTES — the
// file keeps whatever version it has (copying must never silently
// migrate). Source only has to be structurally valid (magic + CRC), so
// even a TooNew save can be backed up. Destination goes through the same
// tmp + verify + rename path as writeSlot.
inline bool copySlot(Backend from, Backend to, int slot) {
  if (!ensureBackend(from) || !ensureBackend(to)) return false;

  char src[40];
  slotPath(src, sizeof(src), slot);
  File f = fsFor(from).open(src, FILE_READ);
  if (!f) return false;

  SaveFormat::Header h;
  uint8_t buf[SaveFormat::MaxPayload];
  bool ok = f.read((uint8_t*)&h, sizeof h) == sizeof h &&
            h.magic == SaveFormat::Magic &&
            h.payloadSize <= SaveFormat::MaxPayload &&
            f.read(buf, h.payloadSize) == h.payloadSize &&
            SaveFormat::crc32(buf, h.payloadSize) == h.payloadCrc;
  f.close();
  if (!ok) return false;

  fs::FS& dst = fsFor(to);
  char tmp[40], fin[40];
  tmpPath(tmp, sizeof(tmp), slot);
  slotPath(fin, sizeof(fin), slot);

  dst.remove(tmp);
  File o = dst.open(tmp, FILE_WRITE);
  if (!o) { cleanupFailure(to, dst, tmp); return false; }
  ok = o.write((const uint8_t*)&h, sizeof h) == sizeof h &&
       o.write(buf, h.payloadSize) == h.payloadSize;
  o.close();

  if (!ok || !commitTmp(dst, tmp, fin)) { cleanupFailure(to, dst, tmp); return false; }
  return true;
}

// Picker helper — scan all slots on one backend. Probe the mount ONCE:
// per-slot readSlot would retry a failed SD.begin() five times in a row
// (five SPI timeouts) every time the picker lands on an empty card slot.
inline void refreshAll(Backend b, SlotInfo out[NumSlots]) {
  if (!ensureBackend(b)) {
    for (int i = 0; i < NumSlots; i++) {
      out[i] = SlotInfo{};
      out[i].state = SlotState::BackendMissing;
    }
    return;
  }
  for (int i = 0; i < NumSlots; i++) readSlot(b, i, out[i]);
}

} // namespace SaveStore
