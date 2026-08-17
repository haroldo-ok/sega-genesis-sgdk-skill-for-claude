# SGDK runtime API & engine

Function names here are verified against SGDK 2.12 headers. The online doxygen
(`http://stephane-d.github.io/SGDK/`) is authoritative for full signatures;
`#include <genesis.h>` pulls in everything.

## Program skeleton

```c
#include <genesis.h>

int main(bool hardReset)
{
    JOY_init();
    SPR_init();                       // only if you use the sprite engine
    PAL_setPalette(PAL0, my_palette.data, DMA);
    VDP_drawText("HELLO", 10, 12);

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        if (joy & BUTTON_START) { /* ... */ }

        // update game logic / sprites here
        SPR_update();                 // if using sprites

        SYS_doVBlankProcess();        // MUST be the last thing each frame
    }
    return 0;
}
```

`main(bool hardReset)` — `hardReset` is TRUE on power-on, FALSE on soft reset.
`SYS_doVBlankProcess()` flushes the DMA queue, advances the sprite/scroll
engines, and waits for vblank. Forgetting it is a top black-screen cause.

Per-frame work that must happen *inside* vblank (palette swaps, scroll updates
for raster effects) goes in a callback: `SYS_setVIntCallback(myVBlank)`.

## VDP: planes, tiles, text

Two 64-cell-wide scroll planes (`BG_A`, `BG_B`), a `WINDOW` plane (fixed
overlay, good for HUDs), 64 KB VRAM. Resolution `VDP_setScreenWidth320()` (H40)
or `VDP_setScreenWidth256()` (H32); NTSC is 224 lines.

- `VDP_drawText(const char* str, x, y)` / `VDP_clearText(x, y, len)` /
  `VDP_clearPlane(plane, wait)` — the built-in 8×8 font uses colour index 15 of
  the current text palette; `VDP_setTextPalette(pal)` picks which of PAL0–3.
- `VDP_setTileMapXY(plane, tile, x, y)` — set one cell (a *tile attribute*:
  index + palette + priority + flip via `TILE_ATTR_FULL(...)`). Don't loop this
  per-cell for big updates — build a row/region and DMA it (see DMA below) or
  use `MAP_*`.
- `VDP_drawImage(plane, const Image*, x, y)` / `VDP_drawImageEx(...)` — blit an
  `IMAGE` resource (loads its tileset + tilemap + optionally palette).
- `VDP_loadTileSet(const TileSet*, index, DMA)` — upload tiles to VRAM.
- `VDP_setHorizontalScroll(plane, offset)` / `VDP_setVerticalScroll(...)` — plane
  scroll; per-line/per-tile scroll modes enable parallax and raster tricks.

## Palettes (colour)

4 palettes (`PAL0`–`PAL3`) × 16 colours, 9-bit BGR. Index 0 of a sprite/image
palette is transparent. Build colours with `RGB24_TO_VDPCOLOR(0xRRGGBB)`.

- `PAL_setPalette(u16 numPal, const u16* data, TransferMethod tm)` — load 16
  colours; `tm` is usually `DMA` (or `CPU`).
- `PAL_setColors(index, data, count, tm)` / `PAL_setColor(index, color)`.
- `PAL_fadeIn/PAL_fadeOut/PAL_fadeTo(...)` — hardware-friendly fades over frames.

Assign palette *lines* deliberately: e.g. PAL0 background, PAL1 player+FX, PAL2
enemies, PAL3 NPCs/HUD — a common port strategy when squeezing a 256-colour
source into 4×16 (frequency-weighted quantization, enforce min perceptual
distance to avoid duplicate slots).

## Sprite engine (`SPR_*`)

Hardware gives 80 sprite entries (H40), 20/line. The SGDK engine manages VRAM
tile allocation, the sprite attribute table, animation, and priority so you work
in game terms:

```c
Sprite* s = SPR_addSprite(&hero_sprite, x, y,
                          TILE_ATTR(PAL1, TRUE, FALSE, FALSE)); // pal, prio, vf, hf
SPR_setAnim(s, ANIM_WALK);
SPR_setPosition(s, x, y);
SPR_setHFlip(s, facingLeft);
SPR_setFrame(s, frame);           // or SPR_setAnimAndFrame(s, anim, frame)
SPR_setVisibility(s, VISIBLE);    // blink invulnerability, culling
SPR_setDepth(s, z);               // draw order
SPR_releaseSprite(s);             // free when the actor dies
SPR_update();                     // ONCE per frame after all sprite changes
```

`SPR_addSprite` needs a `SpriteDefinition` from a `SPRITE` resource
(`references/resources.md`). Use `SPR_addSpriteSafe` when you might exceed VRAM
and want graceful failure. Pool sprites for many actors; release off-screen ones.

## Input (`JOY_*`)

```c
JOY_init();
u16 j = JOY_readJoypad(JOY_1);            // JOY_1, JOY_2, or JOY_ALL
if (j & BUTTON_RIGHT) ...                 // BUTTON_UP/DOWN/LEFT/RIGHT
if (j & BUTTON_A) ...                     // BUTTON_A/B/C  (+ X/Y/Z/MODE on 6-button)
if (j & BUTTON_START) ...
```

