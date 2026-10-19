#!/usr/bin/env bash
# Idempotent SGDK toolchain setup for Linux / CI.
# Safe to re-run: each step is guarded by a capability check.
#
# Routes (pick with SGDK_ROUTE=docker|marsdev|apt):
#   docker  (default) — use doragasu's native-Linux SGDK image; nothing to build.
#   marsdev           — build m68k-elf-gcc + SGDK from source into $MARSDEV.
#   apt               — Ubuntu/Debian gcc-m68k-linux-gnu + an existing SGDK tree.
#                       Works with NO Docker and NO GCC build (restricted sandbox
#                       friendly). Set GDK to an SGDK checkout first. See
#                       references/toolchain-and-build.md, and REBUILD libmd.a
#                       from source with this compiler to avoid the LTO trap.
#
# After running, `echo $GDK` should point at a valid SGDK, and
# `m68k-elf-gcc --version` and `java -version` should both work.
set -euo pipefail

ROUTE="${SGDK_ROUTE:-docker}"

have() { command -v "$1" >/dev/null 2>&1; }

echo ">> SGDK setup route: $ROUTE"

# Java is required for rescomp/xgmtool regardless of route.
if ! have java; then
  echo ">> Installing a JRE (needed by rescomp)…"
  if have apt-get; then sudo apt-get update && sudo apt-get install -y default-jre-headless
  elif have dnf;   then sudo dnf install -y java-latest-openjdk-headless
  else echo "!! Install a Java JRE 8+ manually."; fi
fi

case "$ROUTE" in
  docker)
    if ! have docker; then
      echo "!! Docker not found. Install Docker, or use SGDK_ROUTE=marsdev."; exit 1
    fi
    echo ">> Pulling doragasu SGDK image…"
    docker pull doragasu/sgdk
    cat <<'EOF'

>> Docker route ready. Build your project with:

     docker run --rm -v "$PWD":/m68k -w /m68k doragasu/sgdk

   (The image provides m68k-elf-gcc, SGDK, Java, and sets GDK internally.)
   For host/emulator tests you still need a native cc + git on the host.
EOF
    ;;

  marsdev)
    : "${MARSDEV:=$HOME/mars}"
    export MARSDEV
    if have m68k-elf-gcc && [ -f "$MARSDEV/m68k/sgdk/makefile.gen" ]; then
      echo ">> MarsDev already present at $MARSDEV."
    else
      echo ">> Building MarsDev (this compiles GCC — slow; cache \$MARSDEV in CI)…"
      if have apt-get; then
        sudo apt-get update
        sudo apt-get install -y build-essential git wget texinfo \
          libgmp-dev libmpfr-dev libmpc-dev default-jre-headless zip unzip
      fi
      [ -d "$MARSDEV/.git" ] || git clone --depth 1 https://github.com/andwn/marsdev.git "$MARSDEV"
      make -C "$MARSDEV" m68k-toolchain
      make -C "$MARSDEV" sgdk
    fi
    echo ">> Add to your shell:  export MARSDEV=$MARSDEV; export GDK=\$MARSDEV/m68k/sgdk; export PATH=\$MARSDEV/m68k/bin:\$PATH"
    ;;

  apt)
    # Ubuntu/Debian native m68k GNU toolchain + an existing SGDK tree.
    # Verified to build a booting, playable ROM in a restricted sandbox.
    if ! have apt-get; then
      echo "!! apt route needs apt-get (Debian/Ubuntu)."; exit 1
    fi
    echo ">> Installing gcc-m68k-linux-gnu + binutils + JRE…"
    apt-get update
    apt-get install -y gcc-m68k-linux-gnu binutils-m68k-linux-gnu \
      default-jre-headless make
    echo ">> Creating m68k-elf-* symlinks SGDK expects…"
    mkdir -p /opt/bin
    for t in gcc as ld objcopy nm ar ranlib objdump strip cpp size addr2line; do
      src="$(command -v m68k-linux-gnu-$t || true)"
      [ -n "$src" ] && ln -sf "$src" /opt/bin/m68k-elf-$t
    done
    export PATH=/opt/bin:$PATH
    m68k-elf-gcc --version | head -1

    if [ -n "${GDK:-}" ] && [ -f "$GDK/makefile.gen" ]; then
      echo ">> Building SGDK Z80 tools + libmd.a from source (avoids LTO trap)…"
      ( cd "$GDK/tools/sjasm/src" && make && cp sjasm "$GDK/bin/sjasm" ) || true
      ( cd "$GDK/tools/bintos" && gcc -O2 -o bintos src/*.c && cp bintos "$GDK/bin/bintos" ) || true
      for t in as ld nm ar objcopy ranlib; do ln -sf /opt/bin/m68k-elf-$t "$GDK/bin/$t"; done
      # Clean first: makelib.gen does not track header dependencies, so after
      # any edit under $GDK/inc (e.g. shrinking the DMA defaults in dma.h) an
      # incremental build silently keeps the old objects.
      rm -rf "$GDK/out/release"
      ( cd "$GDK" && PATH="/opt/bin:$GDK/bin:$PATH" make -f makelib.gen PREFIX=m68k-elf- release )
      echo ">> libmd.a rebuilt with this compiler. Link WITH lto (no -fno-lto)."
    else
      echo "!! Set GDK to an SGDK checkout, then re-run to rebuild libmd.a."
      echo "   (Linking the shipped libmd.a with a different gcc hits the LTO trap.)"
    fi
    echo ">> Build with makefile.gen (make GDK=\$GDK) or the manual pipeline in the toolchain doc."
    ;;

  *) echo "!! Unknown SGDK_ROUTE '$ROUTE' (use docker|marsdev|apt)"; exit 1 ;;
esac

echo ">> Done."
