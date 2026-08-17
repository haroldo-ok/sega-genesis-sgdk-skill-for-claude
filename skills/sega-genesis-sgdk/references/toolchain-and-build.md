# Toolchain & build

## What SGDK needs

- **`m68k-elf-gcc`** (GCC 13.2 in SGDK 2.12) + binutils — the Motorola 68000
  cross compiler. On Windows SGDK bundles it; on Linux/macOS you provide it.
- **The SGDK tree** (library sources + prebuilt `libmd.a`/`libmd_debug.a` +
  the `bin/` tools). The environment variable **`GDK`** must point at its root.
- **Java (JRE 8+)** — `rescomp`, `xgmtool`, `xgm2tool` are Java. Without it,
  `.res` compilation fails. Verify `java -version`.

Sanity check any environment before building:

```sh
echo "$GDK" && ls "$GDK/makefile.gen" && m68k-elf-gcc --version && java -version
```

## Install routes on Linux / CI (pick one)

SGDK dropped its own Docker files in 2.10; the recommended headless routes now:

### A. doragasu docker image (fastest, native-Linux compiler)
`https://gitlab.com/doragasu/docker-sgdk`. Pull the image and build by mounting
the project — the image ships `m68k-elf-gcc`, SGDK, and Java. Typical use:

```sh
docker run --rm -v "$PWD":/m68k -w /m68k doragasu/sgdk        # builds via makefile.gen
```

This is the least-fuss option for CI: no compiler build, reproducible.

### B. MarsDev (portable, builds the toolchain from source)
`https://github.com/andwn/marsdev` builds `m68k-elf-gcc` and installs SGDK
(and can also target 32X). Set `MARSDEV=$HOME/mars` and `GDK=$MARSDEV/m68k/sgdk`.
Good when you can't use Docker. The build is slow (compiles GCC) — cache it.

### C. Ubuntu/Debian `gcc-m68k-linux-gnu` + prebuilt SGDK (no Docker, no GCC build)
**This is the route that works in a restricted sandbox** where gitlab (doragasu)
is blocked and building GCC from source (MarsDev) is too slow or its download
host is unreachable. It has been used end-to-end to produce a booting, playable
ROM — see "Manual build pipeline" below. The idea: Ubuntu ships a real m68k GNU
toolchain, and SGDK's shipped `libmd.a` is a plain `elf32-m68k` archive GNU
binutils can read, so you can skip building the library entirely.

```sh
apt-get install -y gcc-m68k-linux-gnu binutils-m68k-linux-gnu default-jre-headless make
```

Key facts that make this work (all verified):
- The **68000 instruction set is identical** whether the target triple says
  `m68k-elf` (bare metal) or `m68k-linux-gnu` (Linux ABI). With
  `-ffreestanding -nostdlib` and SGDK's `md.ld` linker script, the Linux-ABI gcc
  produces valid Genesis code. `-m68000` selects the CPU (default is 68020+).
- Ubuntu's `gcc-m68k-linux-gnu` is **GCC 13.x**, matching SGDK 2.12's GCC 13.2 —
  the closer the versions, the fewer surprises (see the LTO warning below).
- ⚠️ **Debian's `libgcc.a` is built for 68020+**, so even though it contains the
  helpers the 68000 needs (`__mulsi3`, `__udivsi3`, `__divsi3` — the 68000 has no
  32-bit multiply/divide), some use instructions the base 68000 lacks (e.g.
  `__divsi3` via 32-bit `bsr.l`), which trap as an illegal/line-F exception on
  real hardware and accurate emulators. **Link SGDK's shipped 68000-safe
  `$GDK/lib/libgcc.a` explicitly** instead of `-lgcc` — put it *after* `libmd.a`
  on the link line. (This is verified from a real port that crashed until it did.)
- SGDK's tool prefix is `m68k-elf-`. Create symlinks so its makefiles and the
  `-B$(BIN)` mechanism find your tools:
  ```sh
  mkdir -p /opt/bin
  for t in gcc as ld objcopy nm ar ranlib objdump strip cpp; do
    ln -sf "$(command -v m68k-linux-gnu-$t)" /opt/bin/m68k-elf-$t
  done
  export PATH=/opt/bin:$PATH
  ```

Provenance differs from the "blessed" toolchains but the flags, memory model,
and rescomp step are identical.

`assets/setup.sh` wraps the Docker/MarsDev routes in an idempotent script
(checks `command -v m68k-elf-gcc` first) so it's safe to re-run after a cold
start; extend it with the apt route above when that's all you have.

## ⚠️ The LTO-version trap (read this before you debug a "frozen" ROM)

