// Hazke — an independent C++ open-universe space sim for the M5Stack
// Cardputer (ESP32-S3, ST7789 240x135). Inspired by Parkan: Imperial
// Chronicles and Galaxy on Fire 2. All code and data are original.
//
// State machine: Title (selectable menu) -> Info / About / Flight.
//
// Controls (shown on the Controls screen):
//   ; / .  (UP / DOWN arrow)    pitch up / down
//   , / /  (LEFT / RIGHT arrow) roll left / right
//   L / '                       yaw left / right
//   E / S                       accelerate / brake
//   W                           fire laser
//   R / A                       lock / fire missile
//   Q                           ECM blast
//   M                           local system map (chart opens at the gate)
//   CTRL+SPACE                   save a screenshot to the SD card
//   ENTER                       confirm / select (menu)
//   `                           back / title
//
// Build: open this folder in Arduino IDE, select board "M5Cardputer", upload.

#include <M5Cardputer.h>
#include "Config.h"
#include "Input.h"
#include "GameState.h"
#include "Starfield.h"
#include "Cockpit.h"
#include "GameMode.h"
#include "TitleScreen.h"
#include "InfoScreen.h"
#include "AboutScreen.h"
#include "Galaxy.h"
#include "SolarSystem.h"
#include "SystemFlight.h"
#include "ChartScreen.h"
#include "SystemDataScreen.h"
#include "Market.h"
#include "MarketScreen.h"
#include "Hyperspace.h"
#include "WitchspaceScreen.h"
#include "PauseMenu.h"
#include "MapScreen.h"
#include "StatusScreen.h"
#include "EquipScreen.h"
#include "LandingScreen.h"
#include "NPCShip.h"
#include "NPCTradeScreen.h"
#include "Combat.h"
#include "Particles.h"
#include "GameOverScreen.h"
#include "Missile.h"
#include "Quest.h"
#include "QuestScreen.h"
#include "Audio.h"
#include "SDCard.h"
#include "Screenshot.h"
#include "SaveFormat.h"
#include "SaveStore.h"
#include "SaveMenuScreen.h"
#include "NameEntryScreen.h"

static M5Canvas canvas(&M5Cardputer.Display);
static GameState game;
static Starfield stars;
static InputState input;
static uint32_t lastFrameMicros = 0;

static GameMode mode = GameMode::Title;
static GameMode infoReturn = GameMode::Title;
static GameMode chartReturn = GameMode::Title;
static GameMode marketReturn = GameMode::Title;
static GameMode questsReturn = GameMode::Title;
static GameMode mapReturn    = GameMode::Pause;
static GameMode saveMenuReturn = GameMode::Title;
static float modePhase = 0.0f;
static int   menuSelected = 0;

// Player's home system (where they currently are) and the cursor on
// the chart, which doubles as the hyperspace target.
static int currentSystem = 0;
static int targetSystem  = 0;

// Helper used by SystemData ENTER and Flight J: begin a jump if the
// target is reachable. Returns true if the witchspace transition has
// been entered.
static bool tryStartJump() {
  if (!Hyperspace::canJump(game, currentSystem, targetSystem)) return false;
  mode = GameMode::Witchspace;
  modePhase = 0.0f;
  return true;
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(180);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  canvas.setColorDepth(16);
  canvas.createSprite(Config::ScreenW, Config::ScreenH);

  stars.init();
  game.reset();

  // Build the galaxy first so anything we load (current system, market
  // epoch) lines up with the live table.
  Galaxy::generate();
  Galaxy::dumpToSerial();

  // R28: bring up the PWM beeper. Sequential tone() calls queue into
  // the speaker's sample buffer, so SFX play in the background without
  // a per-frame tick.
  Audio::begin();

  // R10: dump the gate adjacency graph and the starting system's POI table
  // to Serial so we can eyeball the procgen output until R11 renders it.
  Serial.println(F("== GATE GRAPH =="));
  for (int i = 0; i < Galaxy::NumSystems; i++) {
    Serial.printf("%02d %-9s ->", i, Galaxy::systems[i].name);
    for (int t = 0; t < Galaxy::gateCount[i]; t++) {
      Serial.printf(" %02d", Galaxy::gates[i][t]);
    }
    Serial.println();
  }
  Serial.println(F("================"));
  SolarSystem::dumpToSerial(0);

  // Save/load lives in SaveStore.h (LittleFS + SD slots). Both backends
  // mount lazily on the first LOAD/SAVE menu open, so boot pays nothing.

  lastFrameMicros = micros();
}

