#!/usr/bin/env python3
"""Generate a synthetic Bold weight from the user-provided DIN Next LT Pro
Regular OTF, since only the Regular weight file was available.

Only the Regular weight was supplied for this project (see the plan doc's
Fonts section) -- the cluster design calls for bold weight throughout, so
this script emboldens outlines via FontForge's changeWeight() (the same
technique font editors use for synthetic bold) rather than faking it at the
LVGL bitmap level. If the real DIN Next LT Pro Bold weight file becomes
available later, prefer that over this synthetic version -- just point
gen_fonts.sh at it instead and drop this step.

Run with the system FontForge-linked interpreter (its python module is only
built for a specific cpython ABI, not whichever `python3` happens to be
first on PATH):

    /usr/bin/python3.12 scripts/synth_bold.py

Requires: apt install fontforge python3-fontforge
"""
import sys

import fontforge

INPUT = "fonts_src/DIN_Next_LT_Pro_Regular.otf"
OUTPUT = "fonts_src/DIN_Next_LT_Pro_Bold_Synthetic.otf"

# Emboldening strength, in font design units (this font uses a 1000-unit em).
# Tuned by inspection: at 36 units, a sample digit's bounding box widened by
# ~18 units per side (~4-5% per side on typical glyph widths), which reads as
# a clear Regular->Bold jump without going as heavy as a "Black" weight. This
# is a judgment call, not a measured match to a real DIN Next LT Pro Bold --
# revisit by eye once rendered on real hardware (see plan doc).
STROKE_WIDTH = 36

# Only the glyphs this UI actually uses -- emboldening FontForge's full ~66k
# glyph set (this OTF is a large Pro font with extensive language coverage)
# would be needlessly slow and is irrelevant since lv_font_conv subsets by
# character anyway.
USED_CHARS = "0123456789PRNDBKMHEVCGW/:°%"


def main():
    font = fontforge.open(INPUT)

    font.selection.none()
    for ch in USED_CHARS:
        font.selection.select(("more", "unicode"), ord(ch))

    count = 0
    for glyph in font.selection.byGlyphs:
        glyph.changeWeight(STROKE_WIDTH, "auto", 0, 0, "squish", True)
        glyph.removeOverlap()
        glyph.correctDirection()
        count += 1

    if count != len(USED_CHARS):
        print(f"WARNING: expected {len(USED_CHARS)} glyphs, emboldened {count}",
              file=sys.stderr)

    font.generate(OUTPUT)
    print(f"wrote {OUTPUT} ({count} glyphs emboldened)")


if __name__ == "__main__":
    main()
