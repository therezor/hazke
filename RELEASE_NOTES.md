# Hazke v1.0 — Cardputer Edition

The first full release of **Hazke**: a complete open-universe space-sim that runs
entirely on the M5Stack Cardputer — no phone, no cloud, no second device. Trade,
hunt and survive across sixteen procedurally-generated star systems on a 240×135
ESP32 handheld. All code and the entire universe are original, written in C++.

## Features

- **A living galaxy** — 16 star systems linked by jump gates, each with its own
  economy, government, tech level and population, all grown from a single seed.
- **Free-flight cockpit** — real 3D space with coordinated banking turns, shaded
  polygon planets, ringed worlds, asteroid belts, and a sun hot enough to cook
  your hull.
- **A living economy** — 17 commodities priced by supply, local law and your
  reputation. Buy low under one government, run contraband past another.
- **Combat** — pulse, beam and military lasers, up to four homing missiles with
  target lock, regenerating shields, and an ECM blast.
- **Contracts & progression** — patrol, delivery, courier, recon and scavenge
  missions; 4 factions that track every kill and trade; 7 combat ranks from
  Harmless to Deadly.
- **Screenshots to SD** — press **Ctrl+Space** in-game to save the current frame
  to the microSD card as a 24-bit BMP (`/hazke/shotNNNN.bmp`).

## Controls

| Key | Action |
| --- | --- |
| `↑` / `↓` | Pitch up / down |
| `←` / `→` | Roll |
| `E` / `S` | Accelerate / brake |
| `W` | Fire laser |
| `R` / `A` | Lock / fire missile |
| `Q` | ECM blast |
| `M` | System map |
| `Ctrl`+`Space` | Screenshot to SD |
| `` ` `` / `ESC` | Back / pause / title |

Fly into a planet to land; fly into a jump gate to open the galaxy chart.

## Install

Flash the attached firmware to a Cardputer over USB-C:

```
esptool.py --chip esp32s3 write_flash 0x0 hazke-v1.0.0-cardputer.bin
```

Or load `hazke-v1.0.0-cardputer.bin` in M5Burner, or build `hazke.ino` from
source in the Arduino IDE with the board set to **M5Cardputer**.