static void newCommander() {
  // In-RAM reset only — save slots are files and stay untouched.
  game.reset();
  currentSystem = 0;
  targetSystem  = 0;
  Galaxy::marketEpoch = 0;
  MarketScreen::localSys = -1;
  // R30: quest state lives outside GameState, so reset it here.
  Quest::resetAll();
  // R24: clear any in-flight promotion banner from the prior commander.
  Rank::resetToast();
}

static void selectMenuItem() {
  switch (menuSelected) {
    case TitleScreen::ItemNewGame: // name the commander first, then fly.
      NameEntryScreen::enter();
      mode = GameMode::NameEntry;
      modePhase = 0.0f;
      break;
    case TitleScreen::ItemLoadGame:
      SaveMenuScreen::enter(SaveMenuScreen::Context::Load, nullptr,
                            currentSystem, targetSystem, -1);
      saveMenuReturn = GameMode::Title;
      mode = GameMode::SaveMenu;
      modePhase = 0.0f;
      break;
    case TitleScreen::ItemSound:
      // Toggle stays on the title screen; the accept chime doubles as
      // an audible "sound is now on" confirmation.
      Audio::toggleMute();
      if (!Audio::muted) Audio::missionAccept();
      break;
    case TitleScreen::ItemControls:
      infoReturn = GameMode::Title;
      mode = GameMode::Info;
      modePhase = 0.0f;
      break;
    case TitleScreen::ItemAbout:
      mode = GameMode::About;
      modePhase = 0.0f;
      break;
    default: break;
  }
}

