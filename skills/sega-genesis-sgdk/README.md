# sega-genesis-sgdk

A Claude **Skill** for creating, debugging, optimizing, and porting **Sega
Genesis / Mega Drive** games in **C with [SGDK](https://github.com/Stephane-D/SGDK)**
(Stéphane Dallongeville's Sega Genesis Development Kit).

It turns a game — a new idea or an existing port target — into a *playable,
verified* `out/rom.bin`. It knows how to stand up a toolchain even in a locked-
down environment, how to get past the number-one SGDK failure (a ROM that
compiles cleanly but **boots to a black screen**), and how to prove the result
actually runs on emulated hardware instead of stopping at "it compiled."

Written against **SGDK 2.x** (2.12 / GCC 13.2 at time of writing) targeting the
Motorola 68000, the VDP, and the Z80 + YM2612 + PSG sound path.

> **Proven end-to-end.** Every capability below — the toolchain setup, the build
> pipeline, the headless-emulator harness, and the debugging playbook — was used
> to take an original maze-chase game from an empty folder to a **booting,
> playable ROM verified in Genesis Plus GX** (muncher moving under joypad
> control, score rising as dots are eaten, ghost AI roaming). The specifics that
> made that work, including the failure modes that cost hours, are baked into the
> references.

---

## What it does

- **Make a game from scratch** — project layout, entry point, the frame loop
  (`SYS_doVBlankProcess()`), sprites (`SPR_*`), tilemaps and scrolling, input,
  palettes, and audio.
- **Port an existing game** (from DOS, C, Java/Pascal, a 6502/Palm codebase, an
  emulator core…) using a repeatable 4-phase method: carve a *platform-clean
  core*, build it on the host as an oracle, map each original subsystem to its
  Genesis equivalent, then bring it up on hardware incrementally.
- **Stand up a toolchain anywhere** — Docker, MarsDev, or (for restricted
  sandboxes with no Docker and no time to compile GCC) Ubuntu's
  `gcc-m68k-linux-gnu` plus a cloned SGDK, including building the library and Z80
  tools from source and a full **manual build pipeline** for when you can't drive
  `makefile.gen` directly.
- **Debug a black-screen / crashing / *frozen* ROM** with a ten-entry triage —
  including the subtle "renders but sprites/logic frozen, RAM writes dead"
  failure (a library/compiler **LTO-version mismatch**), the black screen
  *before* `main()` from SGDK's unchecked boot-time heap allocations, and GCC
  code-gen traps that turn byte accesses into odd-address word accesses.
- **Push the hardware** — the ~7 KB-per-VBlank DMA budget, tear-free double
  buffering, software 3D (raycasters drawn into RAM in VDP tile format with
  compiled scalers), big characters streamed onto a plane, and delta-encoded
  FMV, drawn from complete ports of Catacomb Abyss, Catacomb 3-D and
  Wolfenstein 3D.
- **Debug on target without a debugger** — visual-probe techniques (state-to-tile
  markers, a RAM-persistence probe, the font-palette-index-15 invisibility trap)
  and an "official-sample control test" that instantly separates *your bug* from
  *your build environment's bug*.
- **Convert assets** (tiles, sprite sheets, VGM/XGM music, S3M/MOD trackers, WAV
  samples) through `rescomp` and the `.res` pipeline.
- **Set up sound** — XGM2 / XGM drivers, PCM SFX, and the tracker→VGM→XGM route.
- **Test a ROM** with a two-tier harness: a host-compiled logic *oracle* plus a
  **headless emulator** (Genesis Plus GX via libretro) that boots the real ROM,
  feeds scripted input on one or two pads (holds, button mashing), and asserts
  rendered frames are lit, colourful, changing and not SGDK's crash screen;
  measures audio energy to prove sound plays; dumps work RAM for post-mortems
  (`ramsym.py` reads variables and the call stack back out); and checks the
  cartridge header (`rom_sanity.py`). The templates pass both tiers unmodified.

## Definition of done

The skill treats a task as complete only when the ROM **compiles**, **fits** RAM,
is **not a black screen** (proven by a headless emulator), is **playable** under
scripted input, and is **copied to a reachable output path** — not left in a
scratch build dir.

---

## Layout

```
sega-genesis-sgdk/
├── SKILL.md                  # entry point: workflow selector, build→verify loop,
│                             #   black-screen/frozen triage, constraints cheat-sheet
├── references/               # load on demand
│   ├── toolchain-and-build.md   # install routes (docker/marsdev/apt), GDK env,
│   │                            #   makefile.gen targets, from-source libmd.a,
│   │                            #   the LTO trap, the manual build pipeline, CI
│   ├── api-and-engine.md        # program skeleton, VDP/planes, SPR_*, JOY_*, DMA_*,
│   │                            #   MAP_*, SYS_*, memory model, SRAM, F16/F32
│   ├── advanced-rendering.md    # VBlank DMA budget, double buffering, raycasters,
│   │                            #   big characters on planes, FMV, profiling
│   ├── resources.md             # rescomp .res format + asset-conversion pipeline
│   ├── audio.md                 # XGM2 vs XGM, VGM, WAV PCM (the measured rate
│   │                            #   rule), S3M/MOD→VGM→XGM, four more port routes
│   ├── porting-workflow.md      # 4-phase method, system-mapping table, lessons
│   │                            #   from the data-driven ports (RAM diet, tables
│   │                            #   from the shipped binary, compiled scripts)
│   ├── testing.md               # host oracle + headless GPGX harness and DSL,
│   │                            #   crash/audio/header checks, RAM post-mortems,
│   │                            #   visual probes, the official-sample control test
│   └── examples.md              # ~55 real SGDK projects indexed by technique,
│                                #   plus a 1.x→2.x API-rename table
└── assets/                   # ready-to-adapt files
    ├── Makefile                 # makefile.gen + -fno-store-merging + 68000 libgcc
    ├── rom_header.c             # ROM header (incl. SRAM fields, ROM-end note)
    ├── main.c                   # entry template (fixed-step loop, edge input,
    │                            #   a boot screen that passes the Tier-2 checks)
    ├── setup.sh                 # idempotent toolchain installer (docker/marsdev/apt)
    └── tests/
        ├── test_core_example.c        # host-oracle template (CHECK macro + shims)
        ├── run_host_tests.sh          # Tier-1 runner (compile + run core on host)
        ├── libretro_harness.c         # headless emulator harness (dlopen GPGX core):
        │                              #   holds, mash@, pad 2, shot@, ram@, audiostat@,
        │                              #   HARNESS_WAV
        ├── run_headless_emulator_test.sh  # Tier-2 runner (build harness, run ROM)
        ├── check_frames.py            # lit / colourful / changed / not a crash screen
        ├── rom_sanity.py              # header: ROM end vs size, checksums, SRAM
        ├── ramsym.py                  # read symbols and the stack from a RAM dump
        └── run_all.sh                 # both tiers
```

## How Claude uses it

1. Read `SKILL.md` and pick a workflow (new game / port / debug).
2. **Step 0**: ensure the toolchain exists (`assets/setup.sh`, or an existing
   `$GDK`), then scaffold from `assets/`.
3. Build with the template Makefile (`make` → `out/rom.bin`), or the manual
   pipeline when `makefile.gen` can't be driven directly.
4. **Verify** with the test harness — never stop at "it compiled".
5. Copy the ROM to an output path and hand it back.

Reference files are pulled in only when the task needs them, to keep context lean.

## Example

> **You:** *"Make a Genesis game where a sprite bounces around the screen and
> speeds up when I hold A. I want a real ROM I can run in an emulator."*

With the skill active, Claude will:

1. **Set up the toolchain** — confirm `$GDK`, a 68000 GCC, and Java exist, or run
   `assets/setup.sh` (docker, marsdev, or apt route) to set up SGDK.
2. **Scaffold the project** from `assets/` — the template `Makefile`, `rom_header.c`,
   and a `main.c` with the `int main(bool hardReset)` entry and a fixed-step loop
   ending in `SYS_doVBlankProcess()`. Bounce logic goes in `src/core/` (portable,
   host-testable); the `SPR_*` and `JOY_*` glue goes in `src/platform/`.
3. **Build** — `make` → `out/rom.bin`, with `rescomp` compiling any `.res` assets.
4. **Verify, not just compile** — run the host oracle on the bounce math, then
   boot the real ROM in the headless Genesis Plus GX harness and assert the frames
   are lit, colourful, and changing (i.e. *not a black screen*), with scripted A
   presses to confirm the speed-up path.
5. **Hand back a reachable ROM** — copy `out/rom.bin` to an output path and
   present it, ready to drop into an emulator or flash cart.

Other prompts it handles the same way: *"port this text-adventure C file to the
Mega Drive"*, *"convert these PNG tiles and this .s3m into a scrolling
background with music"*, or *"my SGDK build compiles but boots to a black screen
(or the sprites are frozen) — fix it"* (which jumps straight to the triage).

## Installing the skill

- **From the `.skill` bundle**: use the *Save skill* action on the packaged
  `sega-genesis-sgdk.skill` file.
- **From the folder**: place this directory where your Claude environment loads
  skills (alongside other skill folders).

## Toolchain the skill sets up

The build environment (installed by `assets/setup.sh`) needs **SGDK 2.x**, a
**Java** runtime (for `rescomp`), and a 68000 GCC. Supported install routes:

- **Docker** — `gitlab.com/doragasu/docker-sgdk` (fastest native path on Linux/CI).
- **MarsDev** — `github.com/andwn/marsdev` (native toolchain build).
- **apt** — Ubuntu/Debian `gcc-m68k-linux-gnu` + a cloned SGDK tree, for
  restricted environments with no Docker and no time to build GCC. Proven to
  produce a booting, playable ROM. Rebuild `libmd.a` from source with this
  compiler to avoid the **LTO trap** (a version-mismatched prebuilt library links
  but leaves sprites/logic frozen — the toolchain doc explains and fixes it).

Testing (Tier 2) additionally builds **Genesis Plus GX**
(`github.com/ekeeke/Genesis-Plus-GX`) as a libretro core for the headless harness.

---

## Provenance — verified vs adapted

Facts in the references were checked against **cloned source** and **a real
build**, not recalled from memory:

- **Verified against SGDK 2.12 + the `haroldo-ok` ports**: API names,
  `SYS_doVBlankProcess`, the `rescomp` `.res` format, `makefile.gen` targets, the
  project layout, and the `XGM2_play` signature.
- **Port and community index** (`references/examples.md`): ~55 repos
  categorized by technique, built from their live GitHub metadata/READMEs. The
  1.x→2.x rename table is distilled from the migration notes those repos ship.
- **Battle-tested end-to-end**: the apt toolchain route, the from-source
  `libmd.a` rebuild, the manual build pipeline, the Genesis Plus GX harness, and
  the on-target visual-probe / LTO-trap debugging techniques were all used to
  take an original maze-chase game from scratch to a **booting, playable
  `out/rom.bin`** verified in Genesis Plus GX. Those hard-won specifics live in
  `references/toolchain-and-build.md` and `references/testing.md`.
- **Measured in the October 2026 revision** (SGDK 2.12, Ubuntu GCC 13.3, Genesis
  Plus GX, each with a purpose-built test ROM or the ports' own ROMs):
  - GCC **store merging** turns two adjacent `u8` writes into an odd-address
    `move.w` (fixed by `-fno-store-merging`), and `(s16)(mem32 >> 8)` becomes an
    odd-address `move.w x+1` at `-O1`…`-O3`; a plain inline helper does **not**
    fix the latter, an `asm("" : "+d"(v))` barrier does;
  - SGDK's **boot-time DMA allocations** fail silently with a big `.bss` — the
    46/50/52/56 KB table in `toolchain-and-build.md`, and the fix verified after a
    *clean* library rebuild (an incremental one silently kept the old `dma.o`);
  - the **XGM2 PCM rate rule** (13300, or 6650 with `halfRate`; anything else
    builds but plays at the wrong pitch) from FFT-measured captures;
  - SGDK 2.x's **crash-screen** signature, the faulting-PC → function lookup,
    Genesis Plus GX's **byte-swapped RAM** exposure, `sizebnd`'s **XOR checksum**
    format, and the ROM-end header field across seven real ROMs;
  - the `makefile.gen` variable semantics behind the template `Makefile`;
  - `assets/setup.sh` (apt route) run end to end, and the template project
    passing `run_all.sh` (both tiers) unmodified.
- **Adapted, and exercised**: `assets/tests/libretro_harness.c` compiles clean
  against the real `libretro.h` (`-Wall -Wextra`, zero warnings) and was run
  against the Catacomb 3-D ROM (into gameplay via `mash@`, RAM dumps decoded,
  WAV and audio energy checked) as well as the test ROMs above. Its new features
  re-implement ideas from the Wolf3D, Punch-Out and Tuxemon ports' harnesses;
  no code was copied from those GPL repositories.

## Credits

- **SGDK** — Stéphane Dallongeville (`Stephane-D/SGDK`).
- **Porting patterns & test-harness design** — the `haroldo-ok` SGDK ports. The
  games behind them belong to their owners: *Catacomb Abyss* / *Catacomb 3-D*
  (Softdisk / id Software; source © Flat Rock Software, GPL v2),
  *Wolfenstein 3D* (id Software), *Tuxemon* (the Tuxemon project, GPL-3.0),
  *Punch-Out!! Pizza Tower Edition* (ScratchTac's fan game), *HexGL* (BKcore).
- **Community examples** — the many third-party authors indexed in
  `references/examples.md` (radioation, makeclassicgames, bearmade, fabri1983,
  Anael Seghezzi, VagnoSilva, and others).
- **Headless testing** — Genesis Plus GX (`ekeeke/Genesis-Plus-GX`).

This skill packages knowledge *about* these projects and the SGDK toolchain; the
underlying engines, tools, and games remain the property of their respective
authors under their own licenses.

## License

The skill itself — this README, `SKILL.md`, the `references/`, and the original
`assets/` templates (`Makefile`, `main.c`, `rom_header.c`, `setup.sh`, the
host-test template, and the runner/assertion scripts) — is released under the
**MIT License** (see below). Use, copy, adapt, and ship it freely.

Two things this license does **not** cover:

- **`assets/tests/libretro_harness.c`** is adapted from the test harness in the
  `haroldo-ok/jazz-jackrabbit-for-sega-genesis` project — retain that project's
  license and attribution if you redistribute the harness.
- **Third-party tools and engines** the skill drives or references — SGDK,
  Genesis Plus GX, MarsDev, the doragasu Docker image, and every repo indexed in
  `references/examples.md` — are governed by **their own licenses**, not this one.
  Check each before reusing its code or assets.

```
MIT License

Copyright (c) 2026 the sega-genesis-sgdk skill authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
