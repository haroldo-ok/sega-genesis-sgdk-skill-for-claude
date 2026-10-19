---
name: sega-genesis-sgdk
description: >-
  Create, debug, optimize, and port games for the Sega Genesis / Mega Drive in C using SGDK (the Sega Genesis Development Kit). Use whenever the user wants to make a Genesis / Mega Drive game, port an existing game (from DOS, C, a Palm/6502/Java codebase, or any platform) to the Genesis, build or debug an SGDK project, fix a ROM that boots to a black screen or crashes, wire up graphics/sprites/tilemaps/scrolling, convert assets (tiles, sprite sheets, VGM/XGM music, S3M/MOD tracks, WAV samples) with rescomp, set up XGM/XGM2 sound, or add host + headless-emulator tests. Trigger even when the user only says "make a Genesis game", "port X to the Mega Drive", "compile this .res", or "my SGDK build is black screen", or mentions SGDK, rescomp, makefile.gen, VDP_, SPR_, XGM2_, SYS_doVBlankProcess, out/rom.bin, or the m68k-elf / Motorola 68000 toolchain. Prefer this skill over general knowledge for anything touching SGDK: the toolchain, resource format, and API names are easy to get wrong from memory.
---

# Sega Genesis / Mega Drive game development & porting (SGDK)

This skill turns a game — a new idea or an existing port target — into a
**playable, verified `out/rom.bin`** for the Sega Genesis / Mega Drive, written
in **C with SGDK**. It encodes the toolchain, project layout, the `rescomp`
resource pipeline, the runtime API, an optimization playbook, and a two-tier
test methodology (a host-compiled logic *oracle* plus a **headless emulator**)
that catches the number-one SGDK failure: a ROM that compiles cleanly but
**boots to a black screen**.

SGDK targets the **Motorola 68000** via `m68k-elf-gcc`, drives the **VDP** for
graphics, a **Z80 + YM2612 + PSG** for sound, and reads up to two 3-button (or
6-button) controllers. This skill is written against **SGDK 2.x** (2.12 at time
of writing; GCC 13.2). The authoritative references are the SGDK repo and the
`haroldo-ok` port collection — when unsure how something is really done, look at
how they do it:

- SGDK: https://github.com/Stephane-D/SGDK (the `sample/game/sonic` project is
  the canonical showcase; `bin/rescomp.txt` is the resource-format spec)
- Ports (study these for real porting patterns, one per genre):
  https://github.com/haroldo-ok/god-of-thunder-genesis (DOS action-adventure —
  the best worked port), `text-elite-genesis` (pure-C engine port),
  `spacetrader-genesis-`, `jazz-jackrabbit-for-sega-genesis` (platformer +
  host/emulator tests), `fzero-clone-md` (affine road streaming),
  `gemrush-genesis`, `xquest2-md`, `elifoot2-md`,
  `convert-s3m-vgm-jazzjackrabbit-md` (music pipeline), `catacomb-abbyss-md` /
  `catacomb-3d-md` / `wolf-3d-md` (id-engine raycasters from original data),
  `tuxemon-md` (RPG from Tiled maps + YAML data, compiled event scripts),
  `punch-out-pizza-tower-edition-md` (Scratch port, big characters on a plane,
  68000-driven FM music), and `hexgl-fmv-md` (delta-encoded FMV gameplay).

## Definition of done (do not stop at "it compiled")

An SGDK task is complete only when **all** of these hold. Treat it as a
checklist and report each item:

1. **It compiles** — `make` produces `out/rom.bin` with no errors, and rescomp
   compiled every `.res` (missing/renamed assets fail here).
