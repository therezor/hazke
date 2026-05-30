# Hazke

**Trade. Hunt. Survive.** An open-universe space sim for the M5Stack
Cardputer, inspired by *Parkan: Imperial Chronicles* and *Galaxy on
Fire 2*. Fly between sixteen procedurally-generated star systems, land
on planets, run cargo or hunt pirates for one of four factions, and
shape your standing across the galaxy — all on a 240×135 display.

No assets or code are copied from the inspirations; the galaxy,
commodity table, ship silhouettes and faction roster are all original.

---

## Hardware

- **Board:** M5Stack Cardputer (ESP32-S3, ST7789 240×135 display,
  56-key keyboard, PWM beeper).
- Built with the Arduino IDE / Arduino CLI against the `M5Cardputer`
  / `M5Unified` libraries.

## Build & flash

1. Open `hazke.ino` in the Arduino IDE.
2. Install the **M5Cardputer** library via Library Manager (pulls
   `M5Unified` and `M5GFX` automatically).
3. Board: `M5Stack` → `M5Cardputer`. Port: whichever USB-C tty your
   Cardputer enumerates as.
4. Upload. The first boot drops you on the title screen.

There is **no save system** — every reboot starts a fresh commander.
File / SD-card persistence is on the roadmap but not yet shipped.

## Controls

Cardputer keycaps in brackets show the arrow legend printed on the
device.

