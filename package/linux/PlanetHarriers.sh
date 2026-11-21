#!/bin/bash
# PLANET HARRIERS - RECOMP : launcher for desktops and SteamOS (add this file to Steam as a non-Steam game).
cd "$(dirname "$(readlink -f "$0")")"
# SDL2: the system one (SteamOS and desktop distros ship it); otherwise the copy of the Steam Runtime
if ! ldconfig -p 2>/dev/null | grep -q 'libSDL2-2.0.so.0' && [ ! -e /usr/lib/libSDL2-2.0.so.0 ] && [ ! -e /usr/lib64/libSDL2-2.0.so.0 ]; then
    for d in "$HOME/.steam/root/ubuntu12_32/steam-runtime/usr/lib/x86_64-linux-gnu" "$HOME/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/x86_64-linux-gnu"; do
        [ -e "$d/libSDL2-2.0.so.0" ] && export LD_LIBRARY_PATH="$d${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" && break
    done
fi
# Steam Deck: Game Mode sets SteamDeck=1 (fullscreen, 16:9 by default). Gamescope handles the scaling.
export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0
exec ./pharrier "$@"