Edge detection: keep last frame's mask and compare (`pressed = j & ~prev`).
`JOY_setEventHandler(cb)` gives change callbacks; `JOY_waitPress(joy, mask)`
blocks (menus only — never in the game loop). Controller type is auto-detected;
mask to A/B/C/Start if you don't handle 6-button.

When porting keyboard input, map named actions to buttons and centralize it:
`key_fire → (JOY_readJoypad(JOY_1) & BUTTON_A)`, etc.

## DMA (`DMA_*`)

VRAM/CRAM/VSRAM bulk transfers happen via DMA, queued and flushed in vblank.
Prefer DMA for anything bigger than a few cells: build data in a RAM buffer,
`DMA_queue*(...)` it, and `SYS_doVBlankProcess()` flushes. A frame's DMA budget
is limited (~7.6 KB during active display, more in vblank); overflowing the
queue or exceeding per-frame bandwidth causes tearing or a stalled display.
Split large streams (tilemap swaps, the affine-road technique) across frames.

## Large maps (`MAP_*`)

For scrolling levels bigger than a plane, use a `MAP` resource + the streaming
API instead of a raw `TILEMAP`:

```c
Map* m = MAP_create(&level_map, BG_A, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, 0));
MAP_scrollTo(m, camX, camY);      // decodes only the newly-revealed edge each call
```

This decodes just the incoming rows/columns per frame — the right tool for
platformers/top-down levels. Feed it camera coordinates every frame.

## System / timing (`SYS_*`)

- `SYS_doVBlankProcess()` — end-of-frame; see above.
- `SYS_setVIntCallback(cb)` — run `cb` during vertical interrupt.
- `SYS_disableInts()` / `SYS_enableInts()` — bracket critical sections touching
  VDP state the vint also touches.
- `SYS_isNTSC()` / `SYS_isPAL()` — 60 vs 50 Hz; scale timing if you support both.
- `SYS_getFPS()` — cheap profiler for a debug HUD.
- `SYS_reset()`, `SYS_die(msg)` — soft reset / fatal with on-screen message.

## Memory model

- **68 KB work RAM**, no `far` heap. SGDK provides `MEM_alloc/MEM_free`, but
  prefer **static allocation** with known bounds; heap fragmentation on 68 KB
  bites fast.
- **Keep big immutable data in ROM.** Levels, tables, dialogue, and converted
  assets belong in ROM as `BIN` resources or generated `const` arrays/`.inc`
  files — reference them in place, don't copy into RAM. A struct that is
  binary-compatible with the source format can be `memcpy`'d or even read
  directly from the ROM pointer.
- **Watch `sizeof` differences** host vs target: 68000 `int` is 16-bit under
  SGDK's flags in many idioms — prefer explicit `s8/u8/s16/u16/s32/u32` and
  `F16/F32` fixed-point everywhere for portable, predictable layout.

## SRAM saves (`SRAM_*`)

Enable SRAM in `rom_header.c` (the `"RA"`, RAM start/end fields — see the
template), then:

```c
SRAM_enable();
SRAM_writeByte(addr, v); u8 v = SRAM_readByte(addr);   // + word/long variants
SRAM_disable();
```

Genesis SRAM lives on **odd bytes** of the save address space — SGDK's API hides
this, but raw emulator dumps interleave it with `0xFF` (relevant when a headless
test reads a self-test marker; see testing.md).

## Fixed-point maths (`F16` / `F32`)

Use SGDK fixed-point in physics/geometry: `F16` is 10.6, `F32` is 16.16 (see
`maths.h`). `FIX16(1.5)`, `fix16Add/Mul/Div`, `fix16ToInt`, `sinFix16`, plus
`F16_computePosition(...)` helpers. Never use `float` in hot paths — there's no
FPU, and softfloat is huge and slow.

> ⚠️ **Odd-address word-load trap with *custom* fixed-point.** If you roll your
> own 8.8/16.16 in a 32-bit field and extract the integer part as
> `(s16)(v >> 8)` where `v` is a **memory operand**, modern GCC (14+) can compile
> that into a word load from an *odd* address — legal on 68020+, but an **address
> error (crash)** on the base 68000. It runs fine in some emulators and traps on
> accurate ones / real hardware, so it's easy to miss. Fix: force the shifted
> value through a data register before the narrowing cast — e.g. a tiny
> `static inline s16 fromfp(s32 v){ return (s16)(v >> 8); }` used everywhere
> instead of the inline expression. (Verified from a real port that crashed until
> it did this.) SGDK's own `F16/F32` helpers avoid the pattern; the trap is
> specific to hand-rolled fixed-point on raw struct fields.

## Performance playbook

- Decouple logic from render: accumulate frames, tick logic at the source
  game's rate (often 18–20 Hz → every 3rd NTSC frame), render at 60.
- Fixed-point + shifts/masks; precompute sin/cos and reciprocal tables.
- Batch VRAM writes via DMA; never per-cell loops for big regions.
- Bound per-frame work: pool + cull entities, scan one map column/row per frame,
  cap active sprites. Keep the worst-case frame under budget, not the average.
- Cache HUD/text: redraw only on change, not every frame.
- Build `release` (`-O3 -flto`); mark genuinely hot inner loops and verify with
  `SYS_getFPS()`. Move sustained heavy audio work appropriately (the Z80 drives
  sound, but the 68000 feeds it — see audio.md).
