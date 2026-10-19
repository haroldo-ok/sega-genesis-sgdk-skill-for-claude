# Porting an existing game to the Genesis with SGDK

This is the most common and highest-value task. The method below is distilled
from the `haroldo-ok` ports (God of Thunder from DOS, Text Elite and Space
Trader from C, Jazz Jackrabbit from the OpenJazz engine). It works for DOS, C,
Java, Pascal, Palm, 6502, or emulator-core sources.

The one idea behind everything: **separate a platform-clean *core* from a thin
*SGDK shell*, get the core running and asserting correctly on your host first,
then bring it up on hardware incrementally** — so at every step you're debugging
one new thing, not the whole game.

## Phase 1 — Read the source and write a porting guide

Before writing any Genesis code, read the *entire* original source tree and
produce a short guide (a markdown file in the repo) that answers:

- What is the main loop, and at what **rate** does game logic tick?
- How is the **world represented** (tilemap? bitmap? object list?) and how big?
- What are the **entity/actor structs**, and how large (bytes) are they?
- How does it do **graphics** (blitter? planar VGA/Mode X? sprites?), **input**,
  **sound**, **file I/O**, and **saves**?
- What's **endianness** and integer width of the source, and of any data files?

Then fill in a **system-mapping table** — the single most useful artifact of a
port. God of Thunder's, as a model:

| Original (DOS) system                     | Genesis / SGDK equivalent                          |
|-------------------------------------------|----------------------------------------------------|
| Mode X 320×200 planar VGA, page flip      | VDP tilemap planes + hardware sprites              |
| 256-colour 8-bit palette                  | 4 × 16-colour, 9-bit BGR palettes                  |
| Planar mask-blitter (`xfput`)             | `SPR_addSprite` / `VDP_setTileMapXY` / DMA         |
| Timer interrupt ~18 Hz                    | 60 fps VBlank, run logic every 3rd frame (≈20 Hz)  |
| `far` heap (`farmalloc`)                  | 64 KB flat work RAM, static allocation             |
| File I/O (`fopen`, `GAME.DAT`)            | ROM arrays / `BIN` resources                        |
| Sound Blaster / AdLib / PC speaker        | YM2612 FM (XGM2) + PCM SFX                          |
| Keyboard scan codes                       | 3-button joypad (`JOY_readJoypad`)                 |
| Save file on disk                         | SRAM (`SRAM_*`) via `rom_header.c`                 |

Fill one row per subsystem your game actually uses. This table becomes your
task list.

## Phase 2 — Carve out the platform-clean core

Split the source into:

- **`src/core/`** — pure logic: physics, collision, state machines, economy,
  AI, level/entity update. **No SGDK calls, no VDP/SPR/hardware.** Only
  `s8/u8/s16/u16/s32/u32` and `F16/F32` types (do a mechanical `int → s16`
  pass where the source relied on a 16-bit `int`: DOS compilers used 16-bit
  `int`, but m68k GCC's `int` is **32-bit** — SGDK doesn't pass `-mshort` — so
  wraparound, struct sizes and on-disk formats silently change unless widths
  are explicit; and 32-bit arithmetic is slower on the 68000).
- **`src/platform/`** — the SGDK shell that draws the core's state, reads input,
  plays sound, and persists saves.

Trim the entity struct hard: remove embedded graphics buffers (the DOS
`MASK_IMAGE pic[4][4]` etc.) and replace them with a `Sprite*` handle. God of
Thunder cut its ACTOR struct from ~256 to ~100 bytes this way — on 64 KB RAM
that matters.

Where the source has function-pointer dispatch tables (movement patterns, AI
states), keep them — they port cleanly. Watch for forward references the DOS
compiler allowed; add a forward-declaration block if a pattern calls a later one.

## Phase 3 — Host oracle first

Compile `src/core/` on your **development machine** with plain `cc` and a tiny
test harness (`tests/test_core_example.c`, `run_host_tests.sh`) that drives the
logic and asserts on positions/state/economy — *before* it ever runs on a
Genesis. This is the oracle: fast, debuggable with gdb/asan, and it proves the
port's logic independent of VDP quirks. Every `haroldo-ok` port that has tests
compiles the exact ROM core on the host this way. See `references/testing.md`.

