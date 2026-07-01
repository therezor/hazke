#pragma once

enum class GameMode {
  Title,         // main menu (NEW GAME / CONTROLS / ABOUT)
  Info,          // controls help
  About,
  SystemFlight,  // open free-flight in the active solar system
  Pause,         // in-game pause menu (RESUME / MAP / CONTROLS / EXIT)
  Map,           // local-system top-down map with selection + info
  Landed,        // refit R13: parked on a planet surface
  NPCTrade,      // refit R16: hailed an NPC trader in flight
  Chart,         // legacy galactic chart (kept for hyperspace targeting)
  SystemData,
  Market,
  Witchspace,
  Equip,
  Status,
  Quests,        // R30: per-planet quest board (single active quest)
  GameOver,      // ship destroyed — shows stats + restart prompt
  SaveMenu,      // save-slot picker (load from title / save from landing)
  NameEntry,     // NEW GAME commander name typing screen
};
