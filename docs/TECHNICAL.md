# Technical notes

## Layout

| Folder | What |
|---|---|
| `emu/` | The Hikaru machine and the SH-4 core (C). Frontends: `main_win32.c` (Windows), `main_sdl.c` (Linux / Steam Deck), `main_cli.c` (headless tests). `ph_input.h`: the cabinet controls shared by the frontends. |
| `tools/` | `blockgen.c` (block generator; `blockgen.py` is the reference it was ported from and produces identical output), generators of the opcode tables, analysis scripts. |
| `builder/` | What the user runs: `build_game.ps1`, makefiles, `sdk/` (the headers and generator the module is compiled with) and `seeds_hikaru.txt` (entry addresses only). |
| `package/` | The launch scripts that sit at the root of a released package. |
| `icons/` | Original icons and the script that draws them. |

## Block recompilation

* `blockgen` decodes basic blocks from a RAM image starting at the seed addresses and follows static
  branches. Each block becomes `B_<pc>_<fnv1a>(Cpu*)`, built from the same `SEM_OP_*` macros the interpreter
  uses (`sh4sem.h`), so both always agree.
* At run time `blockrt.c` looks the PC up in the table and compares the FNV-1a hash of the code in RAM with
  the one the block was generated from. A mismatch (self-modified or reloaded code) or a missing block falls
  back to the interpreter for that block. Timing is counted in cycles identically on both paths.
* Idle loops (vblank waits, the slave mailbox) are skipped only at the loop head while the condition holds.

## Game module

The engines link `blk_stub.c` (an empty table). At start they load `ph_game.dll` / `libph_game.so`
from their own folder (`blk_module.c`): the module exports
`ph_module_init(const PhHost*)` and returns `PhModule{abi, cpu_size, machine, table, count}`. The blocks
reach memory through the function pointers in `PhHost` (`phmod.h`). ABI number, `sizeof(Cpu)` and machine
name are checked; anything else means "no module" and the interpreter is used.

The builder runs the engine with `PH_BUILDER_DUMP=<dir>` (3000 frames headless on the interpreter, then
`ram_master.bin` / `ram_slave.bin`), feeds the images and the seeds to `blockgen`, compiles the result and
deletes every intermediate file. The game keeps its code where the boot copy puts it (no per-stage code
overlays were seen), so one RAM image is enough.

To extend coverage: run `hikaru_run -seen file` (or `-misses file`) through new parts of the game and add the
addresses (not the bytes) to `builder/seeds_hikaru.txt`.

## Hikaru

* Two SH-4 (master/slave) with their on-chip peripherals; the comm (link) board is absent.
* MIE: Z80 running the board's own program from the ROM set, JVS, two 93C46 EEPROMs. `pharrier.nv` holds the
  SRAM and both EEPROMs. Region is byte 5 of the MIE EEPROM; a blank chip gets the factory block (game ID
  "BCN") before the BOOTROM reads it.
* Sound: two AICA, each with an ARM7 running Sega's driver. Planet Harriers refuses to run without the second board.
* Command processor (`hikaru_cp.c`): viewports, model matrices, materials, texheads, lightsets, meshes.
  `hikaru_gfx.c` turns its output into clip-space vertices with per-vertex lighting and fog.
* Vulkan (`vkrender.c`, through volk): the TEXRAM is bound as it is (R16_UINT) and decoded, wrapped,
  mip-mapped and filtered in the fragment shader; reversed depth; the 2D layers of the Eurasia chip are
  composed over or under the 3D by coverage.
* Framebuffer blender (command 181: bit 24 enable, bits 16-23 rate): the 3D picture of the frame before is
  mixed over the new one before the 2D layers, `new = kept*k + drawn*(1-k)`. The game uses it while boosting.

## Planet Harriers specifics

* ROM board: EPROM pairs at bus pages 10-13 (8 MB each, mirrored in the page), mask ROM pairs of 32 MB at
  pages 20-2F; empty pages read 0xDEADBEEF. 315-5881 key 0x2912C68A.
* HLE boot (`emu_core.c`): at the BOOTROM's first vblank wait the program is copied as the header descriptor
  at 0x390 says, the slave is parked at its mailbox and the BOOTROM's "starting up" byte (GBR+20) is cleared.
* Idle loops: `0C0125CE` (vblank counters at GBR+0 / GBR+2) and `0C01C14A` (busy wait on an elapsed-time
  function built on TCNT0; the skipper evaluates the same expression from the routine's literal pool).
* Controls: JVS analog 0 = stick up/down (low = up), analog 1 = left/right (low = left); switches 0x0200
  trigger, 0x0100 missile, 0x0080 bomb, 0x0800 / 0x0400 view change.
* Command processor additions: LOD (005 / 055 / 095 / 561, jump conditions 1, 5, 6, 7, 9, D, call and return
  conditions 4, 8), local matrix, per-row modelview updates. Semantics follow the documentation in Odin's
  `antarctic_ops.cpp`.
* 2D layers: register 0x1B0 bits 1-2 select A2BGR10 (32 bits a pixel, colours four times brighter than their
  10 bits say); 0x1BC bits 1-2 say whether a layer is keyed by its alpha or covers everything.
