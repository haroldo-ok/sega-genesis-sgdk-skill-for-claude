/* rom_header.c — Sega Genesis / Mega Drive cartridge header.
 *
 * This file is special: SGDK's makefile.gen EXCLUDES it from the normal source
 * wildcard and links it at a fixed location. Keep it named rom_header.c under
 * src/. Edit the strings for your game. The SRAM fields below enable a 64 Kbit
 * battery-backed save; remove them (set to no-SRAM defaults) if you don't save.
 */
#include <genesis.h>

__attribute__((externally_visible))
const ROMHeader rom_header = {
    "SEGA MEGA DRIVE ",                                 /* console (16)          */
    "(C)YOURNAME 2025",                                 /* copyright (16)        */
    "MY GENESIS GAME                                 ", /* title JP  (48)        */
    "MY GENESIS GAME                                 ", /* title INTL(48)        */
    "GM 00000000-00",                                   /* serial (14)           */
    0x000,                                              /* checksum (filled)     */
    "J               ",                                 /* I/O support (16)      */
    0x00000000,                                         /* ROM start             */
    0x000FFFFF,                                         /* ROM end (1 MB; raise) */
    0xE0FF0000,                                         /* RAM start             */
    0xE0FFFFFF,                                         /* RAM end               */
    "RA",                                               /* SRAM sig ("RA"=yes)   */
    0xF820,                                             /* SRAM type (odd bytes) */
    0x00200000,                                         /* SRAM start            */
    0x0020FFFF,                                         /* SRAM end (64 Kbit)    */
    "            ",                                     /* modem (12)            */
    "YOUR NOTES / DESCRIPTION HERE           ",         /* notes (40)            */
    "JUE             "                                  /* region: J U E (16)    */
};