**Symptom:** the ROM builds and links fine, boots, and *renders* — background
tiles even update — but **sprites never move and game state appears frozen**;
writes to RAM globals silently don't persist while direct VDP writes work.

**Cause:** SGDK's prebuilt `lib/libmd.a` (release) is built with
`-flto -ffat-lto-objects`, so it carries **LTO bytecode tied to the exact GCC
build that produced it**. If your compiler is a *different* build (even a nearby
version), the linker either rejects the bytecode outright:

```
lto1: fatal error: bytecode stream in file 'libmd.a' generated with LTO
version 13.0 instead of the expected 13.1
```

…or, if you work around that with `-fno-lto` (which pulls the fat objects'
machine code instead of re-optimizing), you can get a ROM that links but has
**broken RAM initialization / DMA-flush paths** — exactly the "renders but
frozen, RAM writes dead" symptom above. This is subtle and costs hours if you
don't know it.

**The correct fix: rebuild `libmd.a` from source with YOUR compiler**, then link
*with* LTO (no `-fno-lto`). See the next section. `-fno-lto` against a mismatched
prebuilt lib is only a stopgap and may misbehave.

## Building the SGDK library from source (the reliable path off-Windows)

Prebuilt Windows binaries include `libmd.a`, but per the LTO trap above, if your
compiler differs you should rebuild it. `makelib.gen` needs SGDK's **Z80 tools**
(`sjasm`, `bintos`) as native executables — the shipped ones are Windows `.exe`.
Build them from the sources under `$GDK/tools/` first:

```sh
cd "$GDK/tools/sjasm/src" && make && cp sjasm "$GDK/bin/sjasm"
cd "$GDK/tools/bintos"     && gcc -O2 -o bintos src/*.c && cp bintos "$GDK/bin/bintos"
```

Then build the library with your prefix, and put the m68k tools on PATH plus in
`$GDK/bin` (the recipe calls `sjasm`/`bintos` bare and gcc uses `-B$(BIN)`):

```sh
export PATH=/opt/bin:$GDK/bin:$PATH
for t in as ld nm ar objcopy ranlib; do ln -sf /opt/bin/m68k-elf-$t "$GDK/bin/$t"; done
cd "$GDK" && make -f makelib.gen PREFIX=m68k-elf- release   # -> lib/libmd.a
```

`rescomp.jar`, `sizebnd.jar`, `xgmtool` are Java and work as-is. Docker/MarsDev
images already have the library built with a matching compiler, which is why
they don't hit the LTO trap.

## Manual build pipeline (when you can't use `makefile.gen` directly)

In a restricted environment you may need to reproduce what `makefile.gen` does by
hand. This exact sequence has produced a verified ROM. Flags mirror the release
build; **link with LTO** once the library matches your compiler.

```sh
GDK=/path/to/sgdk
INCS="-Isrc -Iinc -Ires -Ibuild -I$GDK/inc -I$GDK/res -I$GDK/inc/snd"
DF="-DSGDK_GCC -m68000 -Wall -Wextra -Wno-shift-negative-value -Wno-main \
    -Wno-unused-parameter -fno-builtin -ffunction-sections -fdata-sections -fms-extensions"
CFLAGS="$INCS $DF -O3 -fno-web -fno-gcse -fomit-frame-pointer"
AFLAGS="-x assembler-with-cpp -Wa,--register-prefix-optional,--bitwise-or $INCS $DF"

# 1) resources: rescomp emits ASSEMBLY (.s), not an object. Assemble it after.
( cd res && java -jar "$GDK/bin/rescomp.jar" resources.res ../build/resources.s )
m68k-elf-gcc $AFLAGS -c build/resources.s -o build/resources.o

# 2) your C
m68k-elf-gcc $CFLAGS -c src/core/game.c -o build/game.o
m68k-elf-gcc $CFLAGS -c src/main.c      -o build/main.o

# 3) cartridge header -> raw 256-byte binary (the boot code .incbin's it)
m68k-elf-gcc $INCS $DF -c src/rom_header.c -o build/rom_header.o
m68k-elf-objcopy -O binary build/rom_header.o out/rom_header.bin

# 4) SGDK boot (sega.s .incbin's out/rom_header.bin relative to CWD)
cp "$GDK/src/boot/sega.s" build/sega.s
m68k-elf-gcc $AFLAGS -c build/sega.s -o build/sega.o

# 5) link with md.ld + library, then raw binary + pad + checksum
m68k-elf-gcc -m68000 -n -T "$GDK/md.ld" -nostdlib -Wl,--build-id=none \
  build/sega.o build/main.o build/game.o build/resources.o \
  "$GDK/lib/libmd.a" "$GDK/lib/libgcc.a" -o build/rom.out -Wl,--gc-sections
m68k-elf-objcopy -O binary build/rom.out out/rom.bin
java -jar "$GDK/bin/sizebnd.jar" out/rom.bin -sizealign 131072 -checksum
```

