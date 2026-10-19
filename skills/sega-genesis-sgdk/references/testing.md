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
2. **Build the harness** (`assets/tests/libretro_harness.c`): a ~380-line C
   program that `dlopen`s the core, runs N frames with scripted input on one or
   two pads, and records frames (PPM), work-RAM dumps, audio energy, an
   optional WAV, and cartridge SRAM. It needs `libretro.h` (from the GPGX
   checkout, or point `LIBRETRO_INC` at a directory holding it).
   ```sh
   cc -O2 -o harness tests/libretro_harness.c \
      -I Genesis-Plus-GX/libretro/libretro-common/include -ldl -lm
   ```
3. **Build the ROM**, run `rom_sanity.py` on it, and run it through the harness.

The whole runner, templates included, has been run end to end against SGDK 2.12
on the apt toolchain (the template project passes both tiers unmodified).

### The input-script DSL

The harness takes: `harness CORE.so ROM.bin TOTAL_FRAMES SCRIPT OUT_PREFIX`.
`SCRIPT` is comma-separated directives (frames count from 0):

```
120-160:start           hold START over frames 120..160
220-700:right           hold RIGHT over frames 220..700
420-424:b               tap B briefly
300-360:p2.left         hold LEFT on controller 2 (two-player / twin-stick)
mash@400-600:10:a+b     alternate A and B, each held for half of a 10-frame
                        period (get-up mashing, menu skipping, "press anything")
shot@110                write OUT_PREFIX110.ppm (a framebuffer snapshot)
ram@110                 write OUT_PREFIX110.ram (64 KB of 68000 work RAM)
audiostat@110           print "audiostat@110 samples=N avg_energy=E": the mean
                        |sample| since the previous audiostat (or boot)
```

