# Worked examples — real SGDK projects to mine

Two catalogues of real SGDK projects, grouped by the technique each one best
demonstrates: first the **`haroldo-ok` ports** (large, well-structured ports
with tooling and tests), then a broader **community index** covering techniques
the ports don't — raycasting/pseudo-3D, FMV, slope collision, variable-width
fonts, Godot/Aseprite pipelines, and more.

When starting a task, clone the closest match and read its `README.md`,
`Makefile`, `res/*.res`, `tools/`, and `tests/` before writing code — these
encode hard-won, hardware-specific decisions. **Copy the pattern, not the code
verbatim**: many community repos target older SGDK (1.41–2.11) and use renamed
APIs — see "Version drift" at the end before lifting code from any of them.

---

# Part 1 — the `haroldo-ok` ports

Large, complete ports with asset pipelines and (for Jazz) a full test harness.
All at `https://github.com/haroldo-ok/<name>`.

## By genre / difficulty (where to start)

- **Text / menu-driven (easiest — no graphics conversion):**
  `text-elite-genesis`, `elifoot2-md`, `spacetrader-genesis-`. The port is
  mostly a pure-C engine plus a joypad-driven menu UI in 40-column VDP text
  mode. Best first port; the risk is RAM (keep big tables in ROM).
- **Arcade / action (medium):** `gemrush-genesis`, `xquest2-md`. Real-time
  input, sprites, collision, score — the full SGDK loop without a huge asset
  pipeline.
- **Platformer / action-adventure (hard — full pipeline):**
  `jazz-jackrabbit-for-sega-genesis`, `god-of-thunder-genesis`. Streaming maps,
  entity pools, converted original assets, host + emulator tests.
- **Pseudo-3D / effects (hard — performance-critical):** `fzero-clone-md`.
- **Streaming FMV (hard — DMA-budget-critical):** `hexgl-fmv-md` — video-as-
  gameplay with a headless capture pipeline.
- **Audio tooling:** `convert-s3m-vgm-jazzjackrabbit-md`.

## By technique

### `god-of-thunder-genesis` — the reference DOS port
The most complete worked port; treat its README as a porting textbook. Shows:
the full **system-mapping table** (Mode X→VDP, 256-colour→4×16, blitter→`SPR_*`,
18 Hz timer→60 fps every-3rd-frame, `farmalloc`→static, file I/O→ROM arrays,
keyboard→`JOY_*`, save→SRAM); trimming the ACTOR struct 256→100 bytes with a
`Sprite*` handle; decoding two different original binary asset formats (linear
8bpp actors + Mode X planar tiles) in `tools/pack_assets.py`; a WINDOW-plane HUD
that only redraws on change; 2×2 sub-tile decomposition for background tiles;
palette grouping into 4 lines; and the concrete **palette bug** (source stores
8-bit RGB, not VGA 6-bit DAC — an early `×4` scale blew every colour out).
Movement-pattern port tricks: mechanical `int`→`s16`, stripping DOS inline asm,
reconstructing function-pointer tables, adding forward declarations.

### `text-elite-genesis` — minimal pure-C engine port
The whole game is `src/elite.c` (the original Elite universe engine, unchanged
logic) + `src/main.c` (SGDK entry, palettes, loop). No `res/` art at all. Shows
the simplest possible Makefile (`GDK ?= /opt/sgdk; include $(GDK)/makefile.gen`),
using the system font with per-palette text colours (PAL0 green body, PAL1 gold
titles, PAL2 cyan data) via `RGB24_TO_VDPCOLOR`, and replacing a text parser
with a joypad menu. Ideal template for any text/data-driven port.

### `spacetrader-genesis-` — data-heavy trading sim port
A port of the Space Trader game. Same family as Text Elite: a large existing C
codebase with heavy data tables and a menu UI. Study for organising lots of
static game data into ROM and building a multi-screen menu system on the VDP
text plane within the RAM budget.

