PLANET HARRIERS - RECOMP (Windows x64 + Vulkan)
===============================================
Made by Pocket_lucho with Claude.  https://buymeacoffee.com/pocket_lucho
Source code and licence (GPL v2): https://github.com/PocketLucho/Planet-Harriers---Recompilation

This package contains NO game code, graphics, sound or BIOS.

Getting started
---------------
1. Copy your MAME romset pharrier.zip (with the BIOS epr-23400a.ic94 inside) to the "roms" folder.
2. Run BUILD_GAME.bat once. It downloads the llvm-mingw compiler (180 MB, first time only), reads
   the game program from your ROM, translates it to C and compiles it into ph_game.dll. It takes a
   few minutes. An internet connection is needed the first time.
3. Run PlanetHarriers-Recomp.exe.
Without step 2 the game runs on the interpreter (slower; the window title says so).
ph_game.dll comes from YOUR copy of the game: personal use only, do not distribute it.

Default keys (change them with F10)
-----------------------------------
  5 coin   1 start
  Arrows  stick      Z shot      X missile (lock-on)      C bomb      A / S view change
  F2 test (enters the service menu and confirms)   F1 service (moves the cursor)
  P pause   F10 OPTIONS   F11 fullscreen   F12 screenshot   Esc quit
XInput gamepad: left stick or d-pad = stick, A/RT shot, X/B/LT missile, Y bomb,
  LB/RB view change, Start start, Back coin.

Options (F10): language (automatic / espanol / English), internal resolution 1x-8x, MSAA,
16:9 widescreen, fullscreen, country (Japan, USA, Export/Europe, Australia, Korea),
stick speed with keys, invert up/down, gamepad and keys.

First release: some graphics faults remain. Detailed status in docs/STATUS.es.md of the repository.
