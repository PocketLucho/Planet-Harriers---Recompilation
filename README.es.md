# PLANET HARRIERS - RECOMP

Recompilación estática de **Planet Harriers** (Sega Hikaru, 2000) para Windows y Linux / Steam Deck.
Hecho por **Pocket_lucho** con Claude. [Read in English](README.md)

Parte del motor Hikaru del proyecto hermano
[Star Wars Racer - Recomp](https://github.com/PocketLucho/Star-Wars-Racer---Recompilation): el código máquina
SH-4 del juego se traduce por adelantado a C (una función por bloque básico, comprobada por hash al
ejecutar) y se compila a código nativo; lo que no esté traducido lo ejecuta un intérprete exacto al ciclo,
así que el juego entero funciona siempre. Todo lo demás es una implementación propia de la placa: los dos
SH-4, la placa de E/S MIE (Z80 + JVS), las dos placas de sonido AICA (ARM7), el procesador de comandos y la
GPU sobre Vulkan, las capas 2D y las EEPROM.

Si quieres apoyarme: https://buymeacoffee.com/pocket_lucho

## Qué necesitas

* Tu propio romset de MAME `pharrier.zip`, con la BIOS de Hikaru dentro (`epr-23400a.ic94`, o
  `epr-23400.ic94` / `epr-21904.ic94`). Con un romset partido (la BIOS en un `hikaru.zip` aparte), pon
  `hikaru.zip` junto a `pharrier.zip`.
* Windows 10/11 de 64 bits o Linux x86_64, con un driver Vulkan 1.1.

**Este repositorio y sus paquetes no contienen código, gráficos, sonido ni BIOS del juego.** El código
recompilado se genera en tu PC a partir de tu ROM.

## Cómo se usa

1. Copia `pharrier.zip` (y `hikaru.zip`, si tu romset está partido) en la carpeta `roms`.
2. Ejecuta una vez `CONSTRUIR_JUEGO.bat` (Windows) o `./build_game.sh` (Linux). Arranca el juego sin ventana
   para leer su programa, lo traduce a C y lo compila en `ph_game.dll` / `libph_game.so`. En Windows
   descarga el compilador llvm-mingw (180 MB) la primera vez.
3. Ejecuta `PlanetHarriers-Recomp.exe` (o `./pharrier`).

Sin el paso 2 funciona con el intérprete (más lento; lo dice el título de la ventana). El módulo generado
sale de TU copia del juego: es para uso personal, no lo distribuyas.

## Controles por defecto (F10 los cambia)

| Acción | Teclado | Mando |
|---|---|---|
| Palanca | Flechas | Stick izquierdo / cruceta |
| Disparo (gatillo) | Z | A / RT |
| Misil (lock-on) | X | X / B / LT |
| Bomba | C | Y |
| Cambio de vista izq. / der. | A / S | LB / RB |
| Start / Moneda | 1 / 5 | Start / Back |
| Test / Service | F2 / F1 | |
| Pausa, opciones, pantalla completa, captura, salir | P, F10, F11, F12, Esc | L3+R3 opciones (Linux) |

Opciones: resolución interna 1x-8x, MSAA, panorámico 16:9 (bordes limpios opcionales: bandas negras junto a las pantallas solo 4:3, como el título, y franjas de cine a todo lo ancho), ocultar caras traseras, pantalla completa, país (Japón, EE. UU.,
Exportación, Australia, Corea; por defecto Exportación, textos en inglés), velocidad de la palanca con
teclas, invertir arriba/abajo, idioma de la interfaz (español / inglés).

## Estado

Ver [docs/STATUS.es.md](docs/STATUS.es.md). Resumen: el juego arranca, acepta monedas y se juega (fases 1 a 3
recorridas con un guion de pruebas, tienda incluida) con sonido; 27.000 bloques recompilados. Quedan fallos
gráficos por pulir y el renderizador Vulkan aún no se ha probado en una GPU real con este juego.

## Compilar desde el código

```
third_party/fetch_deps.sh                      # cabeceras de Vulkan y SDL2
cd emu
make hikaru_run                                # pruebas sin ventana (renderizador por software)
make pharrier                                  # Linux (SDL2 + Vulkan)
TC=/ruta/a/llvm-mingw ./build_win.sh           # Windows (compilación cruzada)
```

Notas técnicas: [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Créditos y licencia

GPL v2 (ver `LICENSE`). El comportamiento de la placa Hikaru se ha contrastado con la documentación del
emulador [Odin](https://github.com/Tovarichtch/Odin) de Tovarichtch (niveles de detalle, tipos de polígono,
formatos de textura y de capas, mapa de la placa de ROM), con MAME y con las notas de Valkyrie; no se ha
copiado código de esos proyectos. Planet Harriers es una marca de SEGA; este proyecto no está afiliado.