### `elifoot2-md` — management/simulation UI
A football-management port — dense menus, tables, and text with essentially no
action rendering. Study for laying out list/table screens, pagination, and
navigating deep menu trees with a 3-button pad.

### `jazz-jackrabbit-for-sega-genesis` — platformer with a real test suite
Physics matched to the original in **8.8 fixed-point** (walk/run accel, variable
jump, gravity-while-rising, mask-ramp following); a **streaming VRAM cache** over
256×64 maps on Plane A with a sky gradient on Plane B; **data-driven entities**
from the original event grids resolved through per-level tables; an **8-slot
enemy pool** that scans one map column per frame to bound 68000 cost; a
platform-neutral core (`src/game_core.c` + `src/jj1_runtime.c`) that compiles
**both** on the host and in the ROM; Python converters (`tools/build_sgdk_jj1_*`)
that read a shareware install and emit compiled-in `.inc` data; and the full
**two-tier test harness** (`tests/run_host_tests.sh`,
`tests/run_headless_emulator_test.sh`, `libretro_harness.c`) that this skill's
`references/testing.md` is based on.

### `fzero-clone-md` ("Neon Circuit") — pseudo-3D / affine streaming
An F-Zero-style racer. The instructive part is the **performance revision**: it
abandons SGDK's ~20 KiB bitmap-engine transfer and instead converts a 256×96
affine ground plane to *tile* data, streams it as **two 6 KiB DMA halves into a
double VRAM bank**, and displays every second VBlank for a **30 fps** target
while the simulation stays **60 Hz**. Study for DMA bandwidth budgeting, VRAM
double-buffering, and decoupling display rate from simulation rate.

### `convert-s3m-vgm-jazzjackrabbit-md` — the music pipeline
Tooling to turn tracker modules into Genesis music: **S3M → VGM → XGM**. Study
when a port ships `.s3m`/`.mod`/OPL music instead of VGM and you need a
re-runnable conversion step feeding a `.res` `XGM2`/`XGM` line. Pairs with
`references/audio.md`.

### `hexgl-fmv-md` — streaming FMV as gameplay
A prototype that plays a recording of BKcore's WebGL *HexGL* racing track as ROM
video across two alternating 640-tile VRAM banks while a player-controlled ship
steers over it as a hardware sprite. The value is the **inter-frame delta
encoding**: each frame stores only runs of 8×8 tiles that changed vs the frame
already in its hidden bank (N−2), so one section DMAs straight from ROM per
VBlank with no full-frame copy and no CPU decode — a *higher* frame rate (15 fps
NTSC) at a *smaller* ROM (2.875 MB, ~79% of raw). The ship poses are true 3D
renders of HexGL's Feisar mesh through a matched chase camera at fixed scale, and
there's a headless capture→convert→encode pipeline plus the two-tier test
harness. Full write-up in Part 2 → *FMV / video*; **non-commercial** asset
licensing caveat noted there.

---

# Part 2 — community index

Broader set of third-party repos, grouped by what each one teaches. URLs are
`https://github.com/<path>`. Star counts and SGDK versions vary; treat these as
technique references, not drop-in libraries.

### Learning paths & tutorial code (start here if new)
- `williamdsw/sgdk-tutorials` — a curated worktree following multiple tutorial
  series (MatteusBeusRetroDev master class, ohsat's Megalaga/MegaPong/MegaRunner/
  MegaTiler). Good map of the beginner ecosystem.
- `bolon667/sgdkTutorialsCode` — companion code for the Under-Prog.Ru tutorials.
- `fabiof17/Tuto_SGDK_Vigilante` — a guided "Vigilante" demo build; clear install
  + step structure for someone who knows C but not SGDK.
- `radioation/SGDKRocks` — assorted example programs plus a playable Asteroids,
  written as learning notes (updated to SGDK 2.11).

### Pong / Breakout / Invaders clones (smallest complete games)
- `theshaneobrien/MegaDriveBreakout` — Breakout built on ohsat's MegaPong
  groundwork; runs 60 fps on hardware. Simplest "whole game" to read end-to-end.
