#!/usr/bin/env python3
"""Convert an image to the LeOS gfx format: width and height (32-bit LE) then 0xAARRGGBB pixels.
usage: tools/mkimage.py OUT W H [magick input args...]   (the output is resized to exactly WxH)"""
import subprocess, struct, sys
out, w, h, *src = sys.argv[1:]
raw = subprocess.run(["magick", *src, "-filter", "Box", "-resize", f"{w}x{h}!", "-depth", "8", "bgra:-"], capture_output=True, check=True).stdout
assert len(raw) == int(w) * int(h) * 4, len(raw)
open(out, "wb").write(struct.pack("<II", int(w), int(h)) + raw)
