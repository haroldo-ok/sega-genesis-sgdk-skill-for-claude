/* libretro_harness.c — headless Genesis point-to-point test host.
 *
 * Loads a libretro core (Genesis Plus GX), runs a ROM for a scripted number of
 * frames with scripted joypad input, and records evidence a test can assert on:
 * frames (PPM), 68000 work-RAM dumps, audio energy per checkpoint, an optional
 * WAV of everything the console played, and the first bytes of cartridge SRAM
 * (for an on-target self-test marker). No display or audio device is needed,
 * so it runs identically in CI.
 *
 * Adapted from the harness in haroldo-ok/jazz-jackrabbit-for-sega-genesis; the
 * RAM-dump, button-mash, audio-energy and WAV features follow techniques proven
 * in haroldo-ok's wolf-3d-md, punch-out-pizza-tower-edition-md and tuxemon-md
 * ports (re-implemented here, not copied).
 *
 * Build:
 *   cc -O2 -o harness libretro_harness.c -I<dir with libretro.h> -ldl -lm
 *
 * Usage:
 *   harness CORE.so ROM.bin TOTAL_FRAMES SCRIPT OUT_PREFIX
 *
 * SCRIPT is a comma-separated list of directives (frames count from 0):
 *   <first>-<last>:<button>          hold a button over the inclusive range
 *   mash@<first>-<last>:<period>:<b1>+<b2>[+...]
 *                                    tap the listed buttons in turn, each held
 *                                    for period/2 frames (e.g. "get up" mashing)
 *   shot@<frame>                     write OUT_PREFIX<frame>.ppm
 *   ram@<frame>                      write OUT_PREFIX<frame>.ram (64 KB work RAM
 *                                    exactly as the core exposes it — Genesis
 *                                    Plus GX stores it byte-swapped in 16-bit
 *                                    words; tools/ramsym.py handles that)
 *   audiostat@<frame>                print the mean |sample| since the previous
 *                                    audiostat (or boot) to stdout:
 *                                    "audiostat@N samples=S avg_energy=E"
 * Buttons: up down left right a b c start; prefix "p2." for controller 2
 * (e.g. "100-160:p2.left").
 *
 * Environment:
 *   HARNESS_WAV=path   also write all audio as a 16-bit stereo WAV file.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "libretro.h"

#define MAX_DIRECTIVES 256
#define MAX_MASH_BTNS  4

typedef struct { unsigned first, last, id, port; } Hold;
typedef struct { unsigned first, last, period, n; unsigned id[MAX_MASH_BTNS], port[MAX_MASH_BTNS]; } Mash;
typedef struct { unsigned frame; } Mark;

static Hold holds[MAX_DIRECTIVES];   static unsigned holdCount;
static Mash mashes[MAX_DIRECTIVES];  static unsigned mashCount;
static Mark shots[MAX_DIRECTIVES];   static unsigned shotCount;
static Mark rams[MAX_DIRECTIVES];    static unsigned ramCount;
static Mark astats[MAX_DIRECTIVES];  static unsigned astatCount;

static unsigned currentFrame;
static const char *outPrefix;

static enum retro_pixel_format pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t lastRGB[1024 * 512 * 3];
static unsigned lastW, lastH, haveFrame;

/* audio accounting */
static double   energySum;
static uint64_t energySamples;
static FILE    *wavOut;
static uint32_t wavFrames;
static unsigned wavRate = 44100;

/* Map DSL button names to RetroPad ids. D-pad and START are unambiguous.
 * A/B/C follow Genesis Plus GX's default Mega Drive mapping
 * (Genesis A=RetroPad Y, B=RetroPad B, C=RetroPad A); swap here if your core
 * build maps differently. */
static int button_id(const char *name, unsigned *port)
{
    *port = 0;
    if (!strncmp(name, "p2.", 3)) { *port = 1; name += 3; }
    if (!strcmp(name, "up"))    return RETRO_DEVICE_ID_JOYPAD_UP;
    if (!strcmp(name, "down"))  return RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (!strcmp(name, "left"))  return RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (!strcmp(name, "right")) return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (!strcmp(name, "start")) return RETRO_DEVICE_ID_JOYPAD_START;
    if (!strcmp(name, "a"))     return RETRO_DEVICE_ID_JOYPAD_Y;   /* Genesis A */
    if (!strcmp(name, "b"))     return RETRO_DEVICE_ID_JOYPAD_B;   /* Genesis B */
    if (!strcmp(name, "c"))     return RETRO_DEVICE_ID_JOYPAD_A;   /* Genesis C */
    fprintf(stderr, "unknown button '%s'\n", name);
    return -1;
}

