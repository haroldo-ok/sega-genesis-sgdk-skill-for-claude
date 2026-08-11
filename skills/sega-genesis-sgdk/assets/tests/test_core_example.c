/* test_core_example.c — Tier-1 host oracle.
 *
 * Compiles your platform-clean core with the HOST cc (no SGDK, no emulator)
 * and asserts on game state. This is where you verify physics, collision,
 * economy, and state-machine logic in milliseconds, with gdb/asan available.
 *
 * Your core .c files must compile on the host: guard any SGDK-only include or
 * call with #ifndef HOST_BUILD, and pass -DHOST_BUILD here. Types like
 * s16/u16/F16 come from a small host shim (below) or from your own types.h.
 *
 * Build (see run_host_tests.sh):
 *   cc -std=c99 -Wall -Wextra -Werror -DHOST_BUILD -Iinc -Isrc \
 *      src/core/game_core.c tests/test_core_example.c -o core_tests && ./core_tests
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Minimal host stand-ins for SGDK integer/fixed types, if your core uses them
   and you don't already provide a host-friendly types.h. Keep widths EXACT so
   host behaviour matches the 68000 target. */
#ifdef HOST_BUILD
typedef int8_t   s8;  typedef uint8_t  u8;
typedef int16_t  s16; typedef uint16_t u16;
typedef int32_t  s32; typedef uint32_t u32;
typedef s16 F16;  /* if your core uses 10.6 F16, mirror the macros you rely on */
#endif

/* #include "core/game_core.h"   // your core's public API */

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else         { printf("ok   %s\n", #cond); } \
} while (0)

int main(void)
{
    /* Example shape — replace with real core calls:
     *
     *   GameState g; core_init(&g);
     *   for (int f = 0; f < 60; f++) core_step(&g, INPUT_RIGHT);
     *   CHECK(g.player.x > 0);                 // player moved right
     *   CHECK(g.player.on_ground);             // landed
     *   CHECK(core_score(&g) == 0);            // nothing collected yet
     */
    CHECK(1 == 1);   /* placeholder so the harness compiles out of the box */

    if (failures) { printf("\n%d assertion(s) failed\n", failures); return 1; }
    printf("\nall host-core assertions passed\n");
    return 0;
}