2. **It fits** — the ROM is a sane size, RAM (64 KB of 68000 work RAM, shared
   with the stack and SGDK's heap) isn't blown by oversized static buffers, and
   big/immutable data lives in ROM, not RAM. `assets/tests/rom_sanity.py`
   checks the header (ROM-end vs real size, checksum, SRAM fields).
3. **It is not a black screen** — a headless emulator boots the real ROM and
   asserts rendered frames are lit and change over time (see
   `references/testing.md`). A compiling black screen is the default failure of
   a naive port; getting past it is the whole point.
4. **It is playable** — scripted controller input drives the game through its
   real states (title → gameplay) and checkpoint frames pass.
5. **The ROM is reachable by the user** — `out/rom.bin` is copied to a known
   output path and presented, never left only in a scratch build dir.

## Pick the workflow

- **Porting an existing game** (DOS, a C/Java/Pascal codebase, a Palm/6502
  game, an emulator core…) → read **`references/porting-workflow.md`**. This is
  the most common request. The method: split the game into a *platform-clean
  core* and a *thin SGDK shell*, build the core on the host first as an oracle,
  map each original subsystem to its Genesis equivalent, then bring it up on
  hardware incrementally. The system-mapping table there is the heart of it.
- **Creating a new game from scratch** → start from the project layout below
  and `references/api-and-engine.md`; the porting doc's bring-up order still
  applies.
- **Adding / converting assets** (graphics, maps, music, SFX) →
  **`references/resources.md`** (the `rescomp` `.res` format) and
  **`references/audio.md`** (XGM2/XGM, VGM, the S3M→VGM→XGM pipeline, WAV PCM).
- **Optimizing / improving** an existing project → `references/api-and-engine.md`
  "Performance" section (fixed-point, DMA, sprite/tile budgets, logic-rate
  decoupling).
- **Software 3D (raycasters), FMV, full-screen or big-character animation,
  anything uploading kilobytes per frame** → **`references/advanced-rendering.md`**
  (the ~7 KB-per-VBlank DMA budget, tear-free double buffering, RAM tile
  buffers, compiled scalers, streaming large characters onto a plane).
- **Debugging a black screen / crash** → "Black-screen triage" below and
  `references/testing.md`.

Whatever the workflow, wire up the tests from `references/testing.md` **early** —
they are how you *know* you're done rather than hoping.

## Step 0 — Toolchain

SGDK needs `m68k-elf-gcc` + the SGDK library/tools + **Java** (rescomp is Java).
On Linux/CI the reliable routes are the **doragasu docker image**
(native-Linux compiler, fastest), **MarsDev** (builds `m68k-elf-gcc` and drops
SGDK in), or — when Docker/gitlab are unavailable or a GCC build is too slow (a
restricted sandbox) — **Ubuntu's `gcc-m68k-linux-gnu` + a cloned SGDK tree**
(`SGDK_ROUTE=apt` in `assets/setup.sh`). The apt route is proven to build a
booting, playable ROM. `assets/setup.sh` is an idempotent installer. Full
details, env vars (`GDK`), the manual build pipeline, and the exact
library-build step are in **`references/toolchain-and-build.md`**. Verify with
`echo $GDK && m68k-elf-gcc --version && java -version`.

> ⚠️ If you did **not** get the library from a matching toolchain (e.g. you
> linked the shipped `libmd.a` with a different GCC), you can hit the **LTO trap**:
> the ROM boots and renders but sprites/logic freeze. Rebuild `libmd.a` from
> source with your compiler. See the toolchain doc and the triage section.

## Canonical project layout

Keep game logic separate from SGDK/hardware calls — that split is what makes a
port testable on the host and keeps the 68000-facing shell small.

```
mygame/
├── Makefile              # assets/Makefile: GDK, EXTRA_FLAGS (before the include),
│                         #   include $(GDK)/makefile.gen, LIBGCC override (after)
├── src/
│   ├── main.c            # int main(bool hardReset) — entry + game loop
│   ├── rom_header.c      # const ROMHeader rom_header = {...}  (region, name, SRAM)
│   ├── core/             # portable C: game logic, physics, state machines
│   │                     #   NO VDP/SPR/hardware calls — compiles on the host too
│   └── platform/         # thin SGDK shell: rendering, input, audio glue
├── inc/                  # your headers
├── res/                  # *.res descriptors + gfx/ sprite/ music/ sfx/ assets
│   └── resources.res     # IMAGE / SPRITE / TILESET / MAP / PALETTE / XGM2 / WAV / BIN
├── out/                  # BUILD OUTPUT — out/rom.bin lands here (also .lst, .map)
└── tests/                # host oracle + headless-emulator harness (assets/tests/)
```

Templates for `Makefile`, `main.c`, and `rom_header.c` are in `assets/`; copied
together with `assets/tests/` they build and pass both test tiers unmodified
(verified with SGDK 2.12 on the apt toolchain). Use the template `Makefile`,
not the bare 2-line form: it adds `-fno-store-merging` and links SGDK's
68000-safe `libgcc.a` with `--build-id=none`, three fixes every Linux-built
port in `examples.md` ended up needing. `rom_header.c` is special — it is
excluded from the source wildcard and defines the cartridge header (region
string `"JUE"`, ROM/RAM extents, SRAM enable).

## Build → verify loop

```sh
make                 # release build → out/rom.bin   (rescomp runs automatically)
make debug           # -O1 -ggdb, asserts, symbols (KDebug/KLog work in emulators)
make clean           # wipe out/ — REQUIRED after editing any header (see below)
python3 tests/rom_sanity.py out/rom.bin   # header: ROM end, checksum, SRAM
bash tests/run_host_tests.sh              # fast: compile the core on host, assert logic
bash tests/run_headless_emulator_test.sh  # boot rom.bin in Genesis Plus GX, assert pixels
```

`make` targets are `release` (default, `-O3 -flto`), `debug`, `asm`, and their
`clean-*` variants. Always look at `out/rom.bin`'s presence and size after a
build, and re-run rescomp-touching builds after changing any asset.
**Header dependencies are only partly tracked.** SGDK 2.12's `makefile.gen`
rebuilds after edits to *your* headers (verified), but not after edits to
SGDK's headers (they're `-isystem`, excluded by its `-MM` dependency pass), and
a header newly included *by another header* isn't tracked until the `.c` itself
changes; SGDK 2.11 and older were worse, and `makelib.gen` (the library build)
tracks nothing. So after changing a struct layout or anything under
`$GDK/inc`: `make clean` (or `rm -rf $GDK/out/release` for the library) —
stale objects built against an old layout produce "impossible" crashes.
The linked ELF is `out/release/rom.out` and `makefile.gen` writes a symbol map
to `out/release/symbol.txt` — what you need to turn a crash PC into a function.