static void add_mark(Mark *arr, unsigned *count, const char *num)
{
    if (*count < MAX_DIRECTIVES) arr[(*count)++].frame = (unsigned)atoi(num);
}

static void parse_mash(const char *spec)
{
    unsigned a, b, per; char btns[96];
    if (sscanf(spec, "%u-%u:%u:%95s", &a, &b, &per, btns) != 4 || !per || mashCount >= MAX_DIRECTIVES) {
        fprintf(stderr, "bad mash directive 'mash@%s'\n", spec); return;
    }
    Mash *m = &mashes[mashCount];
    m->first = a; m->last = b; m->period = per; m->n = 0;
    for (char *t = strtok(btns, "+"); t && m->n < MAX_MASH_BTNS; t = strtok(NULL, "+")) {
        unsigned port; int id = button_id(t, &port);
        if (id >= 0) { m->id[m->n] = (unsigned)id; m->port[m->n] = port; m->n++; }
    }
    if (m->n) mashCount++;
}

static void parse_script(char *s)
{
    /* split on ',' by hand: parse_mash uses strtok internally */
    char *tok = s;
    while (tok && *tok) {
        char *next = strchr(tok, ',');
        if (next) *next++ = 0;
        if      (!strncmp(tok, "shot@", 5))       add_mark(shots, &shotCount, tok + 5);
        else if (!strncmp(tok, "ram@", 4))        add_mark(rams, &ramCount, tok + 4);
        else if (!strncmp(tok, "audiostat@", 10)) add_mark(astats, &astatCount, tok + 10);
        else if (!strncmp(tok, "mash@", 5))       parse_mash(tok + 5);
        else {
            char btn[24]; unsigned a, b, port;
            if (sscanf(tok, "%u-%u:%23s", &a, &b, btn) == 3 && holdCount < MAX_DIRECTIVES) {
                int id = button_id(btn, &port);
                if (id >= 0) { holds[holdCount] = (Hold){ a, b, (unsigned)id, port }; holdCount++; }
            } else if (*tok) {
                fprintf(stderr, "ignoring directive '%s'\n", tok);
            }
        }
        tok = next;
    }
}

/* --- libretro frontend callbacks --- */

static void cb_log(enum retro_log_level lvl, const char *fmt, ...)
{
    (void)lvl; va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

static bool cb_env(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixFmt = *(const enum retro_pixel_format *)data; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = cb_log; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = "."; return true;
    default:
        return false;   /* decline everything else */
    }
}

static void put_rgb(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b)
{ dst[0] = r; dst[1] = g; dst[2] = b; }

static void cb_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;                         /* duped frame: keep last */
    if (w > 1024 || h > 512) return;
    lastW = w; lastH = h; haveFrame = 1;
    const uint8_t *src = (const uint8_t *)data;
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            uint8_t *o = &lastRGB[(y * w + x) * 3];
            if (pixFmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                const uint32_t p = ((const uint32_t *)(src + y * pitch))[x];
                put_rgb(o, (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
            } else if (pixFmt == RETRO_PIXEL_FORMAT_RGB565) {
                const uint16_t p = ((const uint16_t *)(src + y * pitch))[x];
                put_rgb(o, ((p >> 11) & 0x1F) << 3, ((p >> 5) & 0x3F) << 2, (p & 0x1F) << 3);
            } else { /* 0RGB1555 (default) */
                const uint16_t p = ((const uint16_t *)(src + y * pitch))[x];
                put_rgb(o, ((p >> 10) & 0x1F) << 3, ((p >> 5) & 0x1F) << 3, (p & 0x1F) << 3);
            }
        }
    }
}

static void cb_poll(void) {}

