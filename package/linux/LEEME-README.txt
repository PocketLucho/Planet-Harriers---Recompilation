PLANET HARRIERS - RECOMP (Linux x86_64 / SteamOS / Steam Deck, SDL2 + Vulkan)
=============================================================================
Pocket_lucho + Claude.  https://github.com/PocketLucho/Planet-Harriers---Recompilation  (GPL v2)

ES  1. Copia tu romset de MAME pharrier.zip en la carpeta "roms".
    2. Ejecuta una vez ./build_game.sh (o abre "Construir juego - Build game"). Usa el compilador de C
       del sistema; si no hay (SteamOS) descarga Zig una sola vez (~50 MB) en builder/zig, sin root.
       Sin este paso el juego funciona con el intérprete (más lento).
    3. ./PlanetHarriers.sh  (o ./pharrier). Al iniciar sale el lanzador; se puede desactivar en
       Ajustes > Sistema.
    libph_game.so sale de TU ROM: no lo distribuyas.

    Steam Deck / SteamOS
    - Modo escritorio: copia la carpeta (p. ej. ~/Games/PlanetHarriers), pon pharrier.zip en "roms"
      y abre "Construir juego - Build game" (o ./build_game.sh en Konsole).
    - Steam > Añadir un juego > Añadir un juego que no es de Steam > Examinar > PlanetHarriers.sh.
    - En modo juego arranca a pantalla completa y en 16:9; el mando de la Deck funciona como un mando
      de Xbox (plantilla "Mando" / "Gamepad").
    - Menú: View+Menu (Select+Start) o L3+R3. Moneda: View (Select). Start: Menu.

EN  1. Copy your MAME romset pharrier.zip to the "roms" folder.
    2. Run ./build_game.sh once (or open "Construir juego - Build game"). It uses the system C compiler;
       without one (SteamOS) it downloads Zig once (~50 MB) into builder/zig, no root needed.
       Without this step the game runs on the interpreter (slower).
    3. ./PlanetHarriers.sh  (or ./pharrier). The launcher shows first; it can be turned off in
       Settings > System.
    libph_game.so comes from YOUR ROM: do not distribute it.

    Steam Deck / SteamOS
    - Desktop Mode: copy the folder (e.g. ~/Games/PlanetHarriers), put pharrier.zip in "roms" and open
      "Construir juego - Build game" (or ./build_game.sh in Konsole).
    - Steam > Add a Game > Add a Non-Steam Game > Browse > PlanetHarriers.sh.
    - In Game Mode it runs fullscreen and 16:9; the Deck controls work as an Xbox pad ("Gamepad" layout).
    - Menu: View+Menu (Back+Start) or L3+R3. Coin: View (Back). Start: Menu.

Mando / gamepad: left stick or d-pad, A/RT shot, X/B/LT missile, Y bomb, LB/RB view change.
Teclas / keys: 5 coin, 1 start, arrows stick, Z shot, X missile, C bomb, A/S view change,
F2 test, F1 service, P pause, Esc/F10 menu, F11 fullscreen, F12 screenshot.
Ajustes / settings: pharrier.ini next to the program (or ~/.config/pharrier-recomp if it is read-only).
