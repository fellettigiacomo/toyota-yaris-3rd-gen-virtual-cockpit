#!/usr/bin/env python3
"""Generates src/ui/boot_images.c from assets/boot/*.png: the two boot-splash
logos as LVGL LV_IMG_CF_TRUE_COLOR (RGB565) images.

Dev-time only, like gen_fonts.sh -- the generated .c is checked in, the
source PNGs are not (trademarked artwork; assets/boot/ is git-ignored, so
supply your own copies there before running this).
Requires Pillow (`pip install pillow`).

Each logo is adapted for a dark cluster and then flattened onto the UI's own
background colour (Colors::kBg), so the firmware can draw it as a plain
opaque RGB565 bitmap -- 2 bytes/px, no per-pixel alpha blending at runtime.
The boot overlay behind it is the same kBg, so the rectangle is invisible.

  toyota.png    Transparent background, red badge + BLACK wordmark. Black on
                a black panel would disappear, so everything outside the red
                badge is redrawn in the UI's text colour, keeping its alpha
                (and with it the anti-aliasing).
  gr_yaris.png  Solid dark background. Cropped to the artwork, then the
                background (except the near-black half of the GR badge,
                which is almost the same colour) is swapped for kBg: pixels are treated as a blend
                of artwork over the original background colour and re-blended
                over kBg, so the edges of the white lettering stay smooth.

Usage: python3 scripts/gen_boot_images.py
"""

from PIL import Image

KBG = (0x0A, 0x0B, 0x0D)    # Colors::kBg
KTEXT = (0xEA, 0xF1, 0xF5)  # Colors::kText

TOYOTA_H = 132       # px on the 640x172 panel
GR_YARIS_W = 440     # letters about as tall as the TOYOTA wordmark plus its badge read together


def over(fg, a, bg):
    return tuple(round(f * a + b * (1 - a)) for f, b in zip(fg, bg))


def toyota():
    im = Image.open("assets/boot/toyota.png").convert("RGBA")
    im = im.crop(im.getbbox())
    w, h = im.size
    px = im.load()
    # Right edge of the red badge: last column of the middle row that is red.
    badge_right = max(x for x in range(w)
                      if px[x, h // 2][0] > 200 and px[x, h // 2][1] < 80 and px[x, h // 2][3] > 200)
    out = Image.new("RGB", (w, h))
    o = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            fg = (r, g, b) if x <= badge_right + 2 else KTEXT
            o[x, y] = over(fg, a / 255, KBG)
    nw = round(w * TOYOTA_H / h)
    return out.resize((nw, TOYOTA_H), Image.LANCZOS)


def gr_yaris():
    im = Image.open("assets/boot/gr_yaris.png").convert("RGB")
    bg = im.getpixel((2, 2))
    w, h = im.size
    px = im.load()

    def dist(c):
        return max(abs(c[i] - bg[i]) for i in range(3))

    # Crop to whatever differs from the flat background.
    mask = Image.new("L", (w, h))
    m = mask.load()
    for y in range(h):
        for x in range(w):
            m[x, y] = 255 if dist(px[x, y]) > 24 else 0
    box = mask.getbbox()
    box = (box[0] - 2, box[1] - 2, box[2] + 2, box[3] + 2)
    im = im.crop(box)
    w, h = im.size
    px = im.load()

    # Flood-fill the background from the border: only pixels connected to it
    # are re-blended, so the near-black half of the GR badge (enclosed by its
    # white outline) is left exactly as drawn.
    seen = bytearray(w * h)
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    region = []
    while stack:
        x, y = stack.pop()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y * w + x]:
            continue
        seen[y * w + x] = 1
        c = px[x, y]
        region.append((x, y))
        if dist(c) > 24:
            continue  # an anti-aliased edge pixel: re-blend it, but don't spread past it
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]

    # Background also shows through spots the flood can't reach (the counter
    # of the A, the gaps between the letter strokes). Right of the GR badge
    # there is nothing but white lettering on bg, so everything there is
    # re-blended too. The badge's right edge: its rightmost red pixel, plus
    # its white outline.
    badge_right = max(x for y in range(h) for x in range(w)
                      if px[x, y][0] > 180 and px[x, y][1] < 80 and px[x, y][2] < 80) + 12
    region += [(x, y) for y in range(h) for x in range(badge_right, w) if not seen[y * w + x]]

    shift = tuple(k - b for k, b in zip(KBG, bg))
    for x, y in region:
        c = px[x, y]
        # Edges here are the artwork's white over bg: coverage from luminance.
        lum, lbg = sum(c) / 3, sum(bg) / 3
        a = min(1.0, max(0.0, (lum - lbg) / (255 - lbg)))
        px[x, y] = tuple(max(0, min(255, round(c[i] + (1 - a) * shift[i]))) for i in range(3))

    nh = round(h * GR_YARIS_W / w)
    return im.resize((GR_YARIS_W, nh), Image.LANCZOS)


def to_c(name, im):
    w, h = im.size
    data = []
    raw = im.convert("RGB").tobytes()
    for r, g, b in zip(raw[0::3], raw[1::3], raw[2::3]):
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        data += [v >> 8, v & 0xFF]  # LV_COLOR_16_SWAP=1 (include/lv_conf.h): high byte first
    lines = ["    " + ", ".join("0x%02x" % v for v in data[i:i + 20]) + "," for i in range(0, len(data), 20)]
    body = "\n".join(lines)
    return f"""
static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}_map[] = {{
{body}
}};

const lv_img_dsc_t {name} = {{
    .header = {{
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = {w},
        .h = {h},
    }},
    .data_size = {w * h * 2},
    .data = {name}_map,
}};
"""


def main():
    imgs = [("boot_img_toyota", toyota()), ("boot_img_gr_yaris", gr_yaris())]
    out = """// GENERATED by scripts/gen_boot_images.py from assets/boot/*.png -- do not
// edit by hand. RGB565 (byte-swapped for LV_COLOR_16_SWAP), pre-flattened
// onto Colors::kBg.

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 1
#error "boot_images.c was generated for LV_COLOR_DEPTH 16 with LV_COLOR_16_SWAP 1 -- rerun scripts/gen_boot_images.py"
#endif
"""
    for name, im in imgs:
        out += to_c(name, im)
        print(f"{name}: {im.size[0]}x{im.size[1]}")
    with open("src/ui/boot_images.c", "w") as f:
        f.write(out)
    print("wrote src/ui/boot_images.c")


if __name__ == "__main__":
    main()
