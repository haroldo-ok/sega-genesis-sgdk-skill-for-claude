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

Sound effects are 8-bit unsigned PCM played on a DAC channel through the driver:

```
WAV sfx_jump "sfx/jump.wav" XGM2 6650     # <driver> <rate Hz>; mono, 8-bit
```

```c
XGM2_playPCM(sfx_jump, sizeof(sfx_jump), SOUND_PCM_CH1);   // returns bool
// more control: priority, half-rate, loop:
XGM2_playPCMEx(sfx_jump, sizeof(sfx_jump), SOUND_PCM_CH2, 6, FALSE, FALSE);
XGM2_stopPCM(SOUND_PCM_CH1);
```

Channels `SOUND_PCM_CH1..CH4`. Keep SFX short and low-rate (≈6–13 kHz) — PCM
eats ROM and DAC time. Convert source SFX to mono 8-bit WAV at your target rate
before declaring them (Audacity/Wavosaur), or script it in your asset pipeline.

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
