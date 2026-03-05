#!/usr/bin/env python3
"""Convert SLP 50731 resource icon PNGs to NDS RGB15 C header arrays.

Icons are scaled to 14px tall, width capped at 16px, centered/cropped.
"""

from PIL import Image

SLP_DIR = "/tmp/slp_50731"

ICONS = [
    (2, "icon_food"),
    (0, "icon_wood"),
    (3, "icon_gold"),
    (1, "icon_stone"),
    (4, "icon_pop"),
]

ICON_H = 14
MAX_W = 16

def to_rgb15(r, g, b):
    return ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)) | (1 << 15)

def main():
    icon_data = []
    for frame_idx, name in ICONS:
        img = Image.open(f"{SLP_DIR}/frame_{frame_idx}.png").convert("RGBA")
        w, h = img.size
        scale = ICON_H / h
        new_w = max(1, round(w * scale))
        img = img.resize((new_w, ICON_H), Image.LANCZOS)

        # Crop to MAX_W if wider, centering horizontally
        if new_w > MAX_W:
            left = (new_w - MAX_W) // 2
            img = img.crop((left, 0, left + MAX_W, ICON_H))
            new_w = MAX_W

        icon_data.append((name, new_w, img))

    print("// Auto-generated AoE2 resource icons (%dpx tall, RGB15)" % ICON_H)
    print("// Source: SLP 50731")
    print()
    print("enum { RES_ICON_H = %d };" % ICON_H)
    print()

    for name, w, _ in icon_data:
        cname = name.upper() + "_W"
        print(f"enum {{ {cname} = {w} }};")
    print()

    for name, icon_w, img in icon_data:
        pixels = []
        for y in range(ICON_H):
            for x in range(icon_w):
                r, g, b, a = img.getpixel((x, y))
                if a < 128:
                    pixels.append(0x0000)
                else:
                    pixels.append(to_rgb15(r, g, b))

        print(f"static const u16 {name}[{ICON_H * icon_w}] = {{")
        for row in range(ICON_H):
            vals = pixels[row * icon_w : (row + 1) * icon_w]
            line = "    " + ", ".join(f"0x{v:04X}" for v in vals) + ","
            print(line)
        print("};")
        print()

if __name__ == "__main__":
    main()
