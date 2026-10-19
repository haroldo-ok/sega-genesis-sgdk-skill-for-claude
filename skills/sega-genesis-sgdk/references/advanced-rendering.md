# Advanced rendering: DMA budgets, double buffering, software 3D, streaming

Read this when a game pushes more pixels than the tile/sprite model hands you
for free: a software-rendered 3D view (raycaster), full-screen or
large-character animation streamed from ROM, FMV, or anything that uploads
kilobytes of tile data every frame. The patterns here come from working ports
(`haroldo-ok/catacomb-abbyss-md`, `catacomb-3d-md`, `wolf-3d-md`,
`punch-out-pizza-tower-edition-md`, `hexgl-fmv-md` — see `examples.md`); the
constants are from SGDK 2.12's headers.

## 1. The VBlank DMA budget is the frame budget

The VDP accepts bulk transfers only fast enough during vertical blank. SGDK
2.12 encodes the per-VBlank capacity in `inc/dma.h`:

| Constant | Bytes per VBlank |
|---|---|
| `DMA_TRANSFER_CAPACITY_NTSC` | 7,200 |
| `DMA_TRANSFER_CAPACITY_PAL_LOW` | 8,000 |
| `DMA_TRANSFER_CAPACITY_PAL_MAX` | 15,000 |

Everything that changes on screen — tiles, tilemaps, the sprite attribute
table, scroll tables, CRAM — comes out of that ~7 KB on NTSC. Divide your
per-frame upload by it to get the *upload* frame-rate ceiling before you write
a line of renderer: a 320×120 4bpp view is 19,200 bytes ≈ 3 VBlanks ≈ 20 fps
max; Catacomb Abyss renders at 11–14 fps under that ceiling. Transfers that
overrun VBlank spill into active display (slower, and they can starve the Z80
sound driver — see `audio.md`).

Two proven ways to live inside it:

**SGDK-native: cap the queue, then spend what's left.** Punch-Out sets the cap
once and, after the sprite engine has queued its work, streams only as many
tiles as fit, always making some progress:

```c
DMA_setMaxTransferSize(IS_PAL_SYSTEM ? 8000 : 7000);   /* once, at init */
...
/* per frame, AFTER SPR_update() has queued sprite data */
s16 budget = (s16)(DMA_getMaxTransferSize() - DMA_getQueueTransferSize())
           - reserve_for_tilemap_rows;
if (budget < 32 * 24) budget = 32 * 24;                /* always make progress */
u16 tiles = budget / 32;                               /* 32 bytes per 4bpp tile */
VDP_loadTileData(src + done * 8, bankBase + done, tiles, DMA_QUEUE);
```

Related SGDK calls: `DMA_canQueue()`, `DMA_getQueueSize()`,
`DMA_setIgnoreOverCapacity()`.

**Custom VInt job ring (full control).** The Catacomb ports skip SGDK's queue
for the heavy traffic and run their own from a `SYS_setVIntCallback` handler:
a ring of `{src, dst, len, flags}` jobs (48 entries in Abyss), a fixed byte
budget per VBlank (7,000), and a loop that DMAs jobs in order, **splitting a
job that doesn't fit and resuming it next VBlank**. Two details matter:

- A **flip is itself a job** (`flags = JOB_FLIP`, no data). Because jobs run in
  order, the page flip happens only after every byte of the new frame has
  landed — tear-free by construction, no separate synchronisation.
- DMA runs inside `Z80_enableBusProtection()` / `Z80_disableBusProtection()` so
  the Z80 sound driver doesn't touch the bus mid-transfer; the handler also
  uploads its own sprite table (`DMA_doDma(DMA_VRAM, sat, SAT_ADDR, ...)`, length
  in **words**) and ticks input and sound.

If you replace SGDK's queue like this, the default 80-entry queue and 8 KB
staging buffer SGDK allocates at boot are wasted RAM — see "Boot-time heap
exhaustion" in `toolchain-and-build.md` for shrinking or overriding them
(Wolf3D cut the queue to 4 static entries).

## 2. Tear-free double buffering of tile data

You can't race the beam with 19 KB of tiles; draw into a hidden copy and swap.

- **Two tile banks, one tilemap switch.** Upload the next frame's tiles into
  bank B over as many VBlanks as it takes while bank A stays on screen, then
  rewrite (or re-point) the tilemap in one go. Punch-Out does this for its
  boxers; hexgl-fmv-md does it with two 640-tile banks.
- **Page flip by a single vertical-scroll write.** Abyss makes a 64×64-cell
  plane (`VDP_setPlaneSize(64, 64, FALSE)`) whose rows 0–31 show view buffer 0
  and rows 32–63 view buffer 1 (each with its status bar), so flipping is
  `VDP_setVerticalScroll(BG_A, front ? 256 : 0)` — one register, no tilemap
  traffic at all. It also points BG_A and BG_B at the same plane address and
  moves the sprite table and H-scroll table (`VDP_setSpriteListAddress`,
  `VDP_setHScrollTableAddress`) to make room for two 600-tile buffers: when the
  default VRAM layout doesn't fit, lay VRAM out yourself (and then don't use the
  SGDK subsystems whose VRAM you took, e.g. its font or sprite engine).

