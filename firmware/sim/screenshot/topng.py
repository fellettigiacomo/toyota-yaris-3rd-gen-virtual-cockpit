#!/usr/bin/env python3
"""Turn screenshot.cpp's raw RGB888 dumps into PNGs.

Standard library only (zlib + struct), so there is nothing to install. The
renderer writes raw bytes rather than PNG itself to keep a zlib dependency out
of the C++ build for something Python already does.
"""
import glob
import os
import struct
import sys
import zlib

WIDTH, HEIGHT = 640, 172  # the panel's logical (post-rotation) resolution


def chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def to_png(rgb_path, png_path):
    raw = open(rgb_path, "rb").read()
    expected = WIDTH * HEIGHT * 3
    if len(raw) != expected:
        raise SystemExit(f"{rgb_path}: expected {expected} bytes, got {len(raw)}")
    # PNG wants a filter byte in front of every scanline; 0 means "none".
    scanlines = b"".join(b"\x00" + raw[y * WIDTH * 3:(y + 1) * WIDTH * 3] for y in range(HEIGHT))
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)  # 8-bit truecolour
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", header)
           + chunk(b"IDAT", zlib.compress(scanlines, 9))
           + chunk(b"IEND", b""))
    open(png_path, "wb").write(png)
    print(f"  {os.path.basename(png_path)}  ({len(png)} bytes)")


def main():
    paths = sys.argv[1:] or sorted(glob.glob("*.rgb"))
    if not paths:
        raise SystemExit("no .rgb files given or found")
    for p in paths:
        to_png(p, os.path.splitext(p)[0] + ".png")


if __name__ == "__main__":
    main()
