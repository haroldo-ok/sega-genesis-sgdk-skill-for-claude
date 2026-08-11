#!/usr/bin/env python3
"""Assert a Genesis frame dump is NOT a black screen.

Reads PPM (P6) snapshots produced by libretro_harness.c and checks:
  * lit:       a healthy fraction of pixels are non-black
  * colourful: more than a few distinct colours (catches palette-never-loaded,
               where every pixel maps to index 0 = black)
  * advanced:  the last frame differs from the first (the game moved on)

Usage: check_frames.py FIRST.ppm LAST.ppm
Exit non-zero (with a reason) on failure so CI fails loudly.
"""
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        sys.exit(f"FAIL: {path} is not a P6 PPM")
    # parse header: P6 <w> <h> <maxval> then raw RGB bytes
    idx, fields = 2, []
    while len(fields) < 3:
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if idx < len(data) and data[idx:idx + 1] == b"#":     # comment line
            while idx < len(data) and data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n":
            idx += 1
        fields.append(int(data[start:idx]))
    idx += 1  # single whitespace after maxval
    w, h, _maxval = fields
    return w, h, data[idx:idx + w * h * 3]


def stats(px):
    n = len(px) // 3
    nonblack, colours = 0, set()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        if r > 8 or g > 8 or b > 8:
            nonblack += 1
        colours.add((r >> 3, g >> 3, b >> 3))   # quantize to Genesis-ish depth
    return n, nonblack, len(colours)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: check_frames.py FIRST.ppm LAST.ppm")
    first, last = sys.argv[1], sys.argv[2]
    w0, h0, p0 = read_ppm(first)
    w1, h1, p1 = read_ppm(last)

    n, nonblack, colours = stats(p1)
    lit_ratio = nonblack / n if n else 0.0
    print(f"last frame {w1}x{h1}: lit={lit_ratio:.2%} colours={colours}")

    if lit_ratio < 0.02:
        sys.exit(f"FAIL: last frame is essentially black (lit={lit_ratio:.2%})")
    if colours < 4:
        sys.exit(f"FAIL: too few colours ({colours}) — palette likely not loaded")
    if (w0, h0) == (w1, h1) and p0 == p1:
        sys.exit("FAIL: last frame identical to first — game did not advance")

    print("PASS: frame is lit, colourful, and advanced")


if __name__ == "__main__":
    main()
