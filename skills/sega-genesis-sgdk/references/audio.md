# Audio: music & sound on the Genesis with SGDK

Genesis sound hardware: **YM2612** (6-channel FM, one channel doubles as an
8-bit PCM DAC) + **SN76489 PSG** (3 square + 1 noise), both driven by a **Z80**
running a sound driver out of Z80 RAM. SGDK ships driver(s) and tools so you
work with tracks and samples, not registers. The 68000 kicks off playback and
feeds the driver; the Z80 does the per-tick work.

## Drivers: XGM2 (current) vs XGM (legacy)

- **XGM2** — the current default. Better PCM, more channels, tooling is
  `xgm2tool`. Prefer it for new work. Resource type `XGM2`.
- **XGM** — older, still supported; tool `xgmtool`. You'll see it in older
  projects and tutorials. Resource type `XGM`.

Both play **VGM** logs (register dumps) compiled to the driver's packed format.
So the real question for music is always: *how do I get a good VGM?*

## Music: getting a VGM/XGM

1. Compose in a tracker that targets the Genesis and exports VGM:
   **Deflemask** or **Furnace** (free, actively developed). Author using the
   YM2612+PSG chip set so the export is hardware-faithful.
2. Drop the `.vgm` in `res/music/` and declare it:
   ```
   XGM2 bgm_level1 "music/level1.vgm"
   ```
   rescomp runs `xgm2tool` to pack it.
3. Play it:
   ```c
   XGM2_play(bgm_level1);        // resource symbol is a const u8[]
   XGM2_playTrack(trackId);      // multi-track bank variant
   XGM2_pause(); XGM2_resume(); XGM2_stop();
   XGM2_setLoopNumber(-1);       // -1 = loop forever
   if (XGM2_isPlaying()) ...
   ```
   (Legacy driver: `XGM_startPlay(song)` / `XGM_setLoopNumber(...)`.)

## Sound effects: WAV → PCM

Sound effects are 8-bit PCM samples the XGM2 driver mixes on the YM2612 DAC.
rescomp converts any `.wav` you give it (rate, depth, channels) to the driver's
format, so the only rate that matters is the **out_rate** field:

```
WAV sfx_jump "sfx/jump.wav" XGM2            # out_rate defaults to 13300
WAV sfx_step "sfx/step.wav" XGM2 6650       # half rate: half the ROM
```

```c
XGM2_playPCM(sfx_jump, sizeof(sfx_jump), SOUND_PCM_CH2);          // 13300 samples
// 6650 samples MUST be played with halfRate = TRUE:
XGM2_playPCMEx(sfx_step, sizeof(sfx_step), SOUND_PCM_CH3, 6, TRUE, FALSE);
//                                        channel, priority, halfRate, loop
XGM2_stopPCM(SOUND_PCM_CH2);
```

**The rate rule (measured, not just documented).** The XGM2 driver plays PCM
at exactly **13,300 Hz**, or 6,650 Hz when you pass `halfRate = TRUE`. rescomp's
spec says XGM2 accepts only 6650 or 13300, but rescomp 2.12 *accepts any value
without a warning* and resamples to it — and the driver still plays at its own
rate. A 1-second 440 Hz tone captured through the harness (`HARNESS_WAV`):

| `.res` out_rate | call | plays as |
|---|---|---|
| 13300 | `XGM2_playPCM` | 1.02 s, 440 Hz ✔ |
| 6650 | `XGM2_playPCMEx(..., halfRate=TRUE, ...)` | 1.02 s, 440 Hz ✔ |
| 6650 | `XGM2_playPCM` | 0.52 s, 880 Hz ✘ (2× fast, octave up) |
| 8000 | `XGM2_playPCM` | 0.62 s, 730 Hz ✘ (1.66× fast) |

So: 13300 + `XGM2_playPCM`, or 6650 + `halfRate`. Nothing else. An energy-only
audio test won't catch a wrong rate — listen once, or check pitch/duration from
a captured WAV (`testing.md`).

