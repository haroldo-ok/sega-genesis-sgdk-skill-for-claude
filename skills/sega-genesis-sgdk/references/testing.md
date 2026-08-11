# Testing a Genesis ROM (host oracle + headless emulator)

"It compiled" is not "it works" on the Genesis — the default failure of a naive
build is a **valid ROM that boots to black**. Use two complementary tiers, both
runnable in CI with no display, GPU, or audio device. This mirrors the
`jazz-jackrabbit-for-sega-genesis` test suite; `assets/tests/` has ready
templates.

## Tier 1 — Host oracle (fast, logic-level)

Compile the **platform-clean core** (`src/core/`, the code with no SGDK/VDP
calls) with the *host* `cc` plus a test file, and assert on game state. No
emulator, no toolchain — runs in milliseconds, debuggable with gdb/asan.

`assets/tests/run_host_tests.sh` shape:

```sh
cc -std=c99 -Wall -Wextra -Werror -DHOST_BUILD \
   -Iinc -Isrc \
   src/core/game_core.c src/core/physics.c \
   tests/host_data.c tests/test_core_example.c \
   -o tests/.tmp/core_tests
tests/.tmp/core_tests          # exits non-zero on a failed assertion
```

Guard SGDK-only bits with `#ifndef HOST_BUILD` (or the inverse) so the same
`.c` compiles both for the host and into the ROM. Test the things a black-screen
test can't see cheaply: physics values, collision, economy math, state
transitions, and your **asset converter** (decode a synthetic file in the
original binary format and assert the resulting tiles/frames — no real game data
needed, so it's safe to ship).

## Tier 2 — Headless emulator (boots the real ROM)

Run the actual `out/rom.bin` under the **Genesis Plus GX** libretro core with no
display, feed scripted controller input, and assert on framebuffer pixels. This
is what proves "not a black screen" and "playable".

`assets/tests/run_headless_emulator_test.sh` does:

1. **Build the core once** (cached):
   ```sh
   git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git
   make -C Genesis-Plus-GX -f Makefile.libretro platform=unix -j"$(nproc)"
   # -> genesis_plus_gx_libretro.so
   ```
2. **Build the harness** (`assets/tests/libretro_harness.c`): a ~250-line C
   program that `dlopen`s the core, runs N frames with scripted input, dumps
   frames as PPM, and prints cartridge SRAM.
   ```sh
   cc -O2 -o harness tests/libretro_harness.c \
      -I Genesis-Plus-GX/libretro/libretro-common/include -ldl -lm
   ```
3. **Build the ROM** and run it through the harness.

### The input-script DSL

The harness takes: `harness CORE.so ROM.bin TOTAL_FRAMES SCRIPT OUT_PREFIX`.
`SCRIPT` is comma-separated directives:

```
120-160:start       hold START over frames 120..160
220-700:right       hold RIGHT over frames 220..700
420-424:b           tap B briefly
shot@110            write OUT_PREFIX110.ppm (a framebuffer snapshot)
```

Buttons: `up down left right a b c start`. So a title→play smoke test:

```sh
harness gpgx.so out/rom.bin 300 "shot@30,120-160:start,200-260:right,shot@250" frames_
```

### Assertions on the dumped frames

Read the PPMs and check, in a few lines of Python:

- **Not black / not frozen**: an early frame has many non-background pixels, and
  a later frame differs from the title frame (the game advanced). This is the
  core black-screen guard.
- **Colourful**: more than a handful of distinct colours (catches
  palette-never-loaded, where everything maps to index 0 = black).
- **Point-to-point**: after the START+Right script, the player/camera moved —
  compare a HUD/region crop between checkpoints.

### On-target self-test via SRAM marker (optional, strong)

For deep invariants, add assertions that run **on the emulated 68000**: build
with a define (e.g. `make EXTRA_FLAGS=-DSELFTEST`) so early boot runs your
core's checks against the converted data and writes a marker to SRAM
(e.g. bytes `'O','K',1`). The harness prints the first SRAM bytes; the wrapper
greps for the marker.

**SRAM interleave gotcha:** Genesis SRAM sits on **odd** addresses; Genesis Plus
GX dumps it interleaved with `0xFF`. So a marker `4F 4B 01` may appear as
`4F FF 4B FF 01`. Match both forms.

## Wiring it up

`assets/tests/run_all.sh` runs Tier 1 then Tier 2 and exits non-zero on any
failure. In CI: cache `$GDK`/toolchain and the built libretro `.so` on stable
keys; the emulator core build and the SGDK toolchain are the slow parts. Fail
the job if `out/rom.bin` is missing, the boot is black/frozen, a host assertion
fails, or the self-test marker is absent.

## When a test fails → triage

Map the symptom back to SKILL.md's "Black-screen triage": all-black →
palette/`SYS_doVBlankProcess`; frozen after title → blocking wait or logic-tick
stall; garbage tiles → VRAM overflow / stale rescomp output; red error screen →
address error (build `make debug`, read the dump, check ROM-array bounds and
host-vs-target `sizeof`); **renders but sprites frozen / RAM writes dead** →
library/compiler LTO mismatch (see the SKILL triage entry and toolchain doc).

## Debugging on target without a debugger (visual probes)

When the headless harness only gives you pixels, you can still read internal
state by **drawing it as tiles/markers** — cheap, deterministic, and enough to
localize a bug to logic vs rendering. Techniques that paid off:

- **State-to-column marker.** Draw a distinctive tile at
  `column = someValue`, on a fixed HUD row, once per frame:
  `VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0,0,0,0,TILE_USER_INDEX+3), value, row)`.
  Snapshot a few frames and read the column back in Python. Use it to watch a
  variable evolve (e.g. `player.tx` moving 9→4 proves the *logic* runs even when
  the *sprite* doesn't).
- **RAM-persistence probe.** `static u16 g; g++;` then draw `g` as a marker. If
  it never advances on-screen, RAM global writes aren't sticking → suspect the
  LTO/library mismatch or a broken `.data`/`.bss` init, not your game code.
- **Bypass the font.** The built-in font draws with **palette index 15**; if that
  entry is black your text is invisible (black-on-black) even though the code is
  right. For probes, use solid *tiles* (a known-lit tileset index) instead of
  `VDP_drawText`, and separately fix the font by setting the text palette's index
  15 to white: `PAL_setColor(PALn*16 + 15, RGB24_TO_VDPCOLOR(0xFFFFFF));`
- **Isolate rendering vs logic.** If a marker at `value` moves but the *sprite*
  doesn't, the bug is in sprite handling (or the sprite engine / library), not
  the simulation. Confirm the engine itself with the control test below.

## Control test: build an official SGDK sample

When you suspect the toolchain/library rather than your code, build one of
SGDK's own samples (e.g. `sample/fx/sprite-masking`) with the *same* toolchain
and run it in the harness. If its sprites/animation freeze too, the fault is your
build environment (very likely the LTO/library mismatch), not your game. This one
test collapses hours of guessing — reach for it early whenever "the engine seems
broken" rather than "my logic is wrong."

## Building the Genesis Plus GX core (proven)

The Tier-2 harness needs the libretro core as a native `.so`. Building it from
source works and is self-contained:

```sh
git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git gpgx
cd gpgx && make -f Makefile.libretro -j4      # -> genesis_plus_gx_libretro.so
```

Then `harness genesis_plus_gx_libretro.so out/rom.bin <frames> <script> <prefix>`
(see `assets/tests/libretro_harness.c`). The harness compiles clean against the
upstream `libretro.h`; keep that header alongside it.
