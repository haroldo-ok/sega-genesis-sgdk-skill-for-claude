# Resources & the `rescomp` pipeline

SGDK compiles assets through **`rescomp`** (Java). You describe assets in a
`.res` text file under `res/`; `makefile.gen` runs rescomp on every `.res`,
producing a `.o` (the data) and a same-named `.h` (C symbols) you `#include`.
The full spec is `bin/rescomp.txt` in SGDK — this is a working summary.

```
res/resources.res      ->  #include "resources.h"     (gives you e.g. `hero_sprite`)
```

Each declaration is one line: `TYPE name file [options...]`. `name` becomes a
global `const` symbol of the matching SGDK struct type.

## Resource types

| TYPE     | C type              | use                                                        |
|----------|---------------------|------------------------------------------------------------|
| `PALETTE`| `Palette`           | 16 colours from a `.pal` or the palette rows of an image   |
| `TILESET`| `TileSet`           | tile pixel data (shared by TILEMAP/MAP)                    |
| `TILEMAP`| `TileMap`           | raw tilemap (small backgrounds/images)                    |
| `MAP`    | `MapDefinition`     | large scrolling level (stream with `MAP_*`)               |
| `IMAGE`  | `Image`             | palette+tileset+tilemap bundle (`VDP_drawImage`)          |
| `SPRITE` | `SpriteDefinition`  | animated hardware sprite (`SPR_addSprite`)                |
| `OBJECTS`| array               | custom Tiled objects attached to a MAP                    |
| `XGM`/`XGM2` | music track     | `.vgm`/`.xgm` music for the XGM/XGM2 driver               |
| `WAV`    | PCM sample          | sound effect / streamed sample                            |
| `BIN`    | `u8[]`              | any raw binary blob, verbatim into ROM                    |

## Common declarations (copy/adapt)

```
# palette from a .pal or from an image's palette rows
PALETTE pal_level   "gfx/level.pal"

# tileset + a large streamable level map that share tiles
TILESET tiles_level "gfx/level.png" BEST ALL
MAP     map_level   "gfx/level.png" tiles_level BEST 0

# a whole small background as one bundle
IMAGE   title_img   "gfx/title.png" BEST

# an animated sprite sheet: <w> and <h> are frame size in TILES (8px units),
# <compression> then <time> = frames between animation steps
SPRITE  hero_sprite "sprite/hero.png" 4 4 FAST 5

# music (XGM2 is the current driver) and a PCM sound effect
XGM2    bgm_track   "music/level1.vgm"
WAV     sfx_jump    "sfx/jump.wav" XGM2           # out_rate: 13300 (default) or 6650 only
                                                  # — any other value builds but plays at
                                                  # the wrong pitch; see audio.md

# raw data: a converted level, table, or save blob straight into ROM
BIN     level1_data "data/level1.bin"
```

Compression values where accepted: `NONE`, `APLIB` (small, slow to unpack),
`FAST`/`LZ4W` (bigger, fast — decompress at load) or `BEST`/`AUTO`. Unpack with
`unpackTileSet`/`unpackImage`/etc. at load time if you chose a compressed form.

`TILESET` optimisation flag: `ALL` (drop duplicate + flipped tiles, default),
`DUPLICATE` (drop exact duplicates only), `NONE` (keep every tile — use for
*font-like* tilesets where order matters and duplicates are legitimate).

File-level directives (their own lines): `ALIGN <n>`, `UNGROUP`, `NEAR` (force a
resource before the FAR/bankswitched region — needed for data you access without
the bankswitch trampoline on >4 MB ROMs).

## Image & palette format rules (get these right or colours break)

- **Indexed images** (PNG/BMP, 1/2/4/8 bpp): rescomp uses the image's own
  palette directly. Colour index **0 is transparent** for sprites/images. Author
  the palette exactly as you want it on hardware (Aseprite lets you order it).
  With 8bpp you can encode tile *priority* using indices ≥ 128.
- **RGB (truecolour) images**: rescomp has no palette, so you must embed one in
  the **top-left** of the image — each colour an 8×8 tile, each 16-colour
  sub-palette on its own row, **all four rows present** even if unused. rescomp
  strips that region before processing tiles. See `sample/game/sonic/res/gfx`.
- Genesis colour is **9-bit BGR** (3 bits/channel). Requantize source art down
  to that; don't hand-scale channel ranges. Classic port bug: treating VGA DAC
  6-bit palette values (0–63) as 8-bit and multiplying by 4 blows colours out.
  Let rescomp/your converter own the mapping and verify against a reference.
- Sprite frame `w h` are in **tiles** (×8 px). A 32×32 sprite is `4 4`. Hardware
  sprites max at 32×32; larger art becomes a multi-hardware-sprite metasprite,
  which the engine handles but which costs sprite slots. Past a certain size a
  character is better drawn as **streamed background tiles** on a plane — see
  `advanced-rendering.md` §4 (Punch-Out's boxers).
- **Writing your own converter?** Pillow renumbers P-mode palettes on save and
  drops unused entries, and `quantize()` may order padding colours arbitrarily —
  assign source colours to fixed indexes yourself when the index layout matters.
  Compress mostly-flat UI art (`BEST` rather than `NONE`) to let rescomp's tile
  dedupe work: Tuxemon's HP plates went from 80 and 95 cells to 13 and 27
  distinct tiles. More converter lessons (Tiled maps, data tables taken from
  the shipped binary) in `porting-workflow.md` → "Lessons from the data-driven
  ports".

## Using resources in C

After `#include "resources.h"`:

```c
PAL_setPalette(PAL0, pal_level.data, DMA);
VDP_drawImage(BG_A, &title_img, 0, 0);
Map* m = MAP_create(&map_level, BG_A, TILE_ATTR_FULL(PAL0,0,0,0,0));
Sprite* h = SPR_addSprite(&hero_sprite, 100, 100, TILE_ATTR(PAL1,0,0,0));
XGM2_play(bgm_track);                                      // music resource is a u8[]
XGM2_playPCM(sfx_jump, sizeof(sfx_jump), SOUND_PCM_CH2);   // WAV resource is a u8[]
                                     // (CH1 is usually the music's own PCM channel)
const u8* raw = level1_data;                               // BIN → plain array
```

## Asset-conversion pipeline for ports (the common pattern)

Original games rarely ship PNGs. Every serious `haroldo-ok` port has a
`tools/*.py` (or `convert_assets.py`) step that:

1. Reads the **original binary asset format** (Mode X planar VGA, sprite-sheet
   blobs, tile packs, `.DAT` archives) — documented per game.
2. Decodes/deinterleaves into linear pixels, byte-swapping if the source is
   little-endian.
3. Quantizes to the chosen 4×16 palette layout.
4. Emits either **PNGs consumed by `.res`** *or* generated **`.inc`/`.c` arrays**
   (`const u8 level0_data[] = {...}`) referenced as `BIN`/extern data in ROM.

Keep this step scripted and re-runnable (`python3 tools/convert_assets.py`), run
it before `make`, and check the *generated* files into the repo only if the raw
source assets can't be redistributed. Add a host test that validates the
event→animation→sprite chain against synthetic files in the original format so
the converter itself is covered (see `references/testing.md`).