**Channels.** XGM2 mixes **three** PCM channels: `SOUND_PCM_CH1` (normally
used by the music's own samples), `SOUND_PCM_CH2`, `SOUND_PCM_CH3`, or
`SOUND_PCM_CH_AUTO`. (`SOUND_PCM_CH4` exists in the shared `SoundPCMChannel`
enum for other drivers such as PCM4, not XGM2.) `XGM2_playPCM` uses priority 6,
below the music's PCM (7), and returns FALSE when no channel is free. Put SFX
on CH2/CH3 when the music uses PCM.

Keep SFX short — PCM is ROM-hungry (13.3 KB per second at full rate). rescomp
pads samples to 256-byte multiples and aligns them as the driver requires.

## Porting a game's music (the S3M/MOD → VGM → XGM route)

Ported games often come with **tracker modules** (`.s3m`, `.mod`, `.xm`) or
sampled streams rather than VGM. The `convert-s3m-vgm-jazzjackrabbit-md` project
is a worked example of exactly this pipeline. General recipe:

1. **Identify the source format.** Adlib/OPL logs, S3M/MOD patterns, sampled
   music, or a game-specific sequence format.
2. **Render/convert to VGM.** Route the module through a converter or a
   VGM-logging player so you get a YM2612+PSG register log. For sampled/OPL
   sources you approximate on FM (hand-map instruments) or stream as PCM if
   the tune is short and ROM allows.
3. **Compile to XGM2** via the `.res` `XGM2` line (rescomp → `xgm2tool`).
4. **Verify by ear and by capture** — a headless emulator can dump PCM audio
   for a spectral fingerprint check if you want automated regression (see
   testing.md), but at minimum listen in BlastEm.

Expect to hand-tune: automatic OPL→YM2612 or sample→FM conversions rarely sound
right first pass. Budget ROM for music; a few minutes of FM is cheap, but PCM
streams are not.

## Four more routes the ports took

Pick by what the source gives you and how faithful it must be:

1. **Render the original synth offline to PCM** (Catacomb Abyss / 3-D). AdLib
   effects are played through an OPL emulator (Nuked-OPL3 in OPL2 mode) driven
   by the same register sequence the original sound code issues, written to WAV,
   declared as `WAV ... XGM2 13300`, and triggered with the original's sound
   priorities kept in game code. PC-speaker effects become rendered square waves.
   Catacomb 3-D renders its only IMF music track the same way into a seamlessly
   looping PCM track. Bit-exact sound, at a ROM cost (13.3 KB/s); a looping
   track restarts from the top after a pause rather than resuming.
2. **Convert sequence data to the YM2612** (Wolfenstein 3D). The original IMF
   (OPL register stream) music is translated into YM2612 tracks on 5 FM
   channels plus PSG, while digitized effects stay PCM. Far cheaper in ROM than
   route 1; instruments need mapping by ear.
3. **Write a small 68000-side sequencer** (Punch-Out Pizza Tower Edition). With
   no Z80 driver involved, a per-frame `audio_update()` from the main loop
   parses MML-like strings per channel and drives the chips directly:
   `YM2612_writeReg(port, reg, value)` for FM patches/notes/key-on
   (register `0x28`) and `PSG_setTone` / `PSG_setEnvelope` / `PSG_setNoise`.
   Music takes 5 FM + 2 PSG tone + noise; SFX borrow FM channel 5 and PSG tone 2.
   Full control and an all-original soundtrack, paid for in 68000 time every
   frame — budget and profile it like any other per-frame work.
4. **Mine the data for real per-event sounds** (Tuxemon MD). A converter reads
   which clip each move/monster references in the game's YAML, dedupes them
   (136 techniques → 23 distinct technique sounds; 51 clips in all), converts
   with `ffmpeg` to small mono WAVs, and generates both the `.res` lines and a
   `sfx_id → {data, length}` table, so playback is one call per event. (Its
   clips are declared at 8000 Hz — per the rate rule above, use 6650+halfRate
   or 13300 instead.)

Whichever route: **prove the sound plays** in the headless test by comparing
audio energy at quiet vs. triggering frames (`audiostat@` in the harness —
Tuxemon MD saw 100–300× jumps right after its cries and menu sounds), and
capture a WAV (`HARNESS_WAV`) when pitch or timing matters.

## The starvation gotcha (why sound stutters)

The Z80 driver needs the bus/timing to service the YM2612 every tick. If the
68000 hogs the bus with **huge DMA transfers during active display** (e.g. a
full-plane tilemap swap or the affine-road stream) it can starve the sound
driver and you'll hear dropouts, especially on PCM. Mitigations: keep per-frame
DMA within budget, do big transfers in vblank, split streams across frames, and
avoid holding the Z80 bus. If music stutters exactly when a heavy visual event
fires, this is why.

## Alternative drivers

If SGDK's drivers don't fit (you need FM+PSG SFX with pitch, or a driver that
never touches the cartridge bus), consider **MDSDRV**, **Echo**, or
**MiniMusic** (linked from SGDK's readme). These change how you author and
trigger audio but can solve specific SFX/streaming constraints.
