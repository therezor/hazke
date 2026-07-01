#pragma once
#include <M5GFX.h>
#include <stdio.h>
#include <string.h>
#include "Config.h"
#include "MenuUI.h"
#include "Audio.h"
#include "Galaxy.h"
#include "GameState.h"
#include "SaveFormat.h"
#include "SaveStore.h"

// Save-slot picker, shared by two entry points:
//   * Title menu LOAD GAME   -> Context::Load
//   * Landing menu SAVE GAME -> Context::Save (the commander snapshot is
//     captured on enter(), so whatever happens in the picker saves the
//     state as it was at the moment the menu opened)
//
// LEFT/RIGHT switches the storage backend tab (internal flash <-> SD
// card); ENTER on a slot opens an action modal (LOAD / SAVE / COPY /
// DELETE); destructive actions add an ARE YOU SURE? step. COPY always
// targets the same slot number on the *other* backend — that one action
// is the whole backup/restore feature, both directions.

namespace SaveMenuScreen {

enum class Context : uint8_t { Load, Save };
enum class Result  : uint8_t { None, Loaded, Back };

enum Action : uint8_t { ActLoad, ActSave, ActCopy, ActDelete, ActCancel };

inline Context             ctx     = Context::Load;
inline SaveStore::Backend  backend = SaveStore::Backend::Internal;
inline int                 cursor  = 0;
inline SaveStore::SlotInfo slots[SaveStore::NumSlots];

// Save-context snapshot, captured when the screen opens.
inline SaveFormat::SaveData pendingSave{};

// Action modal + confirm step.
inline bool    modalOpen = false;
inline uint8_t modalActions[5];
inline int     modalCount = 0;
inline int     modalCursor = 0;
inline bool    modalDestOccupied = false;  // other backend, same slot (COPY confirm)
inline bool    confirmActive = false;
inline Action  confirmAction = ActCancel;

inline char  toastMsg[28] = "";
inline float toastTimer   = 0.0f;
constexpr float ToastTime = 1.6f;

inline void flashToast(const char* msg, bool ok) {
  snprintf(toastMsg, sizeof(toastMsg), "%s", msg);
  toastTimer = ToastTime;
  if (ok) Audio::missionAccept(); else Audio::deny();
}

inline void refresh() {
  SaveStore::refreshAll(backend, slots);
  if (cursor < 0) cursor = 0;
  if (cursor >= SaveStore::NumSlots) cursor = SaveStore::NumSlots - 1;
}

inline void enter(Context c, const GameState* g,
                  int currentSystem, int targetSystem, int landedPOI) {
  ctx           = c;
  backend       = SaveStore::Backend::Internal;
  cursor        = 0;
  modalOpen     = false;
  confirmActive = false;
  toastTimer    = 0.0f;
  if (c == Context::Save && g) {
    SaveGame::capture(pendingSave, *g, currentSystem, targetSystem, landedPOI);
  }
  refresh();
}

inline void tick(float dt) { MenuUI::tickToast(toastTimer, dt); }

// ---- Modal helpers ----

inline void openModal() {
  const SaveStore::SlotState st = slots[cursor].state;
  modalCount  = 0;
  modalCursor = 0;

  if (st == SaveStore::SlotState::BackendMissing) { Audio::deny(); return; }

  const bool ok      = st == SaveStore::SlotState::Ok;
  const bool empty   = st == SaveStore::SlotState::Empty;
  const bool tooNew  = st == SaveStore::SlotState::TooNew;
  const bool corrupt = st == SaveStore::SlotState::Corrupt;

  if (ctx == Context::Load && empty) { Audio::deny(); return; }

  if (ctx == Context::Load && ok)  modalActions[modalCount++] = ActLoad;
  if (ctx == Context::Save && !tooNew && !corrupt)
    modalActions[modalCount++] = ActSave;
  // COPY needs a structurally valid source file — Ok or TooNew both copy
  // fine (raw bytes); Corrupt can't (its CRC is already broken).
  if (ok || tooNew)                modalActions[modalCount++] = ActCopy;
  if (!empty)                      modalActions[modalCount++] = ActDelete;
  modalActions[modalCount++] = ActCancel;

  modalOpen = true;
}

// COPY's overwrite-confirm depends on the destination slot, which lives
// on the OTHER backend. Checked only when COPY is actually chosen — an
// eager check on every modal open would pay an SD.begin() timeout each
// time the card is absent.
inline void checkCopyDest() {
  SaveStore::SlotInfo dest;
  SaveStore::readSlot(SaveStore::other(backend), cursor, dest);
  modalDestOccupied = dest.state != SaveStore::SlotState::Empty &&
                      dest.state != SaveStore::SlotState::BackendMissing;
}

inline bool actionNeedsConfirm(Action a) {
  switch (a) {
    case ActDelete: return true;
    case ActSave:   return slots[cursor].state != SaveStore::SlotState::Empty;
    case ActCopy:   return modalDestOccupied;
    default:        return false;
  }
}

inline const char* actionLabel(Action a) {
  switch (a) {
    case ActLoad:   return "LOAD";
    case ActSave:   return slots[cursor].state == SaveStore::SlotState::Empty
                           ? "SAVE" : "OVERWRITE";
    case ActCopy:   return backend == SaveStore::Backend::Internal
                           ? "COPY TO SD CARD" : "COPY TO INTERNAL";
    case ActDelete: return "DELETE";
    default:        return "CANCEL";
  }
}

// Returns Result::Loaded when a LOAD was applied; caller owns the mode
// transition.
inline Result perform(Action a, GameState& game,
                      int& currentSystem, int& targetSystem, int& landedPOI) {
  modalOpen     = false;
  confirmActive = false;
  switch (a) {
    case ActLoad:
      SaveGame::apply(slots[cursor].data, game,
                      currentSystem, targetSystem, landedPOI);
      return Result::Loaded;
    case ActSave:
      if (SaveStore::writeSlot(backend, cursor, pendingSave)) {
        flashToast("SAVED", true);
      } else {
        flashToast(backend == SaveStore::Backend::SDCard
                   ? "NO SD CARD" : "SAVE FAILED", false);
      }
      refresh();
      break;
    case ActCopy: {
      SaveStore::Backend to = SaveStore::other(backend);
      if (SaveStore::copySlot(backend, to, cursor)) {
        flashToast(to == SaveStore::Backend::SDCard
                   ? "COPIED TO SD CARD" : "COPIED TO INTERNAL", true);
      } else {
        flashToast(to == SaveStore::Backend::SDCard
                   ? "NO SD CARD" : "COPY FAILED", false);
      }
      break;
    }
    case ActDelete:
      if (SaveStore::deleteSlot(backend, cursor)) flashToast("DELETED", true);
      else                                        flashToast("DELETE FAILED", false);
      refresh();
      break;
    default:
      break;
  }
  return Result::None;
}

// ---- Input ----

inline Result handleInput(const MenuInput& mk, GameState& game,
                          int& currentSystem, int& targetSystem,
                          int& landedPOI) {
  if (confirmActive) {
    if (mk.enterE) {
      return perform(confirmAction, game,
                     currentSystem, targetSystem, landedPOI);
    }
    if (mk.backE) confirmActive = false;   // back to the action modal
    return Result::None;
  }

  if (modalOpen) {
    if (mk.upE)   modalCursor = (modalCursor - 1 + modalCount) % modalCount;
    if (mk.downE) modalCursor = (modalCursor + 1) % modalCount;
    if (mk.backE) { modalOpen = false; return Result::None; }
    if (mk.enterE) {
      Action a = (Action)modalActions[modalCursor];
      if (a == ActCancel) { modalOpen = false; return Result::None; }
      if (a == ActCopy) checkCopyDest();
      if (actionNeedsConfirm(a)) {
        confirmAction = a;
        confirmActive = true;
        return Result::None;
      }
      return perform(a, game, currentSystem, targetSystem, landedPOI);
    }
    return Result::None;
  }

  if (mk.upE)   cursor = (cursor - 1 + SaveStore::NumSlots) % SaveStore::NumSlots;
  if (mk.downE) cursor = (cursor + 1) % SaveStore::NumSlots;
  if (mk.leftE || mk.rightE) {
    backend = SaveStore::other(backend);
    refresh();
  }
  if (mk.enterE) openModal();
  if (mk.backE)  return Result::Back;
  return Result::None;
}

// ---- Draw ----

constexpr int RowY0 = 18;
constexpr int RowH  = 16;

inline void drawSlotRow(M5Canvas& g, int i, bool selected) {
  const SaveStore::SlotInfo& s = slots[i];
  int y = RowY0 + i * RowH;
  uint16_t bg = MenuUI::drawRowBg(g, y + 3, RowH, selected);

  char num[4];
  snprintf(num, sizeof(num), "%d", i + 1);
  g.setTextSize(1);
  g.setTextColor(selected ? MenuUI::SelTextColor : MenuUI::LabelColor, bg);
  g.setCursor(6, y + 6);
  g.print(num);

  switch (s.state) {
    case SaveStore::SlotState::Ok: {
      const SaveFormat::SaveData& d = s.data;
      char name[13];
      memcpy(name, d.commanderName, 12);
      name[12] = '\0';
      g.setTextColor(selected ? MenuUI::SelTextColor : MenuUI::ValueColor, bg);
      g.setCursor(18, y + 6);
      g.print(name);

      char cr[16];
      MenuUI::formatCredits(cr, sizeof(cr), d.credits);
      g.setTextColor(MenuUI::CreditsColor, bg);
      g.setCursor(90, y + 6);
      g.print(cr);

      // Clamp the display so "K9999" (ends x=181) can't run into the
      // right-aligned system name (9-char names start at x=184).
      char kills[8];
      int k = (int)d.kills > 9999 ? 9999 : (int)d.kills;
      snprintf(kills, sizeof(kills), "K%d", k);
      g.setTextColor(MenuUI::SubColor, bg);
      g.setCursor(152, y + 6);
      g.print(kills);

      const char* sys = (d.currentSystem < Galaxy::NumSystems)
                        ? Galaxy::systems[d.currentSystem].name : "?";
      g.setTextColor(MenuUI::ValueColor, bg);
      g.setCursor(Config::ScreenW - (int)strlen(sys) * MenuUI::CharW - 2, y + 6);
      g.print(sys);
      break;
    }
    case SaveStore::SlotState::Empty:
      g.setTextColor(MenuUI::DisabledColor, bg);
      g.setCursor(18, y + 6);
      g.print("- EMPTY -");
      break;
    case SaveStore::SlotState::Corrupt:
      g.setTextColor(TFT_RED, bg);
      g.setCursor(18, y + 6);
      g.print("CORRUPT SAVE");
      break;
    case SaveStore::SlotState::TooNew: {
      char msg[28];
      snprintf(msg, sizeof(msg), "NEWER VERSION (v%u)",
               (unsigned)s.fileVersion);
      g.setTextColor(TFT_RED, bg);
      g.setCursor(18, y + 6);
      g.print(msg);
      break;
    }
    default:
      break;
  }
}

inline void drawModal(M5Canvas& g, float phase) {
  const int boxW = 150;
  const int rowH = 12;
  const int boxH = 20 + modalCount * rowH + 6;
  const int boxX = (Config::ScreenW - boxW) / 2;
  const int boxY = (Config::ScreenH - boxH) / 2;

  g.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  g.drawRect(boxX, boxY, boxW, boxH, MenuUI::SubColor);

  char title[12];
  snprintf(title, sizeof(title), "SLOT %d", cursor + 1);
  g.setTextSize(1);
  g.setTextColor(MenuUI::TitleColor, TFT_BLACK);
  g.setCursor(boxX + (boxW - (int)strlen(title) * MenuUI::CharW) / 2, boxY + 6);
  g.print(title);

  for (int i = 0; i < modalCount; i++) {
    Action a = (Action)modalActions[i];
    const char* label = actionLabel(a);
    bool sel = (i == modalCursor);
    bool destructive = (a == ActDelete) ||
                       (a == ActSave && actionNeedsConfirm(ActSave));
    int y = boxY + 20 + i * rowH;
    uint16_t color;
    if (sel) {
      uint8_t lum = MenuUI::pulseLum(phase, 170, 255);
      color = destructive ? g.color565(lum, 40, 40) : g.color565(lum, lum, lum);
    } else {
      color = destructive ? 0x6000 : MenuUI::DisabledColor;
    }
    g.setTextColor(color, TFT_BLACK);
    g.setCursor(boxX + (boxW - (int)strlen(label) * MenuUI::CharW) / 2 -
                (sel ? 2 * MenuUI::CharW : 0), y);
    if (sel) g.print("> ");
    g.print(label);
  }
}

inline void drawConfirm(M5Canvas& g, float phase) {
  const int boxW = 170;
  const int boxH = 52;
  const int boxX = (Config::ScreenW - boxW) / 2;
  const int boxY = (Config::ScreenH - boxH) / 2;

  uint16_t border = g.color565(220, 60, 60);
  g.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  g.drawRect(boxX, boxY, boxW, boxH, border);
  g.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, border);

