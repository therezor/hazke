# Hazke

Hazke is a space trading and combat game for the M5Stack Cardputer. You
fly between sixteen star systems, trade cargo, take jobs from planets
and fight pirates on a 240×135 screen.

![Gameplay recorded on a Cardputer: landing on a planet, autolock, a pirate kill and a hyperspace jump](website/media/gameplay.gif)

**[Download v1.3](https://github.com/therezor/hazke/releases/download/v1.3.0/hazke-v1.3.0-cardputer.bin)**
· [All releases](https://github.com/therezor/hazke/releases)

The game takes ideas from *Elite*, *Parkan: Imperial Chronicles* and
*Galaxy on Fire 2*. All of its code and art are original.

## Install

Pick Hazke from your launcher's app list, or flash the firmware yourself.

**Prebuilt firmware.** Download `hazke-v1.3.0-cardputer.bin` from the
[latest release](https://github.com/therezor/hazke/releases/latest) and
flash it with M5Burner or esptool:

```
esptool.py --chip esp32s3 write_flash 0x0 hazke-v1.3.0-cardputer.bin
```

This full image also clears the saves in internal flash. To keep them
when upgrading a Cardputer that already runs Hazke, flash only the game
from `hazke-v1.3.0-app.bin`:

```
esptool.py --chip esp32s3 write_flash 0x10000 hazke-v1.3.0-app.bin
```

You can also copy your slots to the SD card first with `COPY` in the
save menu.

**From source.** Install the `M5Cardputer` library in the Arduino IDE.
It installs `M5Unified` and `M5GFX` with it. Open `hazke.ino`, pick the
`M5Cardputer` board and upload. With arduino-cli:

```
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer --upload -p <port> .
```

## New in v1.3

Every clip below is a capture of a Cardputer's screen.

<table>
<tr>
<td width="50%"><img src="website/media/autolock.gif" alt="Autolock turning the ship onto a pirate"><br>
<b>Autolock.</b> The module costs 300 CR. Press <code>F</code> and the ship turns onto your target and keeps pointing at it. Holding a pitch or yaw key overrides it.</td>
<td width="50%"><img src="website/media/scanner.gif" alt="3D scanner while the ship pitches"><br>
<b>3D scanner.</b> The scanner is a tilted disk. A line from each contact to the disk shows whether the contact is above or below you.</td>
</tr>
<tr>
<td><img src="website/media/hit-arcs.gif" alt="Red hit arc on the scanner rim"><br>
<b>Hit arcs.</b> A red arc on the scanner rim points at whoever just hit you, and follows them as you turn.</td>
<td><img src="website/media/combat.gif" alt="Missile hit and a pirate kill"><br>
<b>Rockets and a new cockpit.</b> Missiles look like rockets and leave an exhaust trail. The gauges have segments, and the rack draws each missile you carry.</td>
</tr>
<tr>
<td><img src="website/media/system-map.gif" alt="System map with zoom"><br>
<b>System map.</b> The map is bigger, and <code>F</code> zooms in 2x. Hostile ships show red and friendly ones green, on the map and on the scanner.</td>
<td><img src="website/media/deep-space.gif" alt="Flying back into a system from deep space"><br>
<b>Open space.</b> Systems have no walls. Past 30K from the sun you are in deep space, and the HUD shows your distance to the sun.</td>
</tr>
</table>

Every sound effect is new. Effects play on separate channels, so one no
longer cuts off another, and the title and pause menus have a volume
setting with four levels. The display is double-buffered now, so the game
draws the next frame while the screen shows the last one.

## How to play

You start next to a jump gate with 100 credits and an empty hold.

- **Trade.** Buy goods where they're cheap and sell them where they're
  scarce. Farm worlds sell food cheap and pay well for machinery, and
  industrial worlds do the opposite. Three goods are illegal, and stable
  governments stock less of them.
- **Take jobs.** Land on a planet and open the job board. Jobs include
  hunting pirates, delivering cargo, bringing goods the local market
  doesn't stock, visiting a planet and carrying a courier package. Most
  jobs pay when you return to the planet that offered them. Courier jobs
  pay on arrival.
- **Fight.** Pirates attack on sight. Below 50% hull a ship's engines
  drop to half power, and below 25% its weapons stop working. Fly up to
  a beaten ship and press `H` to take its cargo.
- **Travel.** Fly into a jump gate to open the galactic chart. You can
  jump only to systems linked by a gate, and a jump costs 10 CR per
  light year.
- **Land.** Fly into a planet. Landing opens the market, the equipment
  shop, the job board and the save menu.

Kills, trades and jobs raise or lower your standing with four factions.
If your standing with a faction drops below -30, its patrols attack you.
Standing also moves prices by up to 20% either way. Kills raise your
combat rank through seven levels, from Harmless to Deadly.

A shield recharges fully in about 37 seconds as long as it stays above
zero. Once it hits zero it stays down until you pay for a repair. Only
the equipment shop can repair hull damage.

### Equipment

| Item | Price | Effect |
|------|-------|--------|
| Repair hull | 10 CR per 10% | Fixes hull damage |
| Repair shield | 100 CR | Brings a downed shield back |
| Missile | 30 CR | Homing missile, up to 4 |
| ECM | 600 CR | Blows up nearby missiles (`Q`) |
| Autolock | 300 CR | Turns the ship onto your target (`F`) |
| Large hold | 400 CR | Cargo space from 20 t to 35 t |
| Beam laser | 1000 CR | More damage and range |
| Military laser | 6000 CR | The best laser, needs the beam laser first |

## Controls

The arrow keys are the `;` `,` `.` `/` keys, marked with arrows on the
Cardputer.

| Key | Action |
|-----|--------|
| `↑` `↓` | Pitch |
| `←` `→` | Roll |
| `L` `'` | Yaw |
| `E` / `S` | Speed up / slow down |
| `W` or `Space` | Fire laser |
| `R` | Cycle missile lock through ships ahead |
| `A` | Fire a missile |
| `Q` | ECM |
| `F` | Autolock, or zoom on the system map |
| `Tab` | Next target (ships and places) |
| `H` | Hail or loot a nearby ship |
| `M` | System map |
| `Enter` | Confirm |
| `` ` `` | Back / pause |
| `Ctrl` + `Space` | Screenshot to the SD card |

## Saving

Save from the landing menu of any planet, and load from the title
screen. There are five slots in internal flash and five on the microSD
card, and `COPY` in the save menu copies a slot from one to the other. Saves from older
versions load in newer ones. A save made by a newer version shows as
`NEWER VERSION` and is left alone.

v1.3 changed the save format. v1.2 saves load fine, but once you save
a slot in v1.3, v1.2 can't read it.

## Serial console

With the Cardputer connected over USB, a serial terminal at 115200 baud
accepts these commands:

| Command | What it does |
|---------|--------------|
| `status` | Prints the commander, screen, credits and system |
| `credits <CR>` | Sets your credits |
| `save [1-5] [sd\|int]` | Saves to a slot |
| `cap on` / `cap off` | Pauses the game so it only advances on `step` / `rec` |
| `keys <chars>` | Holds those keys down (`T` is Tab, `N` is Enter) |
| `step <n>` / `rec <n>` | Runs `n` frames of 1/25 s each; `rec` also sends each frame |
| `shot` | Sends the current frame |

The firmware sends frames as run-length packed RGB565, and `Capture.h`
describes the format. `tools/capture.py` sends these commands from a
computer and saves the frames as PNGs. The clips in this README come
from it.

## Code

The game is a set of single-header modules included from `hazke.ino`,
which runs the screen state machine and the frame loop.

```
hazke.ino           state machine, frame loop, serial console
Config.h            version, screen layout, build switches
Input.h             keyboard polling and keys held by the console
Capture.h           frame capture over serial
GameState.h         credits, cargo, ship, factions
Galaxy.h            the 16 systems and their gate links
SolarSystem.h       planets, belts and the gate in each system
SystemFlight.h      3D flight, camera, autolock, landing
NPCShip.h           traders, pirates and patrols
Combat.h            lasers and damage
Missile.h           homing missiles and ECM
Radar.h             3D scanner and hit arcs
Cockpit.h           gauges, missile rack, footer
Raster.h            clipped line and triangle drawing
Ship3D.h            ship models
Sky.h               distant stars and space dust
Rocket.h            missile art
Starfield.h         hyperspace tunnel
Particles.h         explosions and debris
Audio.h             sound synthesis and mixing
Market.h            prices and stock
Quest.h             jobs
Faction.h           faction standing
Rank.h              combat rank
Hyperspace.h        jump cost and range
SaveFormat.h        save layout, checksum, upgrades between versions
SaveStore.h         save slots on flash and SD
SDCard.h            shared microSD setup
Screenshot.h        Ctrl+Space screenshots
*Screen.h, MenuUI.h, PauseMenu.h   menus and screens
```

## License

Original work. Hazke uses no code or art from the games that inspired it.