- `axmandm/sgdk-space-invaders` — Invaders clone crediting ohsat for loop/
  collision/entity structure; has a CHANGELOG tracking incremental bring-up.
- `NeroJin/TetrisMD` — a complete Tetris (hold piece, hard/soft drop, rotation);
  good state-machine + input-mapping reference.
- `Nightwolf-47/KleleAtoms-MD` — turn-based grid game ("atoms") port from a
  desktop original; clean example of porting simple turn logic to a pad UI.

### Platformers & collision (the most-asked genre)
- `makeclassicgames/exampleSGDKPlatformer` and `.../sgdkplaformer2` — two
  progressive platformer examples from the "Make Classic Games" streams; the
  second adds **camera scrolling and slope handling** (has a companion video).
- `MechaZone/Slope-collision-for-SGDK` — focused **slope-collision** code (from
  the *Gilgamech* game): builds on Pigsy's basic tile-collision tutorials then
  adds ramp following; uses a 48×48 footprint needing extra probe points, with
  two slope angles (extensible to a third via the mapCollision files). The go-to
  when tile-AABB isn't enough.
- `dougkusanagi/MegaMetroid` — a Super-Metroid-style port-in-progress with a
  simple tile-collision system; study for metroidvania room/camera structure.
- `bearmade/GoblinSGDK` — a finished, released 2D action game (itch.io) with an
  `AssetCatalogue.md`; useful as a "what a shipped project's repo looks like".
- `jerellsworth/FIREBRAWL`, `Hyuan02/GenRunner` (endless runner),
  `DanielMoura79/HAMOOPIG`, `alicesim1/Penguin-World` (isometric) — additional
  full small games in varied styles to compare structure.

### Shmups / arcade
- `haroldo-ok/llamatron-md` — a **twin-stick arena shooter** (Robotron/Llamatron
  style) **built with this very skill**, so it's a faithful worked example of the
  whole workflow: a platform-clean `src/core/` (host-testable, 8.8 fixed-point)
  under an SGDK `src/main.c` shell, generated 16-colour sprite art
  (`tools/gen_art.py`), PSG sound effects, battery **SRAM high-score**, and the
  full two-tier host + Genesis-Plus-GX harness (21 host-oracle checks + a scripted
  emulator playthrough). Nice touches: twin-stick on a one-stick pad via a
  **hold-A aim-lock** (plus true dual-stick on pad 2), and a clean-room reimplementation of
  a 68000-assembly framebuffer original as VDP sprite gameplay. Its "porting
  notes" are independent field-confirmation of the skill's toolchain/68000
  gotchas — `-Wl,--build-id=none`, the LTO trap, linking SGDK's 68000-safe
  `libgcc.a` over Debian's 68020+ one, and the odd-address 8.8-fixed-point read
  fixed by forcing the value through a data register (all already in
  `references/toolchain-and-build.md` and `references/api-and-engine.md`). A
  strong template for any Robotron-like.
- `devpow112/quantum-burst` — a **bullet-hell** shooter with a real CI/release
  setup (license/release badges, GitHub Actions); good build-automation model.
- `sixteenbits/TownQuest` — a game-jam (Almería 2019) MegaDrive game; compact,
  time-boxed scope.
- `StudioVetea/Metal-Slug-Warfare-Demo--Megadrive-` — a Metal-Slug-style demo
  (older SGDK 1.62); run-and-gun sprite/animation reference.

### Raycasting & pseudo-3D (performance-critical)
- `anael-seghezzi/SGDK_raycasting` — the well-known **raycaster** for Genesis;
  the canonical starting point for a first-person 3D view on 68000.
- `fabri1983/raycasting_anael` — an **optimized 320×192 60 Hz** rework of Anael's
  raycaster; read alongside the original to see what the 68000 needs for real-
  time raycasting (tile-column tricks, avoiding per-pixel division).
