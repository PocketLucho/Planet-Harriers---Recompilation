#!/bin/bash
# PLANET HARRIERS - RECOMP : builder (Linux / SteamOS)
# Generates the recompiled game code from YOUR ROM and compiles it into libph_game.so.
# Uses the system C compiler (cc / gcc / clang). Without one (SteamOS), it downloads the Zig toolchain
# (a self-contained C compiler, ~50 MB, into builder/zig; no root and no system changes).
# Nothing derived from the game is distributed. / No se distribuye nada derivado del juego.
set -e
cd "$(dirname "$(readlink -f "$0")")"
say() { printf '\n\033[36m== %s\033[0m\n   %s\n' "$1" "$2"; }
fail() { printf '\n\033[31mERROR: %s\n       %s\033[0m\n' "$1" "$2"; rm -rf builder/gen builder/obj builder/work; [ -t 0 ] || sleep 8; exit 1; }
say "PLANET HARRIERS - RECOMP: construcción del código del juego" "PLANET HARRIERS - RECOMP: building the game code"

ROM=""; for f in roms/pharrier.zip pharrier.zip; do [ -z "$ROM" ] && [ -f "$f" ] && ROM="$f"; done
[ -n "$ROM" ] || fail "No hay nada que construir: copia pharrier.zip en 'roms'." "Nothing to build: copy pharrier.zip to 'roms'."
[ -x ./pharrier ] || fail "Falta el programa ./pharrier." "The ./pharrier program is missing."

# ---- C compiler: the system one, or Zig downloaded once into builder/zig ----
ZIGVER=0.13.0
ZIGDIR="builder/zig/zig-linux-x86_64-$ZIGVER"
if [ -n "$CC" ] && command -v "$CC" >/dev/null; then :
elif command -v cc >/dev/null; then CC=cc
elif command -v gcc >/dev/null; then CC=gcc
elif command -v clang >/dev/null; then CC=clang
else
    if [ ! -x "$ZIGDIR/zig" ]; then
        say "No hay compilador de C: descargando Zig $ZIGVER (~50 MB, solo esta vez)..." "No C compiler: downloading Zig $ZIGVER (~50 MB, only this once)..."
        mkdir -p builder/zig
        URL="https://ziglang.org/download/$ZIGVER/zig-linux-x86_64-$ZIGVER.tar.xz"
        if command -v curl >/dev/null; then curl -fL --progress-bar -o builder/zig/zig.tar.xz "$URL" || fail "No se pudo descargar Zig (¿hay conexión?)." "Could not download Zig (are you online?)."
        elif command -v wget >/dev/null; then wget -q --show-progress -O builder/zig/zig.tar.xz "$URL" || fail "No se pudo descargar Zig (¿hay conexión?)." "Could not download Zig (are you online?)."
        else fail "No hay compilador ni curl/wget." "No compiler and no curl/wget."; fi
        tar -xJf builder/zig/zig.tar.xz -C builder/zig || fail "No se pudo descomprimir Zig." "Could not unpack Zig."
        rm -f builder/zig/zig.tar.xz
    fi
    CC="$PWD/$ZIGDIR/zig cc"
fi
say "Compilador: $CC" "Compiler: $CC"

# blockgen (the generator) comes prebuilt; it is rebuilt only if it does not run here
rc=0; builder/tools/blockgen >/dev/null 2>&1 || rc=$?
if [ ! -x builder/tools/blockgen ] || [ $rc -ge 126 ]; then
    mkdir -p builder/tools && $CC -O2 -o builder/tools/blockgen builder/sdk/blockgen.c || fail "No se pudo compilar blockgen." "Could not compile blockgen."
fi

CFLAGS="-O2 -fPIC -fvisibility=hidden -fno-strict-aliasing -w -DNDEBUG -Ibuilder/sdk"
JOBS=$(nproc 2>/dev/null || echo 4)
compile_all() {  # out: compiles builder/gen/*.c + the module glue in parallel and links them (no make needed)
    local out="$1"
    mkdir -p builder/obj
    export CFLAGS CC
    ls builder/gen/*.c builder/sdk/blk_module_game.c | xargs -P "$JOBS" -I{} sh -c 'o=builder/obj/$(basename "{}" .c).o; $CC $CFLAGS -c "{}" -o "$o" || { echo "cc {} failed" >&2; exit 255; }' \
        || return 1
    $CC -shared -o "$out" builder/obj/*.o -lm -s
}
build() {  # name exe envvar out seeds image-specs...
    local name="$1" exe="$2" envn="$3" out="$4" seeds="$5"; shift 5
    rm -rf builder/gen builder/obj builder/work; mkdir -p builder/gen builder/obj builder/work
    say "$name: arrancando el juego para leer su programa..." "$name: booting the game to read its program..."
    local rc=0; env "$envn=builder/work" "./$exe" >/dev/null 2>&1 || rc=$?
    [ $rc -eq 4 ] && fail "$name: falta la BIOS de Hikaru. Tu romset está partido: copia también hikaru.zip en 'roms'." "$name: the Hikaru BIOS is missing. Your romset is split: copy hikaru.zip to 'roms' too."
    [ $rc -eq 5 ] && fail "$name: a pharrier.zip le faltan archivos del juego." "$name: pharrier.zip is missing game files."
    local args=(); for s in "$@"; do f="builder/work/${s#*:}"; f="${f%:*}"; [ -f "$f" ] || fail "$name: no se pudo leer el programa del juego." "$name: could not read the game program."; args+=(--image "${s%%:*}:$f:${s##*:}"); done
    say "$name: generando el código C..." "$name: generating the C code..."
    builder/tools/blockgen -o builder/gen -n 16 "${args[@]}" --seeds "builder/$seeds" || fail "$name: la generación ha fallado." "$name: generation failed."
    say "$name: compilando ($JOBS núcleos, unos minutos)..." "$name: compiling ($JOBS cores, a few minutes)..."
    compile_all "$out" || fail "$name: error de compilación." "$name: compilation error."
    rm -rf builder/gen builder/obj builder/work   # generated sources and RAM images contain code derived from the game
}
build "Planet Harriers" pharrier PH_BUILDER_DUMP libph_game.so seeds_hikaru.txt 0:ram_master.bin:0c000000 1:ram_slave.bin:0c000000
say "¡Listo! Ya puedes jugar." "Done! You can play now."
[ -t 0 ] || sleep 5