## 3. Software-rendered views on a tile machine (raycasters)

The Genesis has no framebuffer, so a 3D view is drawn in **work RAM in VDP
tile format** and DMAed up. The id-engine ports converge on the same design:

| Port | View | Columns | Speed (accurate emulation) |
|---|---|---|---|
| Catacomb Abyss | 320×120 | 160 × 2 px | 11–14 fps |
| Catacomb 3-D | 264×144 | 132 × 2 px | (same renderer lineage) |
| Wolfenstein 3D | 256×128 | 128 × 2 px | ~6 fps |

**Column-major tile layout.** Store the buffer so each 8-pixel-wide screen
column is one contiguous block: tile index increases *down* the column, so a
column of `rows` scanlines is `rows × 4` bytes. Consequences that make it fast:

- A **2-pixel-wide wall column is one byte per scanline at a constant stride of
  4 bytes** (two 4bpp pixels per byte, four bytes per tile row), straight down
  through tile boundaries — exactly what a generated scaler wants.
- **Dirty tracking per 8-px column** (a 40-bit mask) turns into **contiguous DMA
  runs**: adjacent dirty columns are adjacent in RAM *and* in VRAM.
- The tilemap is written once at init; afterwards only tile data moves.

**Compiled scalers.** Instead of a per-pixel loop, generate 68000 code for each
wall height (`src/gen/scalers.s`), unrolled `move.b` sequences with the
source-row offsets baked in, reproducing the original's `BuildCompScale()`
mapping; Abyss has byte (one column) and word (two columns) variants and pairs
close walls. This is the inner loop; everything else is setup.

**Ray casting.** A per-column DDA from the original camera point. Abyss casts
every 4th column and refines only where the wall face changes, interpolating
in between. Sprites reuse the original transform/scale maths with a **column
z-buffer**. Reproduce the original tables bit-exactly (focal length, projection
scale, a float32-built sine table) so maps and timing feel right. Leave
floor/ceiling as flat fills — the originals did.

**Budget RAM before you start.** Abyss's screen buffer alone is 32,000 bytes —
half of work RAM. Expect to cut the original's data structures (Catacomb 3-D
dropped two 4 KB per-tile arrays; see `porting-workflow.md`), and expect a large
`.bss` to collide with SGDK's boot-time allocations
(`toolchain-and-build.md`).

**Keep the original's game clock.** id games tick at 70 Hz; the ports run an
accumulator in the VBlank handler (`acc += pal ? 84 : 70; while (acc >= 60) {
acc -= 60; TimeCount++; }`) and keep the original's adaptive-tics model, so
game speed matches DOS even though the frame rate doesn't.

## 4. Large characters as background tiles, not sprites

The sprite hardware caps out at 80 sprites, 20 per scanline (320 px of sprite
per line). A Punch-Out boxer is wider and taller than that comfortably allows,
so Punch-Out draws the **opponent on plane A** as streamed tiles (Super
Punch-Out style): each animation frame's tiles go into the hidden one of two
VRAM banks over several VBlanks using the leftover DMA budget (§1), and the
tilemap is switched only when the whole frame is resident. Only the frame's
on-screen rows are written to the map, because the 64×32 plane is shorter than
the tallest frames. The player — smaller, overlapping, needing priority over
the opponent — stays a sprite (two stacked SGDK sprites).

Use this whenever one big thing animates and the background behind it is
simple: bosses, fighting-game characters, title-screen mascots.

## 5. Streaming video

`hexgl-fmv-md` (see `examples.md`) is the reference: **inter-frame delta
encoding** against the frame already sitting in the hidden bank (N−2 with two
banks), stored as runs of changed 8×8 tiles in independent sections, one
section DMAed straight from ROM per VBlank — no CPU decompression, no RAM
staging, 15 fps NTSC at ~79% of the raw size. Raw streaming is simpler but
bounded by the §1 budget and by ROM size.

## 6. Measuring

- `SYS_getFPS()` and `SYS_showFrameLoad(bool mean)` for a quick on-screen read.
- **Scanline timer** (Abyss `-DPROFILE`): time = VBlank count × lines per frame
  + `GET_VCOUNTER & 0xFF`, read with interrupts disabled, treating counter
  values ≥ `0xE0` as belonging to the previous frame. Bracket each renderer
  stage and print the deltas in a debug HUD; scanlines are a far more useful
  unit than fps when you're shaving an inner loop.
- An **emulator-side cycle profiler** (per-PC cycle histogram mapped to
  functions with `nm`/`addr2line`) answers "where did the frame go" without
  guessing — Wolf3D's tooling does this with a patched Genesis Plus GX; see
  `testing.md`.
