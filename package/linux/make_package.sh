#!/bin/bash
# Assembles the Linux / SteamOS package in $1 (default ../../dist/linux) from the built engine (emu/pharrier).
# Only the engine and the builder go in: no game code, no ROM, no libph_game.so.
set -e
cd "$(dirname "$(readlink -f "$0")")/../.."
OUT=${1:-dist/linux}
rm -rf "$OUT"; mkdir -p "$OUT/roms" "$OUT/builder/sdk" "$OUT/builder/tools" "$OUT/licenses"
cp emu/pharrier "$OUT/"; strip "$OUT/pharrier"
cp package/linux/PlanetHarriers.sh package/linux/build_game.sh package/linux/LEEME-README.txt "package/linux/Construir juego - Build game.desktop" "$OUT/"
cp LICENSE "$OUT/" 2>/dev/null || true
cp builder/seeds_hikaru.txt "$OUT/builder/"
cp tools/blockgen.c tools/blockgen_ops.h emu/blockrt.h emu/optable.h emu/phmod.h emu/sh4.h emu/sh4exec.h emu/sh4sem.h emu/blk_module_game.c "$OUT/builder/sdk/"
cc -O2 -o "$OUT/builder/tools/blockgen" tools/blockgen.c -Iemu && strip "$OUT/builder/tools/blockgen"
cp emu/third_party/fonts/OFL-*.txt "$OUT/licenses/"
echo "stb_truetype.h: public domain / MIT (Sean Barrett). Fonts: SIL OFL 1.1 (see OFL-*.txt)." > "$OUT/licenses/THIRD-PARTY.txt"
chmod +x "$OUT/pharrier" "$OUT/PlanetHarriers.sh" "$OUT/build_game.sh" "$OUT/Construir juego - Build game.desktop"
echo "package: $OUT"
