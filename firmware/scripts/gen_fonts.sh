#!/usr/bin/env bash
# Regenerates src/ui/fonts/*.c from fonts_src/. Dev-time only, not part of the
# PlatformIO build -- the generated .c files are checked into the repo.
#
# Requires: node/npm (for `npx lv_font_conv`), and FontForge + its Python
# bindings for the synthetic-bold step (`apt install fontforge
# python3-fontforge`; the fontforge module is only importable from the
# system cpython build it was compiled against, e.g. /usr/bin/python3.12,
# not necessarily whatever `python3` resolves to on PATH).
set -euo pipefail
cd "$(dirname "$0")/.."

# Step 1: only the Regular weight of DIN Next LT Pro was available. Synthesize
# a Bold weight via FontForge outline emboldening before handing off to
# lv_font_conv, rather than shipping Regular-weight glyphs for a design that
# calls for bold throughout.
/usr/bin/python3.12 scripts/synth_bold.py

BOLD=fonts_src/DIN_Next_LT_Pro_Bold_Synthetic.otf

gen() {
  local name="$1" size="$2" symbols="$3"
  npx --yes lv_font_conv --font "$BOLD" --size "$size" --bpp 4 --format lvgl \
    --symbols "$symbols" -o "src/ui/fonts/${name}.c" --lv-font-name "$name" --no-kerning
}

# One font per distinct pixel size in the design spec, each subsetted to only
# the glyphs that size is ever used to render (keeps flash usage down --
# e.g. the 120px speed font only needs digits).
gen dinnext_120_speed   120 "0123456789"        # speed number
gen dinnext_40_gear      40 "PRNDB"              # gear letter (P/R/N/D/B)
gen dinnext_30_units     30 "KM/H"                # "KM/H" unit label
gen dinnext_26_battery   26 "0123456789"          # HV battery SOC numeric
gen dinnext_28_ev        28 "EV"                  # "EV" label
gen dinnext_26_rpm       26 "RPM0123456789:°C "   # RPM label+value row (centered group); also left-slot temp/clock
gen dinnext_14_chgpwr    14 "CHGPWR0123456789% " # "CHG"/"PWR" caption above the power gauge
gen dinnext_13_pct       13 "%"                   # "%" suffix
gen dinnext_24_label     24 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./% " # energy-flow labels + efficiency-screen numbers/units
gen dinnext_28_stat      28 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./% " # efficiency-screen headline + tile values
gen dinnext_40_accel_time  40 "0123456789."  # 0-50/0-100 timer: elapsed seconds, e.g. "6.55" (replaces the gear letter while shown)
gen dinnext_40_accel_label 40 "0123456789-"  # 0-50/0-100 timer: "0-50"/"0-100" threshold caption, below the seconds -- same size as the gear letter, was 15px and illegible

echo "done -- regenerated src/ui/fonts/*.c"
