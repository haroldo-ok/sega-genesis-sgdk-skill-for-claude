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

    /* A visible boot screen that proves the pipeline is alive AND passes the
       skill's Tier-2 checks out of the box (lit, >= 4 colours, changes over
       time). Replace with your real palettes/backgrounds once boot works.
       - PAL0 index 0 is the backdrop colour behind every plane.
       - TILE_SYSTEM_INDEX + n is a built-in solid tile of colour n (SGDK
         fills these 16 tiles at boot), handy for bars and debug markers.
       - The built-in font draws with index 15 of the text palette only. */
    PAL_setColor(0,  RGB24_TO_VDPCOLOR(0x203060));      /* backdrop: navy   */
    PAL_setColor(1,  RGB24_TO_VDPCOLOR(0xE04040));      /* bar colours      */
    PAL_setColor(2,  RGB24_TO_VDPCOLOR(0xE0C040));
    PAL_setColor(3,  RGB24_TO_VDPCOLOR(0x40C060));
    PAL_setColor(15, RGB24_TO_VDPCOLOR(0xFFFFFF));      /* font colour      */
    for (u16 c = 1; c <= 3; c++)
        VDP_fillTileMapRect(BG_B, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE,
                            TILE_SYSTEM_INDEX + c), 8 + (c - 1) * 8, 16, 8, 2);
    VDP_drawText("SGDK OK - PRESS START", 8, 12);

    u16 prev = 0;
    u16 tick = 0;
    u16 frames = 0;
    char num[8];

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

        if ((++frames & 15) == 0)     /* a live frame counter: "it's not hung" */
        {
            uintToStr(frames >> 4, num, 4);
            VDP_drawText(num, 18, 20);
        }

        SPR_update();                 /* if using sprites */
        SYS_doVBlankProcess();        /* MUST be last each frame */
    }
    return 0;
}