void loop() {
  M5Cardputer.update();

  uint32_t frameStart = micros();
  float dt = (frameStart - lastFrameMicros) / 1e6f;
  lastFrameMicros = frameStart;
  if (dt > 0.1f) dt = 0.1f;
  modePhase += dt;

  MenuInput mk = pollMenuInput();

  // Global screenshot hotkey: Ctrl+Space writes the current frame to SD.
  // Edge-detected so a held combo snaps exactly one shot.
  static bool prevShotKey = false;
  bool shotKey = false;
  if (M5Cardputer.Keyboard.isPressed()) {
    auto ks = M5Cardputer.Keyboard.keysState();
    shotKey = ks.ctrl && ks.space;
  }
  bool shotEdge = shotKey && !prevShotKey;
  prevShotKey = shotKey;
  Screenshot::tick(dt);

  switch (mode) {
    case GameMode::Title: {
      if (mk.upE) {
        menuSelected = (menuSelected - 1 + TitleScreen::N) % TitleScreen::N;
      } else if (mk.downE) {
        menuSelected = (menuSelected + 1) % TitleScreen::N;
      } else if (mk.enterE) {
        selectMenuItem();
        break;
      }
      TitleScreen::draw(canvas, modePhase, menuSelected);
      break;
    }

    case GameMode::Info: {
      InfoScreen::draw(canvas);
      // Small guard so the press that opened the screen doesn't
      // immediately close it.
      if (modePhase > 0.25f && (mk.any || mk.enterE)) {
        mode = infoReturn;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::About: {
      AboutScreen::draw(canvas);
      if (modePhase > 0.25f && (mk.any || mk.enterE)) {
        mode = GameMode::Title;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::SystemFlight: {
      pollInput(input);
      // While warping, player flight input is locked out — game.update
      // still ticks HUD bars (laser cooldown) but the heading/throttle
      // path in SystemFlight::update ignores it on the warp branch.
      game.update(input, dt);

      // Make sure the layout we're flying matches the system we're "in".
      if (!SystemFlight::state.initialized ||
          SystemFlight::state.loadedSys != currentSystem) {
        SystemFlight::enter(currentSystem, SystemFlight::SpawnAt::AtGate);
      }

      SystemFlight::update(game, dt);
      float dist = SystemFlight::targetDistance();

      // Death pipeline: while dying, the world stops accepting input
      // and runs only the death animation. Once it finishes, jump to
      // the game-over screen.
      if (SystemFlight::state.deathFinished) {
        GameOverScreen::enter();
        mode = GameMode::GameOver;
        modePhase = 0.0f;
        break;
      }

      // R20 combat: NPC return fire + cooldown decay; player fire when
      // W (or SPACE) is held (cooldown-throttled internally). All of this is
      // skipped during warp so jumps aren't free-fire moments.
      Combat::tickPlayerCooldown(dt);
      Combat::tick(dt);
      Particles::tick(dt);
      Rank::tickToast(dt);
      // R21: ECM cooldown decays whether or not we're warping.
      if (game.ecmCooldown > 0.0f) {
        game.ecmCooldown -= dt;
        if (game.ecmCooldown < 0.0f) game.ecmCooldown = 0.0f;
      }
      // Shield regen — only while still active. Once the shield collapses
      // to 0 it stays down until the player buys REPAIR SHIELD at a
      // station, so a depleted shield is a real penalty, not a brief lull.
      // Rate (~0.027/s → ~37 s for a full recharge) mirrors NPCShip's.
      if (game.shield > 0.0f && game.shield < 1.0f) {
        game.shield += 0.0267f * dt;
        if (game.shield > 1.0f) game.shield = 1.0f;
      }
      if (!SystemFlight::state.warping && !SystemFlight::state.dying) {
        Combat::updateNPCs(game, dt);
        if (input.fire) {
          Combat::tryPlayerFire(game,
                                SystemFlight::state.px,
                                SystemFlight::state.py,
                                SystemFlight::state.pz,
                                SystemFlight::state.fx,
                                SystemFlight::state.fy,
                                SystemFlight::state.fz);
        }
        // R21: pirates fire homing missiles in addition to lasers.
        Missile::tickPirateLaunches(game, dt);
        // R21: homing missile motion + hit detection.
        Missile::update(game, dt,
                        SystemFlight::state.px,
                        SystemFlight::state.py,
                        SystemFlight::state.pz);
        // R21: drop the lock if the locked NPC died this frame.
        SystemFlight::validateLock();
      }

      canvas.fillSprite(TFT_BLACK);
      // During warp, override starfield throttle so the stars streak hard
      // and zero out rotation rates so they fly straight in.
      if (SystemFlight::state.warping) {
        stars.update(SystemFlight::WarpStarSpeed, 0.0f, 0.0f, 0.0f, dt);
      } else {
        stars.update(game.speed, game.pitchRate, game.yawRate, game.rollRate, dt);
      }
      // Clip world rendering to the viewport so planet disks and asteroid
      // dots never bleed into the HUD strip or footer.
      canvas.setClipRect(Config::ViewX, Config::ViewY, Config::ViewW, Config::ViewH);
      stars.draw(canvas);
      SystemFlight::renderWorld(canvas);
      canvas.clearClipRect();

      Cockpit::draw(canvas, game);
      SystemFlight::renderHUD(canvas, game, dist);
      SystemFlight::renderRadarBlips(canvas);

      // --- input ---
      // While dying the ship doesn't accept any commands — let the
      // animation finish and the outer switch fire GameOver.
      if (SystemFlight::state.dying) break;
      if (mk.tabE) {
        // Tab always picks the next target, even mid-warp (so you can
        // re-aim without dropping out).
        SystemFlight::cycleTarget();
      }
      // Auto-land: as soon as the player drifts inside LandingRange of any
      // planet (and isn't warping past it), drop straight into the landing
      // screen. The post-launch spawn sits just outside this radius so we
      // don't immediately bounce back in.
      if (!SystemFlight::state.warping) {
        int p = SystemFlight::planetInLandingRange();
        if (p >= 0) {
          // R30: quest hook — delivery targets and home-planet turn-ins
          // resolve on landing. Includes the planet POI so home is
          // matched per-planet, not just per-system.
          Quest::onDock(game, currentSystem, p);
          LandingScreen::enter(currentSystem, p);
          mode = GameMode::Landed;
          modePhase = 0.0f;
          break;
        }
      }
      // Auto-chart: physically touching the jump gate opens the galaxy
      // chart so the player can pick a destination. Inter-system travel
      // happens nowhere else.
      if (!SystemFlight::state.warping && SystemFlight::gateContact()) {
        SystemFlight::cancelWarp();
        chartReturn = GameMode::SystemFlight;
        mode = GameMode::Chart;
        modePhase = 0.0f;
        break;
      }
      if (mk.hailE && !SystemFlight::state.warping) {
        int npc = NPCShip::hailIdx(SystemFlight::state.px,
                                   SystemFlight::state.py,
                                   SystemFlight::state.pz);
        if (npc >= 0) {
          SystemFlight::cancelWarp();
          NPCTradeScreen::enter(currentSystem, npc,
                                NPCShip::ships[npc].hailSeed);
          mode = GameMode::NPCTrade;
          modePhase = 0.0f;
          break;
        }
      }
      // R21: missile lock cycle, missile fire, ECM trigger. All
      // suppressed during warp so jumps aren't free-fire moments.
      if (mk.lockE && !SystemFlight::state.warping) {
        SystemFlight::cycleLock();
      }
      if (mk.missileE && !SystemFlight::state.warping && game.hull >= 0.25f) {
        if (game.missiles > 0 && SystemFlight::state.lockedNPC >= 0) {
          if (Missile::spawnPlayer(SystemFlight::state.px,
                                   SystemFlight::state.py,
                                   SystemFlight::state.pz,
                                   SystemFlight::state.fx,
                                   SystemFlight::state.fy,
                                   SystemFlight::state.fz,
                                   SystemFlight::state.lockedNPC)) {
            game.missiles--;
          }
        }
      }
      if (mk.ecmE && !SystemFlight::state.warping) {
        if (game.ecm && game.ecmCooldown <= 0.0f) {
          Missile::triggerECM(SystemFlight::state.px,
                              SystemFlight::state.py,
                              SystemFlight::state.pz);
          game.ecmCooldown = Missile::ECMCooldown;
        }
      }
      if (mk.mapE) {
        mapReturn = GameMode::SystemFlight;
        MapScreen::enter(currentSystem);
        mode = GameMode::Map;
        modePhase = 0.0f;
        break;
      }
      if (mk.backE) {
        PauseMenu::open();
        mode = GameMode::Pause;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::GameOver: {
      GameOverScreen::tick(dt);
      canvas.fillSprite(TFT_BLACK);
      GameOverScreen::draw(canvas, currentSystem, game);
      // ENTER restarts the run; ESC also bounces to title.
      if (mk.enterE || mk.backE) {
        newCommander();
        mode = GameMode::Title;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::Pause: {
      PauseMenu::tick(dt);
      if (mk.upE)   PauseMenu::moveUp();
      if (mk.downE) PauseMenu::moveDown();
      // ESC always resumes — quick way out without scrolling.
      if (mk.backE) {
        mode = GameMode::SystemFlight;
        modePhase = 0.0f;
        break;
      }
      if (mk.enterE) {
        switch (PauseMenu::selected) {
          case PauseMenu::ItemResume:
            mode = GameMode::SystemFlight;
            modePhase = 0.0f;
            break;
          case PauseMenu::ItemMap:
            mapReturn = GameMode::Pause;
            MapScreen::enter(currentSystem);
            mode = GameMode::Map;
            modePhase = 0.0f;
            break;
          case PauseMenu::ItemSound:
            // Toggle stays in the pause menu; the accept chime doubles
            // as an audible "sound is now on" confirmation.
            Audio::toggleMute();
            if (!Audio::muted) Audio::missionAccept();
            break;
          case PauseMenu::ItemControls:
            infoReturn = GameMode::Pause;
            mode = GameMode::Info;
            modePhase = 0.0f;
            break;
          case PauseMenu::ItemExit:
            mode = GameMode::Title;
            modePhase = 0.0f;
            break;
        }
        break;
      }
      // Draw the cockpit/world behind the menu first so the dim
      // overlay reads as a halt rather than a black screen.
      canvas.fillSprite(TFT_BLACK);
      SystemFlight::renderWorld(canvas);
      Cockpit::draw(canvas, game);
      PauseMenu::draw(canvas, modePhase);
      break;
    }

    case GameMode::Map: {
      MapScreen::tick(dt);
      // Arrows all step through the selectable list — vertical and
      // horizontal both work so the player can grab whichever they're
      // pressing.
      if (mk.upE || mk.leftE)   MapScreen::moveSelection(-1);
      if (mk.downE || mk.rightE) MapScreen::moveSelection(+1);
      if (mk.enterE) MapScreen::markSelected();
      // 'M' is the open key from flight — pressing it again closes the map
      // the same way ESC would, so it acts as a toggle.
      if (mk.backE || mk.mapE) {
        mode = mapReturn;
        modePhase = 0.0f;
        break;
      }
      MapScreen::draw(canvas, game);
      break;
    }

    case GameMode::Chart: {
      if (mk.upE)    targetSystem = ChartScreen::nearestInDirection(targetSystem, 0);
      if (mk.downE)  targetSystem = ChartScreen::nearestInDirection(targetSystem, 1);
      if (mk.leftE)  targetSystem = ChartScreen::nearestInDirection(targetSystem, 2);
      if (mk.rightE) targetSystem = ChartScreen::nearestInDirection(targetSystem, 3);

      ChartScreen::draw(canvas, currentSystem, targetSystem, modePhase);

      if (mk.backE || mk.chartE) {
        // The chart auto-opens on gate contact; if we just abort it the
        // player is still parked inside the ring and gateContact() would
        // re-trigger next frame. Bump them clear of the gate, facing
        // back into the system — same idea as launching from a planet.
        if (chartReturn == GameMode::SystemFlight) {
          SystemFlight::bumpOffGate();
          game.speed      = 0.0f;
          game.pitchInput = 0.0f;
          game.rollInput  = 0.0f;
          game.yawInput   = 0.0f;
          game.pitchRate  = 0.0f;
          game.rollRate   = 0.0f;
          game.yawRate    = 0.0f;
        }
        mode = chartReturn;
        modePhase = 0.0f;
      } else if (mk.enterE) {
        mode = GameMode::SystemData;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::SystemData: {
      SystemDataScreen::draw(canvas, currentSystem, targetSystem, game);
      if (mk.backE || mk.chartE) {
        mode = GameMode::Chart;
        modePhase = 0.0f;
      } else if (mk.enterE) {
        tryStartJump();
      }
      break;
    }

    case GameMode::Market: {
      MarketScreen::handleInput(mk, currentSystem, game);
      MarketScreen::draw(canvas, currentSystem, game);
      if (mk.backE) {
        mode = marketReturn;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::Status: {
      StatusScreen::draw(canvas, currentSystem, game);
      if (mk.backE || mk.enterE) {
        mode = GameMode::Landed;
        modePhase = 0.0f;
      }
      break;
    }

    case GameMode::Equip: {
      EquipScreen::tick(dt);
      if (mk.upE)    EquipScreen::moveUp();
      if (mk.downE)  EquipScreen::moveDown();
      if (mk.enterE) EquipScreen::tryBuy(game);
      if (mk.backE) {
        mode = GameMode::Landed;
        modePhase = 0.0f;
        break;
      }
      EquipScreen::draw(canvas, game);
      break;
    }

    case GameMode::Quests: {
      QuestScreen::tick(dt);
      if (mk.upE)    QuestScreen::moveUp();
      if (mk.downE)  QuestScreen::moveDown();
      if (mk.enterE) QuestScreen::tryEnter(game);
      if (mk.backE) {
        // If the discard-confirmation modal is up, BACK cancels it
        // and stays on the quest board instead of leaving the screen.
        if (!QuestScreen::tryBack()) {
          mode = questsReturn;
          modePhase = 0.0f;
          break;
        }
      }
      QuestScreen::draw(canvas, game);
      break;
    }

    case GameMode::NPCTrade: {
      NPCTradeScreen::tick(dt);
      if (mk.upE)    NPCTradeScreen::moveUp();
      if (mk.downE)  NPCTradeScreen::moveDown();
      if (mk.rightE) NPCTradeScreen::tryBuy(game);
      if (mk.leftE)  NPCTradeScreen::trySell(game);
      if (mk.backE) {
        mode = GameMode::SystemFlight;
        modePhase = 0.0f;
        break;
      }
      NPCTradeScreen::draw(canvas, game);
      break;
    }

    case GameMode::Landed: {
      LandingScreen::tick(dt);

      // R30: a finished quest pops a modal banner over the menu. Block
      // all normal Landed input (move/select/launch) until the player
      // acknowledges it with ENTER or ESC.
      if (Quest::completionPending) {
        if (mk.enterE || mk.backE) Quest::dismissCompletion();
        LandingScreen::draw(canvas, game, modePhase);
        break;
      }

      if (mk.upE)   LandingScreen::moveUp();
      if (mk.downE) LandingScreen::moveDown();

      bool launchNow = mk.backE;
      if (mk.enterE) {
        switch (LandingScreen::selected) {
          case LandingScreen::ItemMarket:
            marketReturn = GameMode::Landed;
            MarketScreen::enter(currentSystem, LandingScreen::planetPOIidx);
            mode = GameMode::Market;
            modePhase = 0.0f;
            break;
          case LandingScreen::ItemEquip:
            mode = GameMode::Equip;
            modePhase = 0.0f;
            break;
          case LandingScreen::ItemQuests:
            questsReturn = GameMode::Landed;
            QuestScreen::enter(currentSystem, LandingScreen::planetPOIidx);
            mode = GameMode::Quests;
            modePhase = 0.0f;
            break;
          case LandingScreen::ItemStatus:
            mode = GameMode::Status;
            modePhase = 0.0f;
            break;
          case LandingScreen::ItemSave:
            // Snapshot the commander as they are right now, docked at
            // this planet — the picker only chooses where it lands.
            SaveMenuScreen::enter(SaveMenuScreen::Context::Save, &game,
                                  currentSystem, targetSystem,
                                  LandingScreen::planetPOIidx);
            saveMenuReturn = GameMode::Landed;
            mode = GameMode::SaveMenu;
            modePhase = 0.0f;
            break;
          case LandingScreen::ItemLaunch:
            launchNow = true;
            break;
        }
      }

      if (launchNow) {
        // Spawn well outside the planet's visible body so we're nowhere
        // near the collision shell, and zero the throttle so the ship
        // hovers until the player actually presses accel — combined with
        // enterNearPOI's face-away orientation, the first push of accel
        // pulls them off the planet, not back into it.
        SolarSystem::Layout L;
        SolarSystem::layoutFor(currentSystem, L);
        float standoff = 1500.0f;
        if (LandingScreen::planetPOIidx >= 0 &&
            LandingScreen::planetPOIidx < L.numPOIs) {
          float visualR = (float)L.poi[LandingScreen::planetPOIidx].radius
                          * SystemFlight::PlanetVisualScale;
          standoff = visualR * 3.0f + 600.0f;   // ~3× body away
        }
        SystemFlight::enterNearPOI(currentSystem,
                                   LandingScreen::planetPOIidx, standoff);
        // R30: a freshly-accepted Patrol quest fires a ONE-SHOT spawn of
        // its full pirate quota — killed pirates stay dead, so the
        // player has a fixed roster to hunt for this contract.
        if (Quest::needsPirateSpawn()) {
          NPCShip::ensurePirates((int)Quest::pirateSpawnQty(),
                                 currentSystem, SystemFlight::layout);
          Quest::consumePirateSpawn();
        }
        game.speed      = 0.0f;
        game.pitchInput = 0.0f;
        game.rollInput  = 0.0f;
        game.yawInput   = 0.0f;
        game.pitchRate  = 0.0f;
        game.rollRate   = 0.0f;
        game.yawRate    = 0.0f;
        mode = GameMode::SystemFlight;
        modePhase = 0.0f;
        break;
      }

      LandingScreen::draw(canvas, game, modePhase);
      break;
    }

    case GameMode::NameEntry: {
      NameEntryScreen::handleTyping();
      if (mk.backE) {
        mode = GameMode::Title;
        modePhase = 0.0f;
        break;
      }
      if (mk.enterE) {
        // Reset FIRST (it stamps the JAMESON default), then overlay the
        // typed name if the player entered one.
        newCommander();
        NameEntryScreen::applyTo(game);
        SystemFlight::enter(currentSystem, SystemFlight::SpawnAt::AtGate);
        mode = GameMode::SystemFlight;
        modePhase = 0.0f;
        break;
      }
      NameEntryScreen::draw(canvas, modePhase);
      break;
    }

    case GameMode::SaveMenu: {
      SaveMenuScreen::tick(dt);
      int landedPOI = -1;
      auto r = SaveMenuScreen::handleInput(mk, game, currentSystem,
                                           targetSystem, landedPOI);
      if (r == SaveMenuScreen::Result::Back) {
        mode = saveMenuReturn;
        modePhase = 0.0f;
        break;
      }
      if (r == SaveMenuScreen::Result::Loaded) {
        // apply() already restored the commander, quest state, market
        // epoch and validated the docked planet. Finish the transition:
        // invalidate the caches that key off the old commander/system,
        // then come up docked exactly where the save was made. The first
        // LAUNCH reuses the normal spawn/standoff/pirate-spawn path.
        MarketScreen::localSys = -1;
        Rank::resetToast();
        SystemFlight::state.initialized = false;
        Quest::clearCompletion();
        LandingScreen::enter(currentSystem, landedPOI);
        mode = GameMode::Landed;
        modePhase = 0.0f;
        break;
      }
      SaveMenuScreen::draw(canvas, modePhase);
      break;
    }

    case GameMode::Witchspace: {
      WitchspaceScreen::update(stars, modePhase, dt);
      WitchspaceScreen::draw(canvas, stars, currentSystem, targetSystem,
                             modePhase);
      if (modePhase >= WitchspaceScreen::Duration) {
        // Arrive: pay the jump fee in credits, advance the clock that
        // shifts market noise, make the next market visit re-roll local
        // stock, persist.
        int cost = Hyperspace::jumpCostTenths(currentSystem, targetSystem);
        game.credits -= cost;
        if (game.credits < 0) game.credits = 0;
        currentSystem = targetSystem;
        Galaxy::marketEpoch++;
        MarketScreen::localSys = -1;
        // R30: recon quests flip to ReadyToTurnIn if their target was
        // this system. Done before SystemFlight::enter so any state
        // update is visible in HUD / Pause overlay immediately.
        Quest::onHyperspaceArrive(game, currentSystem);
        SystemFlight::enter(currentSystem, SystemFlight::SpawnAt::AtGate);
        mode = GameMode::SystemFlight;
        modePhase = 0.0f;
      }
      break;
    }
  }

  // Screenshot: snap the finished frame, then overlay the toast so the
  // confirmation banner is never baked into the saved image.
  if (shotEdge) Screenshot::capture(canvas);
  Screenshot::drawToast(canvas);

  canvas.pushSprite(0, 0);

  uint32_t elapsed = micros() - frameStart;
  if (elapsed < Config::FrameUs) delayMicroseconds(Config::FrameUs - elapsed);
}
