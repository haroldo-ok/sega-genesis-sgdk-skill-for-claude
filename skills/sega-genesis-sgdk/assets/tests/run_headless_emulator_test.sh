#!/usr/bin/env bash
# Tier 2 — headless emulator. Boots the real out/rom.bin under Genesis Plus GX
# (libretro) with no display, feeds scripted input, dumps frames, and asserts
# the ROM is lit, colourful, and advances (i.e. NOT a black screen).
#
# Requires: GDK set, a native cc, git, make. No X server / GPU / audio device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${GDK:?Set GDK to your SGDK dir (or run inside the doragasu docker image).}"
TMP="$ROOT/tests/.tmp"; ART="$ROOT/tests/artifacts"; mkdir -p "$TMP" "$ART"
CORE="${GPGX_CORE:-$TMP/genesis_plus_gx_libretro.so}"

# 1. Build (or reuse) the Genesis Plus GX libretro core.
if [[ ! -f "$CORE" ]]; then
  [[ -d "$TMP/gpgx" ]] || git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git "$TMP/gpgx"
  make -C "$TMP/gpgx" -f Makefile.libretro platform=unix -j"$(nproc)"
  cp "$TMP/gpgx/genesis_plus_gx_libretro.so" "$CORE"
fi

# 2. Build the headless harness. It needs libretro.h: use LIBRETRO_INC if set,
#    else the GPGX checkout (cloned here even when GPGX_CORE was supplied).
LIBRETRO_INC="${LIBRETRO_INC:-$TMP/gpgx/libretro/libretro-common/include}"
if [[ ! -f "$LIBRETRO_INC/libretro.h" ]]; then
  [[ -d "$TMP/gpgx" ]] || git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git "$TMP/gpgx"
  LIBRETRO_INC="$TMP/gpgx/libretro/libretro-common/include"
fi
cc -O2 -o "$TMP/harness" "$ROOT/tests/libretro_harness.c" -I"$LIBRETRO_INC" -ldl -lm

# 3. Build the ROM.
make -C "$ROOT" clean >/dev/null
make -C "$ROOT"
test -f "$ROOT/out/rom.bin" || { echo "FAIL: out/rom.bin not produced"; exit 1; }

# 3b. Static header checks (ROM-end vs file size, checksum, SRAM fields).
#     Add --strict to fail on a ROM-end mismatch instead of warning.
python3 "$ROOT/tests/rom_sanity.py" "$ROOT/out/rom.bin"

# 4. Smoke playthrough: snapshot the title, press START, hold RIGHT, snapshot.
#    Tune the frame ranges/buttons to your game's real states. Also dumps work
#    RAM at the end (inspect with tests/ramsym.py) and prints audio energy at
#    two checkpoints; add HARNESS_WAV=file.wav to keep what was played.
"$TMP/harness" "$CORE" "$ROOT/out/rom.bin" 300 \
  "shot@30,audiostat@100,120-160:start,200-260:right,shot@250,ram@250,audiostat@250" \
  "$ART/frame_" | tee "$TMP/harness.out"

# 5. Assert on the dumped frames: lit, colourful, changed over time, and not
#    SGDK's exception (crash) screen.
python3 "$ROOT/tests/check_frames.py" "$ART/frame_30.ppm" "$ART/frame_250.ppm"

# 5b. (Optional) prove sound actually plays, not just links: assert the energy
#     printed at a checkpoint after a sound-triggering input is far above a
#     quiet one, e.g.
#       grep -o 'audiostat@250.*avg_energy=[0-9.]*' "$TMP/harness.out"

# 6. (Optional) on-target self-test marker in SRAM — see testing.md.
#    Build with -DSELFTEST, run, and grep the printed SRAM head for your marker,
#    matching BOTH the plain and 0xFF-interleaved forms (odd-byte SRAM).

echo "PASS: headless emulator boot + playthrough"