Buttons: `up down left right a b c start`, optionally prefixed `p2.`. Set
`HARNESS_WAV=out.wav` to also save everything the console played (16-bit
stereo at the core's rate). A title→play smoke test:

```sh
harness gpgx.so out/rom.bin 300 "shot@30,120-160:start,200-260:right,shot@250" frames_
```

Tested on a commercial-scale ROM (the Catacomb 3-D port): `mash@400-600:10:a+b`
got from the title into level 1, `ram@` dumps decoded, the WAV came out at
15.02 s for 900 frames, and audio energy went 0 → 389 when the music started.

### Assertions on the dumped frames

Read the PPMs and check, in a few lines of Python:

- **Not black / not frozen**: an early frame has many non-background pixels, and
  a later frame differs from the title frame (the game advanced). This is the
  core black-screen guard.
- **Colourful**: more than a handful of distinct colours (catches
  palette-never-loaded, where everything maps to index 0 = black).
- **Point-to-point**: after the START+Right script, the player/camera moved —
  compare a HUD/region crop between checkpoints.
- **Not a crash screen.** SGDK 2.x's default exception handlers reinitialise the
  VDP and print a register dump. Captured from a real address error under
  Genesis Plus GX it is exactly four colours: black (~78%), cyan labels
  `(168,236,232)` (~18%), white values, and a yellow title line `(232,204,0)`
  ("ADDRESS ERROR", "BUS ERROR", ...). `check_frames.py` keys on that
  cyan+yellow pair — a "mostly black, few colours" rule would also fire on
  ordinary text screens (Catacomb's intro text is 89% black, 2 colours). The
  dump's `Offset:` line is the faulting PC: `$5D4C` in the test ROM fell inside
  `main.isra.0` per `out/release/symbol.txt`.
- **Sound actually plays** (not merely links): bracket the frame that triggers a
  sound with `audiostat@` checkpoints and assert the energy jumps (Tuxemon MD
  sees near-zero while walking and 100–300× after a cry or menu confirm). For
  pitch or speed, capture `HARNESS_WAV` and check duration and the FFT peak —
  that is how the XGM2 rate rule in `audio.md` was measured.
- **Cadence**, for streamed video/animation: snapshot every Nth frame and assert
  each interval carries a new image (hexgl-fmv-md asserts 15/15 four-frame
  intervals change at 15 fps).

### Header checks (`rom_sanity.py`)

`python3 tests/rom_sanity.py out/rom.bin [--strict]` reads the cartridge header:
console name, **declared ROM end vs actual file size**, the checksum, and the
SRAM fields. The checksum comes in two formats and the script reports both:
SGDK's (what `sizebnd -checksum` writes and `SYS_isChecksumOk()` checks — XOR of
all 32-bit words with the field zeroed, folded to 16 bits) and the classic Sega
16-bit word sum from `$200` (what emulators display, so they'll usually call an
SGDK ROM's checksum "bad"; harmless). Of seven real ROMs checked, six declared a
ROM end larger than the file (the template's `$0FFFFF`/`$3FFFFF`), which most
emulators ignore — Tuxemon MD traced an ILLEGAL INSTRUCTION crash to it and now
fails its test on any mismatch; use `--strict` for the same.

### Post-mortems from RAM dumps (`ramsym.py`)

A `ram@N` dump plus the ELF answers "what was the game doing" without a
debugger:

```sh
python3 tests/ramsym.py frames_250.ram out/release/symbol.txt score:4 lives:1
python3 tests/ramsym.py frames_250.ram out/release/rom.out --stack   # return addrs
```

Genesis Plus GX exposes work RAM **byte-swapped per 16-bit word** (a high-score
name "Sir Lancelot" reads `iS raLcnleto` raw); `ramsym.py` swaps it back
(`--raw` to skip). Typical uses: confirm `main()` was reached at all (store a
magic value as the first statement), read `MEM_getFree()` and DMA buffer
pointers for the boot-heap problem, read the state machine's current state,
and scan the stack for the call chain of a hang.

**Prefer RAM reads to debug-HUD text for exact state.** On-screen debug
readouts only update when the code that draws them runs: Tuxemon MD found its
HUD frozen mid-transition because a per-frame VM flipped the state machine
before the drawing case executed, which looked like a bug that wasn't there.
Read values from a frame where the game is in a stable state, or from RAM.

### Deeper: CPU registers and a cycle profiler

Wolf3D MD extends this harness with two optional, core-specific tricks. (1)
**68000 registers per snapshot**: `m68k_get_reg` is a local symbol in the GPGX
`.so`, so its address is computed as `dlsym("retro_run")` minus `retro_run`'s
`nm` address plus `m68k_get_reg`'s, then PC/SR/A7/D0/A0 are printed at each
shot — enough to see where a hang spins. (2) A **cycle profiler**: a patched
core accumulates cycles per PC word into a global array, the harness dumps it,
and a script maps addresses to functions (`nm`) and lines (`addr2line`). Both
need a non-stripped core you build yourself; reach for them when the frame
budget is the problem.

### On-target self-test via SRAM marker (optional, strong)

For deep invariants, add assertions that run **on the emulated 68000**: build
with a define (e.g. `make EXTRA_FLAGS=-DSELFTEST`) so early boot runs your
core's checks against the converted data and writes a marker to SRAM
(e.g. bytes `'O','K',1`). The harness prints the first SRAM bytes; the wrapper
greps for the marker.

**SRAM interleave gotcha:** Genesis SRAM usually sits on **odd** addresses, and
Genesis Plus GX's save buffer then shows the data on odd offsets with `0xFF`
between — the Catacomb 3-D ROM's SRAM printed `FF 43 FF 33 FF 44 ...`
("C3D..."). So a marker `4F 4B 01` appears as `FF 4F FF 4B FF 01`. Match the
plain and both interleaved forms.

## Wiring it up

`assets/tests/run_all.sh` runs Tier 1 then Tier 2 and exits non-zero on any
failure. In CI: cache `$GDK`/toolchain and the built libretro `.so` on stable
keys; the emulator core build and the SGDK toolchain are the slow parts. Fail
the job if `out/rom.bin` is missing, the boot is black/frozen, a host assertion
fails, or the self-test marker is absent.

## When a test fails → triage

Map the symptom back to SKILL.md's "Black-screen triage": all-black →
palette/`SYS_doVBlankProcess`; frozen after title → blocking wait or logic-tick
stall; garbage tiles → VRAM overflow / stale rescomp output; black-and-cyan
register dump → exception (read `Offset:`, look it up in `symbol.txt`, check
odd-address loads/stores and ROM-array bounds); **black before `main()`** →
boot-heap exhaustion (a `ram@` dump with a magic value written first thing in
`main()` tells you); **renders but sprites frozen / RAM writes dead** →
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
