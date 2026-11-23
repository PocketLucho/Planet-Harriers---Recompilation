#!/bin/bash
# Assembles the Windows package (zip) from the cross-built exe ($1) into $2. Engine and builder only:
# no game code, no ROM, no ph_game.dll.
set -e
cd "$(dirname "$(readlink -f "$0")")/../.."
EXE=$(readlink -f "$1"); OUT=$(readlink -f "${2:-dist/PlanetHarriers-Recomp-win64.zip}")
T=$(mktemp -d); P="$T/PHARRIERS-RECOMP"
mkdir -p "$P/roms" "$P/builder/sdk" "$P/licenses"
cp "$EXE" "$P/PlanetHarriers-Recomp.exe"
cp package/windows/BUILD_GAME.bat package/windows/CONSTRUIR_JUEGO.bat package/windows/LEEME.txt package/windows/README_EN.txt LICENSE "$P/"
cp builder/build_game.ps1 builder/module.mk builder/seeds_hikaru.txt "$P/builder/"
cp tools/blockgen.c tools/blockgen_ops.h emu/blockrt.h emu/optable.h emu/phmod.h emu/sh4.h emu/sh4exec.h emu/sh4sem.h emu/blk_module_game.c "$P/builder/sdk/"
cp emu/third_party/fonts/OFL-*.txt "$P/licenses/"
echo "stb_truetype.h: public domain / MIT (Sean Barrett). Fonts: SIL OFL 1.1 (see OFL-*.txt)." > "$P/licenses/THIRD-PARTY.txt"
rm -f "$OUT"; (cd "$T" && zip -qr - PHARRIERS-RECOMP) > "$OUT"; rm -rf "$T"
echo "package: $OUT"
