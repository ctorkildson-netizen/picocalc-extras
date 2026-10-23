#!/usr/bin/env python3
"""Draws the picker's folder icon (64x48) and writes it as icons.h (RGB565).
   python3 make_icons.py folder.png icons.h."""
import sys
from PIL import Image, ImageDraw

W, H = 64, 48
BG = (16, 20, 16)                       # the panel colour, 0x10A2
im = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(im)
d.rounded_rectangle([2, 4, 28, 14], 3, fill=(196, 150, 60))          # tab
d.rounded_rectangle([2, 9, 61, 45], 4, fill=(214, 168, 72))           # back
d.rounded_rectangle([2, 15, 61, 45], 4, fill=(240, 196, 96))          # front
d.line([(6, 19), (57, 19)], fill=(255, 222, 140), width=2)            # shine
im.save(sys.argv[1] if len(sys.argv) > 1 else "folder.png")

with open(sys.argv[2] if len(sys.argv) > 2 else "icons.h", "w") as f:
    f.write("/* icons.h -- made by make_icons.py; don't edit */\n")
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write(f"#define FOLDER_W {W}\n#define FOLDER_H {H}\n")
    f.write("static const uint16_t folder_icon[FOLDER_W * FOLDER_H] = {\n")
    px = list(im.getdata())
    for i in range(0, len(px), 12):
        f.write("\t" + ", ".join("0x%04X" % (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
                                for r, g, b in px[i:i + 12]) + ",\n")
    f.write("};\n")