static int16_t cb_input(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port > 1 || device != RETRO_DEVICE_JOYPAD) return 0;
    for (unsigned i = 0; i < holdCount; i++)
        if (holds[i].id == id && holds[i].port == port &&
            currentFrame >= holds[i].first && currentFrame <= holds[i].last)
            return 1;
    for (unsigned i = 0; i < mashCount; i++) {
        const Mash *m = &mashes[i];
        if (currentFrame < m->first || currentFrame > m->last) continue;
        unsigned t = currentFrame - m->first;
        unsigned which = (t / m->period) % m->n;          /* whose turn */
        unsigned down  = (t % m->period) < (m->period + 1) / 2;
        if (down && m->id[which] == id && m->port[which] == port) return 1;
    }
    return 0;
}

static void wav_header(FILE *f, uint32_t frames, unsigned rate)
{
    uint32_t data = frames * 4, riff = 36 + data, byterate = rate * 4, fmtlen = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    uint32_t r = rate;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&r, 4, 1, f); fwrite(&byterate, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

/* The host is little-endian (x86/ARM CI), matching the WAV sample layout. */
static void account_audio(const int16_t *d, size_t frames)
{
    for (size_t i = 0; i < frames * 2; i++) energySum += d[i] < 0 ? -d[i] : d[i];
    energySamples += frames * 2;
    if (wavOut) { fwrite(d, 4, frames, wavOut); wavFrames += (uint32_t)frames; }
}

static size_t cb_audio_batch(const int16_t *d, size_t frames) { account_audio(d, frames); return frames; }
static void   cb_audio(int16_t l, int16_t r) { int16_t s[2] = { l, r }; account_audio(s, 1); }

static void write_ppm(unsigned frame)
{
    if (!haveFrame) return;
    char path[512];
    snprintf(path, sizeof(path), "%s%u.ppm", outPrefix, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", lastW, lastH);
    fwrite(lastRGB, 3, (size_t)lastW * lastH, f);
    fclose(f);
    fprintf(stderr, "wrote %s (%ux%u)\n", path, lastW, lastH);
}

/* Function-pointer typedefs so a plain-identifier LOAD macro works. */
typedef void   (*fn_set_env)(retro_environment_t);
typedef void   (*fn_set_video)(retro_video_refresh_t);
typedef void   (*fn_set_asample)(retro_audio_sample_t);
typedef void   (*fn_set_abatch)(retro_audio_sample_batch_t);
typedef void   (*fn_set_ipoll)(retro_input_poll_t);
typedef void   (*fn_set_istate)(retro_input_state_t);
typedef void   (*fn_void)(void);
typedef bool   (*fn_load)(const struct retro_game_info *);
typedef void   (*fn_set_ctrl)(unsigned, unsigned);
typedef void  *(*fn_mem_data)(unsigned);
typedef size_t (*fn_mem_size)(unsigned);
typedef void   (*fn_av_info)(struct retro_system_av_info *);

static fn_mem_data g_mem_data;
static fn_mem_size g_mem_size;

static void write_ram(unsigned frame)
{
    uint8_t *ram = (uint8_t *)g_mem_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t   n   = g_mem_size(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram || !n) { fprintf(stderr, "core exposes no system RAM\n"); return; }
    char path[512];
    snprintf(path, sizeof(path), "%s%u.ram", outPrefix, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(ram, 1, n, f);
    fclose(f);
    fprintf(stderr, "wrote %s (%zu bytes)\n", path, n);
}

static void print_audiostat(unsigned frame)
{
    double avg = energySamples ? energySum / (double)energySamples : 0.0;
    printf("audiostat@%u samples=%llu avg_energy=%.1f\n",
           frame, (unsigned long long)energySamples, avg);
    fflush(stdout);
    energySum = 0; energySamples = 0;
}

#define LOAD(T, var, sym) T var = (T)dlsym(h, sym); \
    if (!var) { fprintf(stderr, "missing symbol %s\n", sym); return 2; }

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s CORE.so ROM.bin TOTAL_FRAMES SCRIPT OUT_PREFIX\n", argv[0]);
        return 1;
    }
    const char *corePath = argv[1], *romPath = argv[2];
    unsigned total = (unsigned)atoi(argv[3]);
    char scriptBuf[8192]; strncpy(scriptBuf, argv[4], sizeof(scriptBuf) - 1);
    scriptBuf[sizeof(scriptBuf) - 1] = 0;
    outPrefix = argv[5];
    parse_script(scriptBuf);

    void *h = dlopen(corePath, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    LOAD(fn_set_env,     set_environment, "retro_set_environment");
    LOAD(fn_set_video,   set_video,       "retro_set_video_refresh");
    LOAD(fn_set_asample, set_asample,     "retro_set_audio_sample");
    LOAD(fn_set_abatch,  set_abatch,      "retro_set_audio_sample_batch");
    LOAD(fn_set_ipoll,   set_ipoll,       "retro_set_input_poll");
    LOAD(fn_set_istate,  set_istate,      "retro_set_input_state");
    LOAD(fn_void,        r_init,          "retro_init");
    LOAD(fn_load,        r_load_game,     "retro_load_game");
    LOAD(fn_void,        r_run,           "retro_run");
    LOAD(fn_set_ctrl,    set_ctrl,        "retro_set_controller_port_device");
    LOAD(fn_mem_data,    mem_data,        "retro_get_memory_data");
    LOAD(fn_mem_size,    mem_size,        "retro_get_memory_size");
    LOAD(fn_av_info,     av_info,         "retro_get_system_av_info");
    g_mem_data = mem_data; g_mem_size = mem_size;

    set_environment(cb_env);
    set_video(cb_video);
    set_asample(cb_audio);
    set_abatch(cb_audio_batch);
    set_ipoll(cb_poll);
    set_istate(cb_input);
    r_init();

    /* Load ROM both by path and from memory so the core can use either. */
    FILE *rf = fopen(romPath, "rb");
    if (!rf) { fprintf(stderr, "cannot open %s\n", romPath); return 2; }
    fseek(rf, 0, SEEK_END); long sz = ftell(rf); fseek(rf, 0, SEEK_SET);
    void *rom = malloc((size_t)sz);
    if (fread(rom, 1, (size_t)sz, rf) != (size_t)sz) { fprintf(stderr, "read fail\n"); return 2; }
    fclose(rf);

    struct retro_game_info gi;
    memset(&gi, 0, sizeof(gi));
    gi.path = romPath; gi.data = rom; gi.size = (size_t)sz; gi.meta = NULL;
    if (!r_load_game(&gi)) { fprintf(stderr, "retro_load_game failed\n"); return 2; }
    set_ctrl(0, RETRO_DEVICE_JOYPAD);
    set_ctrl(1, RETRO_DEVICE_JOYPAD);

    const char *wavPath = getenv("HARNESS_WAV");
    if (wavPath) {
        struct retro_system_av_info av; memset(&av, 0, sizeof(av));
        av_info(&av);
        if (av.timing.sample_rate > 1000) wavRate = (unsigned)(av.timing.sample_rate + 0.5);
        wavOut = fopen(wavPath, "wb");
        if (wavOut) wav_header(wavOut, 0, wavRate);     /* placeholder, patched at exit */
    }

    for (currentFrame = 0; currentFrame < total; currentFrame++) {
        r_run();
        for (unsigned i = 0; i < shotCount; i++)  if (shots[i].frame == currentFrame)  write_ppm(currentFrame);
        for (unsigned i = 0; i < ramCount; i++)   if (rams[i].frame == currentFrame)   write_ram(currentFrame);
        for (unsigned i = 0; i < astatCount; i++) if (astats[i].frame == currentFrame) print_audiostat(currentFrame);
    }

    if (wavOut) {
        wav_header(wavOut, wavFrames, wavRate);
        fclose(wavOut);
        fprintf(stderr, "wrote %s (%u frames @ %u Hz)\n", wavPath, wavFrames, wavRate);
    }

    /* Print the first SRAM bytes for an on-target self-test marker.
       Genesis SRAM is on odd bytes; some cores interleave with 0xFF. */
    uint8_t *sram = (uint8_t *)mem_data(RETRO_MEMORY_SAVE_RAM);
    size_t   ssz  = mem_size(RETRO_MEMORY_SAVE_RAM);
    if (sram && ssz) {
        printf("SRAM:");
        for (size_t i = 0; i < ssz && i < 16; i++) printf(" %02X", sram[i]);
        printf("\n");
    }

    free(rom);
    return 0;
}