| Key                | Action |
|--------------------|--------|
| `;` / `.` (↑ / ↓)  | Pitch up / down |
| `,` / `/` (← / →)  | Roll left / right |
| `E`                | Accelerate |
| `S`                | Brake |
| `W` or `Space`     | Fire laser |
| `R`                | Cycle missile lock |
| `A`                | Fire missile at current lock |
| `Q`                | Fire ECM burst (if installed) |
| `Tab`              | Cycle target POI |
| `H`                | Hail nearby NPC trader |
| `M`                | Open local system map |
| `Enter`            | Confirm in menus |
| `Backspace` / `` ` `` | Back / pause |

---

## How it plays

You start in your ship at a jump gate with 100 CR and an empty hold.
From there you can:

- **Trade.** Buy commodities cheap in their home market and sell them
  where they're rare. Agricultural goods are cheap on farm worlds and
  dear on industrial worlds, and vice versa. Hailing an NPC in flight
  opens a separate buy/sell dialog with its own spread.
- **Run quests.** Land on a planet, open its quest board, and accept
  one of four offered contracts. Quests are local-system-only and
  return-to-origin: you take a job at planet A, complete it somewhere
  in the same system, and fly back to A for payment.
- **Hunt.** Kill pirates for bounties and reputation with lawful
  factions. Patrol quests force a fixed pirate roster to spawn after
  launch, so the hunt is winnable even in peaceful systems. NPCs are
  slower than you at full throttle, so you can always either outrun a
  fight or close in on a wounded one. Damage thresholds apply to both
  sides: below 50% hull engines drop to half thrust, below 25% hull
  weapons go offline. A ship that's been beaten below 25% can no
  longer shoot back — `H` then opens a loot dialog where you grab its
  cargo for free.
- **Travel.** Pay 10 CR per LY at a jump gate to hyperspace to a
  neighbouring system. Only direct gate links are reachable; the
  galaxy is a graph, not a free-distance grid.
- **Upgrade.** Spend earnings at the EQUIP shop on missiles, an ECM,
  a larger hold, better lasers, a docking computer, an escape pod,
  or hull repairs.

Die (hull → 0) and the game-over screen offers a restart.

---

## Game systems

### Galaxy

Sixteen systems generated deterministically from a single LCG seed.
Each has a name, government, economy, tech level, population, and
productivity. A gate adjacency graph (2–4 neighbours per system, made
symmetric) is the only path between systems — the chart screen draws
this graph and dims any system without a direct link to your current
location.

Coordinates live in a 256×256 map; one map unit is roughly 0.1 LY,
so a typical neighbour is 2–5 LY away.

### Solar systems

Each system is a 3D volume (about 48 km on a side) containing:

- a star at the origin,
- one to three planets on inclined orbits (some with rings, moonlets,
  or asteroid belts),
- one or more Coriolis stations,
- a jump gate (the only way out).

Layouts are re-derived on demand from the system seed, so the same
system always looks the same.

### Free flight

The cockpit shows a starfield, HUD bars (shield, hull, heat,
throttle, laser cooldown), a 3D radar of nearby ships and POIs, and a
mode banner stack for contextual prompts (`H=HAIL`, `BELT`, `WARP`,
`ENTER GATE`, `HEAT!`). Landing on a planet and jumping at a gate are
automatic — drift into range and the screen hand-off fires itself.

Pressing `Tab` cycles a target POI.

### Combat

- **Lasers** come in three tiers: Pulse (start), Beam (1000 CR),
  Military (6000 CR; requires Beam first). Each tier raises damage,
  range, and cooldown speed.
- **Missiles** are racked up to four. `R` cycles a lock onto the
  nearest target in front; `A` fires.
- **ECM** is unlimited but cooldown-throttled, blanketing incoming
  missiles in a single burst.
- **Shields** absorb damage first and regenerate slowly (~37 s for a
  full top-up) **while still active** — once a shield is depleted to
  0 it stays down until you buy `REPAIR SHIELD` at the EQUIP shop.
  **Hull** only goes down — repair it the same way.
- **Sun heat** builds while skimming the star. Past 1.0 hullHeat
  forward shields drain, then aft. A `HEAT!` banner blinks faster
  as you approach the threshold.

### NPCs and factions

Every system spawns 1–4 NPC ships, roles weighted by government
(anarchy → mostly pirates; corporate → mostly patrols). Pirates are
always Cartel; everyone else inherits the system's faction.

There are four factions: **Imperium**, **Federation**, **Cartel**,
**Free Traders**. Your standing with each runs from -100 to +100 and
shifts on kills, trades, and quest payouts. Patrols turn hostile to
you once their faction's standing drops past -30, and market prices
flex up to ±20% with standing.

### Market

Seventeen commodities, from Food and Textiles up through Gold,
Platinum, and Aliens. Three are illegal (Slaves, Narcotics, Firearms)
and shrink under stable governments. Prices and stock are
deterministic per (system, commodity, market epoch); each hyperspace
jump bumps the epoch so prices shift slightly on a return visit.

### Quests

One active quest at a time. Accept at a planet, complete the
objective, and return to that same planet for the payout. Five
quest types are implemented (see `Quest.h`):

| Type        | Objective | Return required? |
|-------------|-----------|------------------|
| Patrol      | Kill N pirates in this system. | Yes |
| Delivery    | Drop N tons of a commodity at another planet POI. | Yes |
| VisitPlanet | Touch down at a specified planet POI. | Yes |
| Courier     | One-way drop of a passenger / packet at a planet POI. | **No** — paid on arrival |
| Scavenge    | Bring N tons of a commodity not stocked locally. | Yes |

Each quest carries a *difficulty* (EASY / MED / HARD / ELITE) derived
from distance from system 0, which scales both the size of the
objective and the payout. Patrol contracts also arm a one-shot pirate
spawn on the first launch after acceptance — killed pirates do not
respawn for that contract.

The pause menu always shows the single next step (e.g. `GO TO TRIX II`,
`KILL 2/4 PIRATES`, `RETURN TO TRIX`). The board itself renders the
narrative title (`COURIER PACKET TO TRIX II`, `BOUNTY: 3 PIRATES`,
etc.). Completion fires a modal popup on landing — press `Enter` to
dismiss before the docked menu becomes usable again.

### Equipment shop

Reached from the docked menu (`EQUIP`). All purchases debit
`game.credits` in tenths of a credit.

| Item        | Price | Effect |
|-------------|-------|--------|
| REPAIR HULL   | 10 CR per +10% | Patches hull damage (full repair = ~100 CR). |
| REPAIR SHIELD | 100 CR         | One-shot full recharge — required after depletion. |
| MISSILE     | 30 CR each, cap 4 | Adds one homing missile. |
| ECM SYSTEM  | 600 CR | Enables the ECM burst (one-time install). |
| LARGE HOLD  | 400 CR | Cargo capacity 20 t → 35 t. |
| BEAM LASER  | 1000 CR | Mid-tier laser. |
| MIL LASER   | 6000 CR | Top-tier laser (requires Beam). |
| DOCK COMP   | 1500 CR | Reserved for autodock assistance. |
| ESCAPE POD  | 1000 CR | Reserved for ejection on hull death. |

### Rank

The kill counter feeds a seven-tier rank ladder (Harmless → Mostly
Harmless → Poor → Average → Competent → Dangerous → Deadly). Crossing
a tier surfaces a `PROMOTED: <RANK>` banner in flight; the status
card shows your current tier and kills-to-next.

### Audio

A PWM beeper drives small SFX through the M5Unified speaker class:
laser zap (pitched per tier), shield hit, missile launch, ECM burst,
dock chime, witchspace whoosh, hostility alarm, quest accept /
complete chimes, and a denied-action buzz. Volume is fixed at 80/255;
the engine has no mute toggle yet.

---

## Code structure

Most code lives in single-header `.h` files included from `hazke.ino`,
the top-level state machine. Each module owns its own state and
exposes inline functions — there's no build system beyond the Arduino
IDE.

```
hazke.ino           top-level state machine + frame loop
Config.h            screen, frame, viewport constants
Input.h             keyboard polling + edge detection
GameMode.h          enum of screen states
GameState.h         mutable player state (credits, cargo, ship, faction)