Convert data files here too, and add a test that decodes a synthetic file in the
original format so your converter is covered.

## Phase 4 — Bring up the SGDK shell incrementally

Bring up one subsystem at a time, verifying each on the headless emulator before
adding the next. A reliable order:

1. **Boot + palette + text.** Get *something lit* on screen (`VDP_drawText`,
   a seeded palette). This alone clears the #1 black-screen trap.
2. **Static background.** Draw one screen/level via `IMAGE`/`MAP` from converted
   art. Confirm the palette conversion looks right against a reference frame —
   this is where the VGA-6-bit-vs-8-bit colour bug shows up.
3. **Input + player sprite.** Wire `JOY_readJoypad` to the core's input, add the
   player `Sprite`, sync `SPR_setPosition/SPR_setAnim` from core state each tick,
   `SPR_update()` each frame.
4. **Camera + scrolling** (`MAP_scrollTo` fed camera coords) for larger worlds.
5. **Entities**: spawn from the core's object/event tables; pool + cull.
6. **HUD** (WINDOW plane; cache, redraw on change).
7. **Sound**: music (XGM2) then SFX (PCM).
8. **Saves**: SRAM.

## Recurring patterns & gotchas

- **Timing.** Source ticks at ~18–20 Hz, NTSC is 60 fps: keep a frame
  accumulator and run logic every 3rd frame; render/animate every frame.
  PAL is 50 Hz — scale if you support both (`SYS_isPAL`).
- **Memory.** No `far`/heap sprawl. Static-allocate; keep levels/tables/text in
  ROM and read them in place. A struct that's binary-compatible with the source
  layout can be `memcpy`'d straight from a ROM pointer.
- **Palette.** 256→4×16 needs deliberate grouping (background / player+FX /
  enemies / NPCs+HUD) via frequency-weighted quantization with a minimum
  colour-distance so you don't waste slots on near-duplicates. Do **not**
  hand-scale channel ranges — requantize. VGA DAC values are 6-bit (0–63);
  treating them as 8-bit (×4) blows colours out.
- **Rendering granularity.** Source "blit a 16×16 image at (x,y)" becomes either
  a hardware sprite or, for background tiles, a 2×2 sub-tile decomposition into
  the tilemap. Big/full-screen redraws must go through DMA and often across
  multiple frames.
- **Input.** Centralize the keyboard→joypad mapping in one place; expose the
  core's named actions and bind them to `BUTTON_*`.
- **I/O & saves.** Replace `fopen`/FAT with ROM arrays (data) and SRAM (saves).
- **Text-heavy / menu games** (Text Elite, Space Trader, Elifoot): the port is
  mostly replacing a text parser / mouse UI with a **joypad-driven menu** in
  40-column VDP text mode — little to no graphics conversion, so it's the
  easiest genre to start with. Watch RAM for large data tables (put them in ROM).
  For 80-column DOS text screens, Catacomb Abyss draws a 4×8 code-page-437 font
  so 80 columns fill the 320-pixel screen.

## Lessons from the data-driven ports

From `catacomb-abbyss-md`, `catacomb-3d-md`, `wolf-3d-md`, `tuxemon-md` and
`punch-out-pizza-tower-edition-md` (see `examples.md`) — ports that convert the
**original game data** rather than redrawing it.

