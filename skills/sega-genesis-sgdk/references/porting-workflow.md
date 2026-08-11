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
| `far` heap (`farmalloc`)                  | 68 KB flat work RAM, static allocation             |
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
  pass; the 68000/SGDK idiom treats plain `int` as 16-bit in places, so make
  widths explicit to avoid silent overflow and struct-size drift).
- **`src/platform/`** — the SGDK shell that draws the core's state, reads input,
  plays sound, and persists saves.

Trim the entity struct hard: remove embedded graphics buffers (the DOS
`MASK_IMAGE pic[4][4]` etc.) and replace them with a `Sprite*` handle. God of
Thunder cut its ACTOR struct from ~256 to ~100 bytes this way — on 68 KB RAM
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

## Definition of done

Return to the SKILL.md checklist: compiles, fits RAM/ROM, **not a black screen**
(headless boot asserts lit changing frames), playable (scripted input drives
real states), and `out/rom.bin` delivered to the user. A port isn't done because
it compiles — it's done when the emulator test proves it runs.