Non-obvious gotchas this sequence encodes:
- **rescomp output is `.s`, not `.o`** — the second positional arg is the asm
  filename. Assemble it yourself; feeding a `.o` name to rescomp then to `ld`
  gives "file format not recognized".
- **`-Wl,--build-id=none` is required.** Otherwise gcc emits a
  `.note.gnu.build-id` section at address 0 that collides with the 68000 vector
  table (`section .note.gnu.build-id LMA overlaps .text`).
- **`.data`/`.bss` VMA is `0xE0FF0000`** in `md.ld` (masks to `0x00FF0000` work
  RAM on the 24-bit bus). `_start_entry` in libmd copies `.data` and clears RAM
  before `main` — this is why a *matching* library matters (see LTO trap).
- **A 68000-safe `libgcc`** must be on the link line for the mul/div helpers —
  prefer SGDK's shipped `$GDK/lib/libgcc.a` over the Debian toolchain's `-lgcc`
  (the latter is 68020+ and can emit instructions that trap on a 68000; see the
  apt-route warning above).
- The `AFLAGS` for `.s`/`.s80`-derived asm need
  `--register-prefix-optional,--bitwise-or` or SGDK's assembly won't assemble.

## Building the SGDK library (only if you built/cloned SGDK yourself)

Prebuilt Windows binaries include `libmd.a`. If you cloned SGDK on Linux you may
need to build it once:

```sh
cd "$GDK"
make -f makelib.gen   # produces lib/libmd.a and lib/libmd_debug.a
```

Docker/MarsDev images already have this done.

## The project Makefile

An SGDK project Makefile is essentially two lines — it delegates everything to
SGDK's `makefile.gen`, which discovers `src/**/*.c`, `res/**/*.res`, etc.:

```makefile
GDK ?= /opt/sgdk
include $(GDK)/makefile.gen
```

Build targets provided by `makefile.gen`:

| target        | effect                                                        |
|---------------|---------------------------------------------------------------|
| `release`     | default; `-O3 -flto -fuse-linker-plugin`, no debug            |
| `debug`       | `-O1 -ggdb -g -DDEBUG=1`; enables asserts, KDebug/KLog output |
| `asm`         | `-O3` without LTO, emits `.lst` assembly listings             |
| `clean` / `clean-debug` / `clean-asm` | remove the matching `out/`           |

Output always lands in **`out/<type>/`** with the final ROM at **`out/rom.bin`**.
`out/rom.out` is the ELF (symbols, for debugger). `.lst`/`.map` help debugging.

Common overrides:

- `make GDK=/path/to/sgdk` — point at a specific SGDK.
- `make EXTRA_FLAGS=-DMY_DEFINE` — inject a compile define (used e.g. for
  self-test builds; see `references/testing.md`).
- `PREFIX=m68k-elf-` — cross-tool prefix if yours differs.

## Source discovery rules (important gotchas)

`makefile.gen` globs `src/`, `src/*/`, up to a few levels deep, for `*.c`,
`*.s` (68000 asm), `*.asm`, `*.s80` (Z80 asm), and `res/**/*.res`. Consequences:

- **`src/rom_header.c` is explicitly excluded** from the wildcard and linked
  specially — keep the ROM header there, don't rename it.
- Any `.c` under `src/` is compiled — stray/backup files get built too. Keep the
  tree clean or the build breaks on unrelated files.
- A `.res` anywhere under `res/` is compiled by rescomp into a `.o` and a
  generated `.h` you `#include` (same basename). Rename an asset and you must
  update the `.res` line **and** clean, or a stale generated symbol lingers.

## Emulators for local run/debug

- **Genesis Plus GX** (libretro) — used headless in tests (see testing.md).
- **BlastEm** — accurate, good debugger; great for manual verification.
- **Gens KMod / Regen** — classic debuggers; the VSCode *Genesis-Code*
  extension (`zerasul.genesis-code`) can launch one on `>Genesis: Run`.

## CI shape

A minimal GitHub Actions job: use the doragasu docker image (or cache a MarsDev
build on the release tag), `make`, then run both test scripts from
`references/testing.md`. Cache `$GDK` / `/opt/toolchains` aggressively — the
compiler is the slow part. Fail the job if `out/rom.bin` is missing, if the
headless boot is a black screen, or if a host-core assertion fails.