**Convert, don't hand-port, the data.** Each has a `tools/convert.py` (or
several converters) that reads the retail/shareware files and emits
`src/gen/*.c`, `.s` and `res/` inputs; `make data` regenerates them, and the
generated files are checked in so a plain `make` works without the originals.
Write the converter in Python with `-I` isolation, keep it re-runnable, and make
it fail loudly on anything unexpected (Tuxemon's sprite converter *skips with an
error* any sheet that isn't the expected 48×128 instead of silently mis-slicing).

**Take tables from the shipped binary, not the source release.** Catacomb 3-D's
Huffman dictionaries in the GPL source's `.OBJ` files came from a later (1993)
build and didn't match the v1.00 data. The converter instead LZEXE-decompresses
`CAT3D.EXE`, locates each table by content search, and **validates it by
decoding real chunks** before trusting it. Whenever a source release and the
data you're converting disagree, the binary that shipped with the data wins.

**Put the data on a RAM diet.** 64 KB holds the game state, not the game. The
Catacomb 3-D recipe, ending at 49 KB `.bss` with no runtime allocation after boot:
- keep only **mutable** state in RAM; read immutable map planes straight from ROM
  (an opened door is detected as "wall cell whose `actorat` entry is no longer
  solid" instead of keeping a second 4 KB `tilemap`);
- replace per-tile scratch arrays with computation (the 4 KB `spotvis`
  visibility map became a Bresenham line-of-sight test per actor);
- shrink hot structs (a 56-byte actor, hit-rects computed where used);
- intern strings to small integer ids at conversion time (Tuxemon's 324 story
  keys → indexes into a flat `u8 story_vars[324]`, a 355-byte save).
Re-check every index type when you shrink: Catacomb 3-D's `0x80 + index` actor
marker overflowed a byte at 128 actors.

**Removing a table can expose a hidden writer.** After `tilemap` was removed,
the renderer's old `spotvis` marking (a write 4,096 bytes past a pointer) landed
inside the object list and corrupted `objtype.state` — crashes on levels 4 and
6 only. When you delete or shrink a buffer, grep for code that wrote to it by
offset, not by name.

**Keep the original's clock.** Run the source's tick rate (70 Hz for id games)
from a VBlank accumulator and keep its adaptive-tics logic, so gameplay speed
matches the original on NTSC and PAL even when the frame rate doesn't
(`advanced-rendering.md` §3).

**Fix the original's real bugs deliberately, and say so.** Catacomb 3-D's
100,000-point goal overflowed the DOS game's 16-bit score and *subtracted*
points; the port uses a 32-bit counter. Document each such change under
"Differences from the original".

**Compile scripted behaviour to bytecode; fail safe.** Tuxemon's Tiled maps
carry event objects with ordered `cond*`/`act*` properties. Rather than
hand-porting 140 events, `tools/compile_events.py` emits condition/action
bytecode with every string pre-interned, and the ROM runs one interpreter
(condition evaluation lives in the host-testable core). The rules that made it
trustworthy:
- an unsupported **condition** compiles to "never true", so content the port
  can't evaluate stays locked instead of firing early; an unsupported
  **action** compiles to a no-op;
- an event with *no* conditions is not "always true" — check every gating
  namespace (Tuxemon's `behav*` properties hid real gates; the naive reading
  made one event shadow every later event on the map, and a later variant made
  an NPC greet on every step);
- the compiler **prints what it skipped**;
- actions that wait (dialogue, `wait`, scripted movement) need a resumable
  per-frame VM (`event_vm_pump()`) rather than running a whole action list in
  one call; reset the VM and any control lock on every map change and load.

**Porting from Scratch.** Punch-Out Pizza Tower Edition starts by decompiling
the project's `project.json` into readable pseudo-code
(`tools/decompile_scratch.py` → `docs/original_scripts/*.txt`) and transcribes
the opponent AI into C from that, keeping the original HP/damage tables; SVG
costumes are rasterized with `cairosvg` before quantizing.

**Porting from Tiled maps** (`tuxemon-md`'s `tools/convert_map.py`): decode the
base64+zlib layers, resolve GIDs through both external `.tsx` and embedded
tilesets, **alpha-composite all layers per cell** (taking the top layer left
trees floating on black), dedupe and quantize to 15 colours + transparent,
read collision rectangles from the object layer into a solid bitmap, and read
event objects for encounters, doors (`transition_teleport` → trigger rect +
destination) and spawn points. A 16×16 Tiled tile maps 1:1 to a 2×2 metatile.

**PIL palette pitfalls in converters.** Pillow renumbers a P-mode palette on save
and drops entries no pixel uses, and `quantize()` can return padding entries
in arbitrary order. When palette *indexes* matter (UI art sharing the text
palette, index 15 reserved for the font), map source colours to fixed slots
yourself and set run-time-only colours with `PAL_setColor()` instead of baking
them into the PNG.

## Definition of done

Return to the SKILL.md checklist: compiles, fits RAM/ROM, **not a black screen**
(headless boot asserts lit changing frames), playable (scripted input drives
real states), and `out/rom.bin` delivered to the user. A port isn't done because
it compiles — it's done when the emulator test proves it runs.