  char line[32];
  snprintf(line, sizeof(line), "%s SLOT %d?",
           actionLabel(confirmAction), cursor + 1);
  g.setTextSize(1);
  g.setTextColor(border, TFT_BLACK);
  g.setCursor(boxX + (boxW - (int)strlen(line) * MenuUI::CharW) / 2, boxY + 10);
  g.print(line);

  uint8_t lum = MenuUI::pulseLum(phase, 140, 240);
  g.setTextColor(g.color565(lum, lum, 0), TFT_BLACK);
  const char* prompt = "ENTER=YES  ESC=NO";
  g.setCursor(boxX + (boxW - (int)strlen(prompt) * MenuUI::CharW) / 2,
              boxY + 32);
  g.print(prompt);
}

inline void draw(M5Canvas& g, float phase) {
  MenuUI::clearBg(g);

  char tab[16];
  snprintf(tab, sizeof(tab), "< %s >", SaveStore::backendName(backend));
  MenuUI::drawHeader(g, ctx == Context::Load ? "LOAD GAME" : "SAVE GAME",
                     tab, MenuUI::TitleColor, MenuUI::SubColor);

  if (slots[0].state == SaveStore::SlotState::BackendMissing) {
    const char* msg = backend == SaveStore::Backend::SDCard
                      ? "NO SD CARD" : "STORAGE UNAVAILABLE";
    MenuUI::printCenter(g, 60, msg, TFT_RED);
  } else {
    for (int i = 0; i < SaveStore::NumSlots; i++) {
      drawSlotRow(g, i, !modalOpen && !confirmActive && i == cursor);
    }
  }

  MenuUI::drawToast(g, toastMsg, toastTimer);
  MenuUI::drawFooter(g, "ARROWS  L/R STORAGE  ENTER  ESC");

  if (confirmActive)    drawConfirm(g, phase);
  else if (modalOpen)   drawModal(g, phase);
}

} // namespace SaveMenuScreen