## Hard constraints cheat-sheet

The VDP and 68000 impose limits SGDK does *not* hide. The most common
newcomer/port mistakes:

- **Colour is 9-bit BGR, 4 palettes × 16 colours; index 0 is transparent.**
  Source art in 24-bit RGB or VGA DAC 6-bit must be requantized. Classic bug:
  scaling VGA DAC (0–63) as if 8-bit, or ×4-ing 8-bit values — colours blow out
  (see God of Thunder's palette-scaling fix). Let rescomp own the conversion.
- **Two scroll planes (A/B) + a WINDOW plane + up to 80 hardware sprites**
  (max 20 per scanline, 320 px wide). Sprites are 8×8..32×32; the SGDK sprite
  engine (`SPR_*`) manages VRAM/attribute allocation for you — call
  `SPR_update()` once per frame.
- **VRAM is 64 KB** shared by tiles, tilemaps, sprite patterns, and H-scroll
  tables. Budget tiles; deduplicate with rescomp; stream large maps with `MAP_*`.
  The usable tile range ends at `TILE_USER_MAX_INDEX`, a *runtime* value that
  drops once `SPR_init()` reserves sprite VRAM (one port measured ~1,004 free
  user tiles after it vs ~1,424 before). Overrunning it silently wraps uploads
  into plane/sprite memory and corrupts graphics far from the bug — compute your
  highest tile index and halt at boot with a need-vs-have message if it exceeds
  `TILE_USER_MAX_INDEX`.
- **Per-frame DMA is ~7 KB** (SGDK's `DMA_TRANSFER_CAPACITY_NTSC` = 7,200 bytes
  per VBlank; more on PAL). Anything streaming tiles every frame lives or dies
  by this — see `references/advanced-rendering.md`.
- **Work RAM is 64 KB** (plus a separate 8 KB the Z80 owns), shared by your
  `.data`/`.bss`, the stack, and SGDK's heap. No `far` heap. Prefer static allocation and **keep
  large immutable data in ROM** (as `.res` `BIN`, generated `.inc`/arrays, or
  `MAP`/`IMAGE` resources) — never `memcpy` a whole level into RAM if you can
  read it from ROM.
- **Decouple logic rate from 60 fps.** Many source games tick at ~18–20 Hz;
  run game logic every *N*th frame with an accumulator, render every frame.
- **End every frame with `SYS_doVBlankProcess()`** (older tutorials say
  `VDP_waitVInt()` — that's outdated). Put per-frame VDP updates in a VBlank
  callback set via `SYS_setVIntCallback` when you need them mid-blank.
- **Everything is big-endian.** Byte-swap little-endian source assets (DOS
  files, LE binaries) at conversion time.
- **Save games use SRAM**, not files — enable it in `rom_header.c` and use the
  `SRAM_*` API. See `references/api-and-engine.md`.

## Black-screen triage

When a ROM compiles but shows black (or a frozen title), check in this order
(details in `references/testing.md`):

1. **Palette never loaded / all-zero** — non-zero pixels map to a black palette.
   Seed palettes (`PAL_setPalette`) before the first frame.
2. **`SYS_doVBlankProcess()` never called** — the main loop must call it every
   iteration or the display engine and DMA queue never advance.
3. **A blocking wait wedged the loop** — an infinite wait on input/audio, or a
   `while` with no `SYS_doVBlankProcess()`.
4. **Stale build products** — rescomp kept a renamed PNG's old symbol, or
   objects survived a header edit the dependency tracking missed (SGDK headers,
   newly nested includes, older SGDK); `make clean` and rebuild before
   debugging anything else.
5. **VRAM overflow / tile clobber** — tiles or sprite patterns overran a
   tilemap or the sprite engine's VRAM region; text/plane data got overwritten.
6. **DMA queue overflow in one frame** — too many/too-large transfers queued;
   split across frames or raise the queue size.
7. **Crash → register-dump screen or reset loop** — a bad pointer / odd-address
   access, or a 68020+-only instruction on the 68000. Build `make debug` and read
   the address-error dump. Recurring, easy-to-miss causes: (a) **odd-address
   word load** from a custom fixed-point read like `(s16)(mem32 >> 8)` — GCC
   (13.3 and 14 alike) emits `move.w x+1`, fine on 68020 but a crash on 68000. A
   plain inline helper does *not* help; force the long through a register with
   `__asm__("" : "+d"(v));` before the shift (`references/api-and-engine.md`
   → fixed-point). (b) **68020+ libgcc**
   — Debian's `-lgcc` `__divsi3` uses `bsr.l`, illegal on 68000; link SGDK's
   shipped `lib/libgcc.a` instead. (c) **GCC store merging** — two adjacent
   `u8` writes become one word write at an odd address (reproduced on GCC 13.3
   `-O3 -m68000`: `clr.w game+11`); build with `-fno-store-merging` (the template
   Makefile does). Also check ROM-array bounds and host-vs-target `sizeof`.
   SGDK 2.x's crash screen is a black register dump (yellow title, cyan labels);
   its `Offset:` line is the faulting PC — look it up in
   `out/release/symbol.txt`. `check_frames.py` detects this screen.
8. **Black before `main()` with a big `.bss`** — SGDK allocates its DMA queue
   and 8 KB staging buffer from the heap *during reset*, without checking for
   failure. Measured on SGDK 2.12 defaults: 46 KB of game arrays boots; at 50 KB
   and 56 KB `main()` is never reached; at 52 KB it boots with a NULL DMA buffer
   (a crash waiting to happen) — non-monotonic, so one lucky build proves
   nothing. Probe with `MEM_getFree()` at the top of `main()` (read it from a
   `ram@` dump). Fix: shrink the defaults in `$GDK/inc/dma.h` and **clean**-
   rebuild `libmd.a`, or override `DMA_init` in your project — see
   `references/toolchain-and-build.md` → "Boot-time heap exhaustion".
9. **Header claims more ROM than exists** — SGDK's template declares ROM end
   `$0FFFFF` regardless of size; usually ignored, but it has produced ILLEGAL
   INSTRUCTION crashes on hardware/strict emulators. `rom_sanity.py` prints the
   right value for `rom_header.c`.

**Distinct failure — "renders but frozen, RAM writes dead":** the ROM boots and
draws, *background* tiles even update, but **sprites never move and game state
looks stuck** — writes to RAM globals silently don't persist while direct VDP
writes work. This is almost always a **library / compiler LTO-version mismatch**,
not your code: SGDK's prebuilt `libmd.a` carries LTO bytecode tied to its exact
GCC build, and linking it with a different GCC (or forcing `-fno-lto` around the
mismatch) breaks the `.data` copy / DMA-flush paths. Confirm by building an
official SGDK sprite sample with the same toolchain — if *its* sprites freeze
too, it's the library. Fix: **rebuild `libmd.a` from source with your compiler**
and link with LTO (see `references/toolchain-and-build.md` → LTO trap). Quick
isolation test: a `static u16 g++;` global whose value you draw each frame — if
it never changes on-screen, RAM writes aren't sticking.

## Optimization quick rules (details in api-and-engine.md)

- Use **fixed-point** (SGDK `F16`/`F32`, e.g. 16.16), never `float`, in hot paths.
- **Bit-shift/mask** instead of `*`, `/`, `%` by powers of two.
- **Batch VRAM writes through DMA** (`DMA_*` / the sprite engine); avoid
  per-tile `VDP_setTileMapXY` in loops — build a row and blit it.
- **Pool and cull entities**: activate only near-camera actors; bound per-frame
  work (e.g. scan one map column per frame) so the 68000 keeps 60 fps.
- Precompute tables; hoist invariant work out of loops; for a per-pixel inner
  loop (wall/sprite scaling) generate unrolled 68000 code per case ("compiled
  scalers") rather than looping.
- Ship `release` (`-O3 -flto`); profile with `SYS_getFPS()` /
  `SYS_showFrameLoad()` in a debug HUD, or time stages in scanlines
  (`references/advanced-rendering.md` §6).

## Bundled resources

- `references/toolchain-and-build.md` — install routes (docker/MarsDev/native),
  `GDK`, `makefile.gen`, build targets, library build, CI.
- `references/api-and-engine.md` — the runtime API (VDP planes/tiles/text,
  palettes, the `SPR_*` sprite engine, `JOY_*` input, `DMA_*`, `MAP_*`, `SYS_*`,
  memory model, SRAM), plus the performance playbook.
- `references/resources.md` — the `rescomp` `.res` format for every resource
  type, image/palette constraints, and how resources appear in C.
- `references/audio.md` — XGM2/XGM drivers, VGM, the S3M/MOD→VGM→XGM pipeline,
  WAV PCM SFX, and the "framebuffer work starves the sound driver" gotcha.
- `references/porting-workflow.md` — the core/shell split, the DOS/legacy→
  Genesis system-mapping table, timing/memory/I-O/palette/input/save patterns,
  and incremental bring-up order.
- `references/advanced-rendering.md` — the VBlank DMA budget (SGDK-native cap
  vs a custom VInt job ring), tear-free double buffering (two tile banks, or a
  page flip by one vertical-scroll write), raycaster design (column-major RAM
  tile buffer, compiled scalers, DDA), big characters on a plane, FMV, and
  scanline profiling.
- `references/testing.md` — the host-core oracle, the headless Genesis Plus GX
  libretro harness and its script DSL (holds, `mash@`, `shot@`, `ram@`,
  `audiostat@`, pad 2, WAV capture), frame assertions including crash-screen
  detection, audio-energy assertions, RAM-dump post-mortems, the SRAM self-test
  marker, and the official-sample control test.
- `references/examples.md` — ~55 real SGDK projects categorized by technique: the
  large `haroldo-ok` ports plus a community index (raycasting/pseudo-3D, FMV,
  slope collision, scrolling taxonomy, variable-width fonts, Godot/Aseprite
  pipelines, shmups, roguelike, etc.), and a **1.x→2.x API-rename table** for
  making old samples compile. Mine the closest match before writing from scratch.
- `assets/` — ready-to-adapt `Makefile`, `main.c`, `rom_header.c`, `setup.sh`,
  and a `tests/` kit: `libretro_harness.c`, `check_frames.py` (lit / colourful /
  advancing / not a crash screen), `rom_sanity.py` (header checks), `ramsym.py`
  (read variables and a crash stack out of a RAM dump), `run_host_tests.sh`,
  `run_headless_emulator_test.sh`, `run_all.sh`, `test_core_example.c`.
