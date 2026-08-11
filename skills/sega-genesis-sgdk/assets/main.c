/* main.c — minimal SGDK entry point and game loop.
 *
 * The two rules that keep you off a black screen:
 *   1. Seed a palette (and draw something) before the first frame.
 *   2. Call SYS_doVBlankProcess() at the END of every loop iteration — it
 *      flushes the DMA queue, advances the sprite/scroll engines, and waits
 *      for vblank. (Old tutorials say VDP_waitVInt(); that is outdated.)
 */
#include <genesis.h>
/* #include "resources.h"   // generated from res/resources.res */

/* Run game logic at ~20 Hz on a 60 fps NTSC console: tick every 3rd frame.
   Render/animate every frame. Adjust STEP to your source game's logic rate. */
#define LOGIC_EVERY 3

static void update_logic(u16 joy)
{
    (void)joy;
    /* your platform-clean core update goes here */
}

int main(bool hardReset)
{
    (void)hardReset;

    JOY_init();
    SPR_init();                       /* only if you use the sprite engine */

    /* A visible palette + text: proves the pipeline is alive. Replace with
       your real palettes/backgrounds once boot is confirmed. */
    PAL_setColor(15, RGB24_TO_VDPCOLOR(0x00CC44));
    VDP_drawText("SGDK OK - PRESS START", 8, 12);

    u16 prev = 0;
    u16 tick = 0;

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        u16 pressed = joy & ~prev;    /* edge-detected buttons */
        prev = joy;
        (void)pressed;

        if (++tick >= LOGIC_EVERY)
        {
            tick = 0;
            update_logic(joy);
        }

        SPR_update();                 /* if using sprites */
        SYS_doVBlankProcess();        /* MUST be last each frame */
    }
    return 0;
}
