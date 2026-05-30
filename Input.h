#pragma once
#include <M5Cardputer.h>

// Held-state input used during flight.
//
// Cardputer arrow keys (printed labels on the keytops) map to:
//   ;  -> up arrow
//   .  -> down arrow
//   ,  -> left arrow
//   /  -> right arrow
struct InputState {
  bool pitchUp;
  bool pitchDown;
  bool rollLeft;
  bool rollRight;
  bool accel;
  bool decel;
  bool fire;
};

inline void pollInput(InputState& in) {
  in = InputState{};
  if (!M5Cardputer.Keyboard.isPressed()) return;
  auto state = M5Cardputer.Keyboard.keysState();
  for (auto c : state.word) {
    switch (c) {
      case ';': in.pitchUp    = true; break;
      case '.': in.pitchDown  = true; break;
      case ',': in.rollLeft   = true; break;
      case '/': in.rollRight  = true; break;
      case 'e': in.accel      = true; break;
      case 's': in.decel      = true; break;
      // W or SPACE fires the laser — W is the documented primary, SPACE
      // is kept for muscle-memory.
      case 'w': in.fire       = true; break;
      case ' ': in.fire       = true; break;
      default: break;
    }
  }
}

// Edge-detected input for menus and modal screens.
//
// "Back" is mapped to the BACKSPACE key on the Cardputer — the keycap
// closest to a traditional ESC. Labels in the UI call it ESC.
struct MenuInput {
  bool any;
  bool upE, downE, leftE, rightE;
  bool enterE, backE;
  bool chartE;
  bool toggleE;   // 'f' — short/long range chart toggle (re-usable)
  bool tabE;      // R12: cycle selected POI in SystemFlight
  bool landE;     // R13: land on a nearby planet ('l')
  bool hailE;     // R16: hail a nearby NPC trader ('h')
  bool lockE;     // R21: cycle missile lock onto next NPC in front ('r')
  bool missileE;  // R21: fire a missile at the current lock ('a')
  bool ecmE;      // R21: trigger ECM blast ('e' tap; held 'e' still accels)
  bool mapE;      // 'm' — open the local system map from flight
};

namespace MenuInputInternal {
  inline bool prevUp, prevDown, prevLeft, prevRight;
  inline bool prevEnter, prevBack, prevChart, prevToggle, prevTab, prevLand, prevHail;
  inline bool prevLock, prevMissile, prevEcm, prevMap;
}

inline MenuInput pollMenuInput() {
  using namespace MenuInputInternal;
  MenuInput m{};
  bool up=false, down=false, left=false, right=false;
  bool enter=false, back=false, chart=false, toggle=false, tab=false, land=false, hail=false;
  bool lock=false, missile=false, ecm=false, map=false;

  if (M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    if (st.enter) enter = true;
    // Either keycap acts as ESC: top-right BACKSPACE (the obvious one)
    // and top-left backtick (where a real ESC would normally sit).
    if (st.del)   back  = true;
    if (st.tab)   tab   = true;
    for (auto c : st.word) {
      m.any = true;
      switch (c) {
        case ';': up    = true; break;
        case '.': down  = true; break;
        case ',': left  = true; break;
        case '/': right = true; break;
        case '`': back  = true; break;
        case 'f': toggle = true; break;
        case '\t': tab  = true; break;   // belt-and-braces in case ASCII path fires
        case 'l': land = true; break;
        case 'h': hail = true; break;
        // 'r' cycles missile lock; 'a' fires; 'm' opens the local map.
        case 'r': lock    = true; break;
        case 'a': missile = true; break;
        case 'm': map     = true; break;
        case 'e': ecm     = true; break;
        default: break;
      }
    }
  }

  m.upE     = up    && !prevUp;
  m.downE   = down  && !prevDown;
  m.leftE   = left  && !prevLeft;
  m.rightE  = right && !prevRight;
  m.enterE  = enter && !prevEnter;
  m.backE   = back  && !prevBack;
  m.chartE  = chart && !prevChart;
  m.toggleE = toggle && !prevToggle;
  m.tabE    = tab    && !prevTab;
  m.landE   = land   && !prevLand;
  m.hailE   = hail   && !prevHail;
  m.lockE    = lock    && !prevLock;
  m.missileE = missile && !prevMissile;
  m.ecmE     = ecm     && !prevEcm;
  m.mapE     = map     && !prevMap;

  prevUp = up;   prevDown = down;
  prevLeft = left; prevRight = right;
  prevEnter = enter; prevBack = back; prevChart = chart;
  prevToggle = toggle; prevTab = tab;
  prevLand = land;   prevHail = hail;
  prevLock = lock; prevMissile = missile; prevEcm = ecm;
  prevMap = map;
  return m;
}
