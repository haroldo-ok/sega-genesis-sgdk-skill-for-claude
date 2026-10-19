#!/usr/bin/env python3
"""ramsym.py — read named variables (and a crash stack) out of a work-RAM dump.

The harness writes OUT_PREFIX<frame>.ram for every `ram@<frame>` directive:
the 64 KB of 68000 work RAM as the libretro core exposes it. Genesis Plus GX
stores that RAM **byte-swapped within each 16-bit word** (verified: the string
"Sir Lancelot" reads "iS raLcnleto" in a raw dump), so this tool swaps it back
by default; pass --raw for a core that exposes it in 68000 order.

Peek variables by symbol (sizes in bytes, default 4; values big-endian):

    python3 ramsym.py out/f600.ram out/release/symbol.txt score:4 lives:1 game:64

Scan the top of RAM (the stack) for plausible return addresses — the quickest
post-mortem for a hang or crash when you have no debugger:

    python3 ramsym.py out/f600.ram out/release/rom.out --stack [0xF000]

The symbol source is the linked ELF (SGDK 2.x's makefile.gen leaves it at
`out/release/rom.out`, or `out/debug/rom.out` for `make debug`) or a text file
of `nm -n` output — makefile.gen already writes one as `out/release/symbol.txt`.
Static variables appear too (as `b`/`d`); LTO may rename them with a suffix
like `.lto_priv.0`, so grep symbol.txt if a name is "not found".
"""
import bisect
import os
import shutil
import struct
import subprocess
import sys

NM_CANDIDATES = ("m68k-elf-nm", "m68k-linux-gnu-nm", "nm")


def load_symbols(src):
    if src.endswith((".out", ".elf")) or open(src, "rb").read(4) == b"\x7fELF":
        nm = next((t for t in NM_CANDIDATES if shutil.which(t)), None)
        if not nm:
            sys.exit("no nm found (install binutils-m68k-linux-gnu or pass nm text)")
        text = subprocess.run([nm, "-n", src], capture_output=True, text=True, check=True).stdout
    else:
        text = open(src).read()
    syms = []
    for line in text.splitlines():
        parts = line.split()          # "addr type name" (+ "file:line" with nm -l)
        if len(parts) >= 3:
            try:
                syms.append((int(parts[0], 16), parts[1], parts[2]))
            except ValueError:
                pass
    return syms


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = [a for a in argv[1:] if a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 1
    dump = bytearray(open(args[0], "rb").read())
    if "--raw" not in flags:
        dump[0::2], dump[1::2] = dump[1::2], dump[0::2]
    syms = load_symbols(args[1])
    by_name = {name: addr for addr, _, name in syms}

    if "--stack" in flags:
        lo = int(args[2], 16) if len(args) > 2 else 0xF000
        code = sorted((a, n) for a, t, n in syms if t in "tTwW")
        addrs = [a for a, _ in code]
        rom_top = max(addrs) + 0x1000 if addrs else 0x400000
        print(f"scanning RAM ${lo:04X}-$FFFF for return addresses into code (< ${rom_top:06X})")
        for off in range(lo & ~1, len(dump) - 3, 2):
            v = struct.unpack(">I", dump[off:off + 4])[0]
            if 0x200 <= v < rom_top:
                i = bisect.bisect_right(addrs, v) - 1
                if i >= 0:
                    print(f"  RAM ${off:04X}: ${v:06X}  {code[i][1]}+0x{v - code[i][0]:X}")
        return 0

    for spec in args[2:]:
        name, _, size = spec.partition(":")
        size = int(size) if size else 4
        if name not in by_name:
            print(f"{name}: not found (static? build with -g / make debug)")
            continue
        addr = by_name[name]
        off = addr & 0xFFFF                     # $E0FF0000.. / $FF0000.. -> RAM offset
        raw = bytes(dump[off:off + size])
        val = int.from_bytes(raw, "big")
        print(f"{name:24s} @${addr:08X}  {raw.hex(' ')}  = {val} (0x{val:X})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
