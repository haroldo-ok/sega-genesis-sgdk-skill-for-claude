#!/usr/bin/env bash
# Tier 1 — host oracle. Compiles the platform-clean core with the host cc and
# runs logic assertions. No SGDK toolchain and no emulator needed; runs in ms.
#
# Edit CORE_SRCS to list your core .c files (the ones with NO SGDK/VDP calls).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC_BIN="${CC:-cc}"
TMP="$ROOT/tests/.tmp"; mkdir -p "$TMP"

# --- list your platform-clean core sources here ---
CORE_SRCS=(
  # "$ROOT/src/core/game_core.c"
  # "$ROOT/src/core/physics.c"
)

"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DHOST_BUILD \
  -I"$ROOT/inc" -I"$ROOT/src" \
  "${CORE_SRCS[@]}" \
  "$ROOT/tests/test_core_example.c" \
  -o "$TMP/core_tests"

"$TMP/core_tests"
echo "PASS: host-core tests"
