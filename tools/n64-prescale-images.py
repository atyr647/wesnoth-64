#!/usr/bin/env python3
"""Shrink oversized WebP images in a staged copy of the data tree for the N64.

Decoding a multi-megapixel WebP takes minutes on the console's 93 MHz CPU
(the 4096x2160 title background: ~100 s) and the screen shows 640x240. Images
above 0.7 megapixels are re-encoded to fit within about 0.6 megapixels, and
the original size is appended in a private RIFF chunk ("N64O": width, height
as big-endian u32). The console's WebP loader (src/n64/sdl2/IMG_webp_n64.c)
reads it and the engine keeps laying the image out at its original size.

    tools/n64-prescale-images.py <staged-tree>      (needs ImageMagick with WebP)
"""
import os, struct, subprocess, sys

LIMIT = 700_000      # pixels above which an image is shrunk
TARGET = 600_000     # pixels to shrink to (aspect kept)

def process(path):
    out = subprocess.run(["identify", "-format", "%w %h %n", path + "[0]"],
                         capture_output=True, text=True).stdout.split()
    if len(out) < 2:
        return False
    w, h = int(out[0]), int(out[1])
    if w * h <= LIMIT:
        return False
    k = (TARGET / (w * h)) ** 0.5
    nw, nh = max(1, round(w * k)), max(1, round(h * k))
    tmp = path + ".tmp.webp"
    subprocess.run(["convert", path, "-filter", "Lanczos", "-resize", f"{nw}x{nh}!",
                    "-quality", "90", "-define", "webp:method=4", tmp], check=True)
    data = bytearray(open(tmp, "rb").read())
    os.remove(tmp)
    if len(data) & 1:
        data += b"\0"
    data += b"N64O" + struct.pack("<I", 8) + struct.pack(">II", w, h)
    data[4:8] = struct.pack("<I", len(data) - 8)
    os.remove(path)                      # the staged tree is hard links
    open(path, "wb").write(data)
    return True

def main():
    root = sys.argv[1]
    n = 0
    for d, _, files in os.walk(root):
        for f in files:
            if f.lower().endswith(".webp"):
                n += process(os.path.join(d, f))
    print(f"prescaled {n} oversized WebP images")

if __name__ == "__main__":
    main()