Galaxy.h            16-system procgen + gate adjacency graph
SolarSystem.h       per-system POI table
Faction.h           faction map + standing nudges
Market.h            commodity table + deterministic pricing
Hyperspace.h        jump cost + neighbour gating
Rank.h              kill-tier ladder + promotion banner
Quest.h             quest data model + onDock / onPirateKill hooks
NPCShip.h           in-flight roster + AI (trader / pirate / patrol)
Combat.h            laser + missile damage resolution
Missile.h           homing missile motion + ECM
Particles.h         tiny pixel explosion / debris system
Audio.h             PWM SFX wrappers
Ship3D.h            wireframe ship renderer (6 hull silhouettes)
Starfield.h         parallax stars
Cockpit.h           HUD overlay (bars, banners, rank toast)
SystemFlight.h      the actual 3D flight loop + landing-range checks

TitleScreen.h       title menu
InfoScreen.h        controls help
AboutScreen.h       about / credits
ChartScreen.h       galactic gate-graph map
SystemDataScreen.h  per-system info panel
MapScreen.h         in-system top-down map
MarketScreen.h      commodity buy/sell UI
NPCTradeScreen.h    hail-in-flight buy/sell dialog
EquipScreen.h       equipment shop (includes REPAIR HULL)
StatusScreen.h      commander card
LandingScreen.h     planet surface menu
QuestScreen.h       planet quest board
PauseMenu.h         in-flight pause overlay (shows active quest step)
WitchspaceScreen.h  hyperspace transit cinematic
GameOverScreen.h    death + restart prompt
MenuUI.h            shared menu chrome (headers, footers, toasts)
```

## Design constraints

- ~320 KB usable RAM on the ESP32-S3. POI tables stay small (≤ 16
  POIs per system, ≤ 4 active NPCs).
- 240×135 @ 16 bpp via an `M5Canvas` double-buffer. Target ~30 FPS in
  flight; menus may drop to 15.
- All game state is in-RAM. Persistence (file/SD save) is the only
  major feature still on the roadmap.

## License

Original work. No code or art from the inspirations.