- `radioation/MegaDriving` — **pseudo-3D roads** (Lou's Pseudo-3D method) and
  other fake-3D effects; the reference for an Outrun/Road-Rash-style road. Pairs
  with `haroldo-ok/fzero-clone-md` (affine-as-tiles approach).
- `massie0414/SGDK_3D` — a **polygon 3D** demo (video in README; SGDK 1.41).
- `radioation/SGDK_Scrolling` — not 3D but essential for the above: a taxonomy of
  **scrolling** techniques (`MAP` type for large images; `VDP_setTileMapEx` with
  `HSCROLL_PLANE` vs `HSCROLL_TILE` for parallax). Read before any scrolling task.

### Racing
- `realbrucest/Mega-Car-Wars` — an **isometric** adventure/racing game; combines
  iso rendering with racing feel.

### Text / RPG / narrative engines
- `haroldo-ok/choice4genesis` — a **ChoiceScript-style transpiler** (JS) that
  emits SGDK C for choose-your-own-adventure / visual-novel games. Use when the
  deliverable is branching text/slideshow rather than an action game.
- `tryphon77/rpg-text` and `garrettjwilke/tidytext` — text-RPG scaffolding and
  (tidytext) a **variable-width / kerned font** system: left-aligned glyphs in
  8×8 tiles, a per-char width table, pixel-masking and cross-tile packing to fit
  far more text per line than the fixed 8-px font. Essential for text-heavy games.
- `VagnoSilva/accented-text-tileset` — a tileset for **accented characters**
  (needed for non-English text); pairs with the font work above.

### Effects, intros & UI widgets
- `a-dietrich/SEGA-Genesis-Demos` — a demo pack: console/log window, **palette
  fading**, **scaling**, SEGA logo, a slideshow, and a **wobble/distortion**
  effect. Mine individual effects here.
- `makeclassicgames/BoingSGDK` — the "Boing" ball demo from *Code the Classics
  Vol.1*, ported and **fully commented**; a gentle, well-annotated read.
- `VagnoSilva/IntroAnimada` — an animated intro sequence.
- `VagnoSilva/lifebar_for_SGDK_Games` — a **health/life bar via tile
  manipulation** (partial-fill by swapping tiles); a self-contained HUD widget.
- `Rahzelk/sap-for-sgdk` — a **Sweep-and-Prune** broadphase collision algorithm;
  the README notes it pays off beyond ~20 sprites (below that, plain AABB wins).

### FMV / video
- `haroldo-ok/hexgl-fmv-md` — **FMV as *gameplay*, not just cutscene**: streams a
  recording of BKcore's WebGL *HexGL* Cityscape track (256×160, 180 frames, one
  ~18 s lap, 15 colors/frame) as ROM video across two alternating 640-tile VRAM
  banks while a player-controlled Feisar ship steers over it as a hardware
  sprite. Two techniques make it the reference for video-on-a-cart:
  - **Inter-frame delta encoding (the headline trick).** `tools/encode_delta.py`
    compares frame N against **N−2** — the frame already sitting in its hidden
    VRAM bank — and stores only contiguous *runs of changed 8×8 tiles*, split
    into four independent 160-tile sections. At runtime one section is **DMAed
    straight from ROM per VBlank**; unchanged tiles just stay in the hidden bank,
    so there is **no full-frame copy and no CPU pixel decompression**. The result
    is a *higher* frame rate at a *smaller* ROM: ~79% of the raw stream, stable
    **15 fps NTSC** (a new frame every 4 hardware frames), 2.875 MB down from
    3.625 MB, and it retired an earlier 40 KB RAM delta-buffer entirely (only
    VRAM holds the two reconstructed frames). This is the pattern to copy for any
    large streamed media on Genesis.
  - **Genuine 3D ship poses, not rotated 2D.** `tools/render_chase_ship.py` poses
    the *original* Feisar mesh in Three.js under a camera that recreates HexGL's
    `CameraChase` (FOV 70°, matched offsets/look-ahead) and renders five steering
    poses, all through the **same camera and a fixed 128×96 crop → 64×48 resize**
    so silhouette/perspective change naturally while projection scale stays
    constant — no per-pose 2D rescaling.
  Ships a real headless **capture→convert→encode pipeline**
  (`tools/capture_hexgl.py` drives the original WebGL game via Selenium/Chromium
  along a centerline from HexGL's own collision map → `convert_assets.py`
  letterboxes/quantizes → `encode_delta.py` produces the cart stream) plus the
  two-tier host + Genesis-Plus-GX harness (a cadence test asserts 15/15
  four-frame intervals each carry a new FMV frame; a fixed-scale sprite
  regression check). A feasibility slice (one on-track lap, throttle + bounded
  ±28 px lane steering, no collision branches, no sound); its "current
  limitations" are a good spec for a production FMV racer (synchronized
  left/center/right branch clips, H-blank palette bands, streamed audio within
  the Z80/VDP DMA budget). **Licensing caveat:** HexGL assets are partly
  CC BY-NC 3.0 — treat as **non-commercial** unless rights are cleared.
