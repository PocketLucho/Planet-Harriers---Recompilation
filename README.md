# PLANET HARRIERS - RECOMP

Static recompilation of **Planet Harriers** (Sega Hikaru, 2000) for Windows and Linux / Steam Deck.
Made by **Pocket_lucho** with Claude. [Leer en español](README.es.md)

It builds on the Hikaru engine of the sister project
[Star Wars Racer - Recomp](https://github.com/PocketLucho/Star-Wars-Racer---Recompilation): the game's SH-4
machine code is translated ahead of time to C (one function per basic block, hash-checked at run time) and
compiled to native code; whatever is not translated runs on a cycle-exact interpreter, so the whole game
always works. Everything else is an own implementation of the board: both SH-4s, the MIE I/O board
(Z80 + JVS), the two AICA sound boards (ARM7), the command processor and the GPU on Vulkan, the 2D layers
and the EEPROMs.

Support: https://buymeacoffee.com/pocket_lucho

## What you need

* Your own MAME romset `pharrier.zip`, with the Hikaru BIOS inside (`epr-23400a.ic94`, or
  `epr-23400.ic94` / `epr-21904.ic94`). With a split set (BIOS in a separate `hikaru.zip`), put `hikaru.zip`
  next to `pharrier.zip`.
* 64-bit Windows 10/11 or Linux x86_64, with a Vulkan 1.1 driver.

**This repository and its packages contain no game code, graphics, sound or BIOS.** The recompiled code is
generated on your PC from your ROM.

## How to use it

1. Copy `pharrier.zip` (and `hikaru.zip`, if your set is split) to the `roms` folder.
2. Run `BUILD_GAME.bat` (Windows) or `./build_game.sh` (Linux) once. It boots the game without a window to
   read its program, translates it to C and compiles it into `ph_game.dll` / `libph_game.so`. On Windows it
   downloads the llvm-mingw compiler (180 MB) the first time.
3. Run `PlanetHarriers-Recomp.exe` (or `./pharrier`).

Without step 2 it runs on the interpreter (slower; the window title says so). The generated module comes
from YOUR copy of the game: personal use only, do not distribute it.

## Default controls (F10 changes them)

| Action | Keyboard | Gamepad |
|---|---|---|
| Stick | Arrows | Left stick / d-pad |
| Shot (trigger) | Z | A / RT |
| Missile (lock-on) | X | X / B / LT |
| Bomb | C | Y |
| View change left / right | A / S | LB / RB |
| Start / Coin | 1 / 5 | Start / Back |
| Test / Service | F2 / F1 | |
| Pause, options, fullscreen, screenshot, quit | P, F10, F11, F12, Esc | L3+R3 options (Linux) |

Options: internal resolution 1x-8x, MSAA, 16:9 widescreen (optional clean edges: black bars beside 4:3-only
screens such as the title, full-width cutscene bars), back-face culling, fullscreen, country (Japan, USA,
Export, Australia, Korea; Export by default, English text), stick speed with keys, invert up/down,
interface language (Spanish / English).

## Status

See [docs/STATUS.es.md](docs/STATUS.es.md) (Spanish). In short: the game boots, takes coins and plays
(stages 1 to 3 covered by a scripted test run, shop included) with sound; 27,000 recompiled blocks. Some
graphics faults remain, and the Vulkan renderer has not yet been tried on a real GPU with this game.

## Building from source

```
third_party/fetch_deps.sh                      # Vulkan and SDL2 headers
cd emu
make hikaru_run                                # headless tests (software renderer)
make pharrier                                  # Linux (SDL2 + Vulkan)
TC=/path/to/llvm-mingw ./build_win.sh          # Windows (cross-compiled)
```

Technical notes: [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Credits and licence

GPL v2 (see `LICENSE`). The behaviour of the Hikaru board was checked against the documentation of
Tovarichtch's [Odin](https://github.com/Tovarichtch/Odin) emulator (levels of detail, polygon types, texture
and layer formats, ROM board map), MAME and the Valkyrie notes; no code was copied from those projects.
Planet Harriers is a trademark of SEGA; this project is not affiliated.