- `matthewbennion/SGDKCodeForFMV` — plays **video built from PNG frames**; the
  code side of a full-motion-video cutscene.
- `fabri1983/sgdk-video-player` — a **video converter + player** (Java tool +
  SGDK C) for streaming video off the cartridge. Study ROM-streaming and
  frame-pacing for large media.

### Asset pipelines & exporters (tooling, not games)
- `retrodevbr/sgdk-aseprite-scripts` — **Aseprite** Lua scripts that quantize art
  to the Genesis 9-bit palette (the 8 discrete channel levels: 0, 49, 87, 119,
  146, 174, 206, 255). Run these before rescomp to avoid off-palette colours.
- `fiddd0/godot2sgdk` — a **Godot 4 editor plugin** that exports TileMaps and
  Sprite2D/AnimatedSprite2D to SGDK-compatible C headers (16-colour palette
  optimization, `SpriteDefinition` structs). Use when a team authors levels in
  Godot. (Verify generated code against current SGDK; it's AI-assisted.)

### Roguelike / procedural
- `bearmade/SGDK-ASCII-roguelike` — a small **ASCII roguelike** using cellular
  automata for map generation; the reference for text-grid + procedural gen on
  Genesis (pairs with the tidytext font work for a denser grid).

---

# Version drift — read before lifting community code

SGDK renamed much of its fixed-point and scroll API between 1.x and 2.x. Several
repos above (radioation's especially) ship `sed` one-liners in their READMEs to
migrate old code — the same renames you'll hit porting any pre-2.0 sample. The
common ones:

| Old (≤ 1.x) | New (2.x, this skill's target) |
|---|---|
| `fix16Div`, `fix32Div` | `F16_div`, `F32_div` |
| `fix16ToInt`, `fix32ToInt` | `F16_toInt`, `F32_toInt` |
| `fix16Mul`, `fix32Mul` | `F16_mul`, `F32_mul` |
| `intToFix16`/`FIX16` | `FIX16()` / `F16_fromInt` (check header) |
| `VSCROLL_2TILE` | `VSCROLL_COLUMN` |
| `VDP_waitVInt()` at end of frame | `SYS_doVBlankProcess()` |

If an old sample won't compile, suspect a renamed symbol first — grep the
installed SGDK headers (`inc/`) for the new name rather than guessing. Bitmap-
engine and some `VDP_*` tilemap signatures also changed; confirm against the
online doxygen for the exact version you build with.

## How to use all of the above

1. Match your task to the closest project (technique first, genre second).
2. `git clone --depth 1` it; read README → Makefile → `res/*.res` → `tools/` →
   `tests/`.
3. Copy the *pattern*, port symbol names to 2.x per the table, and verify against
   the installed SGDK headers / doxygen before trusting any snippet.
