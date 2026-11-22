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
REGULAR=fonts_src/DIN_Next_LT_Pro_Regular.otf

gen_from() {
  local font="$1" name="$2" size="$3" symbols="$4"
  npx --yes lv_font_conv --font "$font" --size "$size" --bpp 4 --format lvgl \
    --symbols "$symbols" -o "src/ui/fonts/${name}.c" --lv-font-name "$name" --no-kerning
}

gen() {
  gen_from "$BOLD" "$@"
}

# One font per distinct pixel size in the design spec, each subsetted to only
# the glyphs that size is ever used to render (keeps flash usage down --
# e.g. the 120px speed font only needs digits).
gen dinnext_120_speed   120 "0123456789"        # speed number
gen dinnext_40_gear      40 "PRNDB"              # gear letter (P/R/N/D/B)
gen dinnext_30_units     30 "KM/H"                # "KM/H" unit label
gen dinnext_26_battery   26 "0123456789"          # HV battery SOC numeric
gen dinnext_28_ev        28 "EV"                  # "EV" label
gen dinnext_26_rpm       26 "RPM0123456789:°C ANDUSBEV·"   # RPM label+value row (centered group); also left-slot temp/clock; "CAN DUMP USB · EV" while dumping
gen dinnext_14_chgpwr    14 "CHGPWR0123456789% " # "CHG"/"PWR" caption above the power gauge
gen dinnext_13_pct       13 "%A"                  # "%" suffix; "A" when the power readout shows battery amps
gen dinnext_24_label     24 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./% " # energy-flow labels + efficiency-screen numbers/units
gen dinnext_28_stat      28 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./%- " # efficiency-screen headline + tile values
gen dinnext_40_accel_time  40 "0123456789."  # 0-50/0-100 timer: elapsed seconds, e.g. "6.55" (replaces the gear letter while shown)
gen dinnext_40_accel_label 40 "0123456789-"  # 0-50/0-100 timer: "0-50"/"0-100" threshold caption, below the seconds -- same size as the gear letter, was 15px and illegible
gen dinnext_72_speed     72 "0123456789"          # speed in the compact centre column (every screen but the cockpit)
gen dinnext_21_small     21 "PRNDBKM/H"           # gear letter + "KM/H" in the compact centre column
gen dinnext_17_rpm       17 "RPM0123456789 EV"    # RPM/EV row in the compact centre column
gen dinnext_13_tag       13 "ABCDEFGHIJKLMNOPQRSTUVWXYZ" # energy-flow source names (ENGINE / MOTOR / CHARGE)
gen dinnext_26_value     26 "0123456789.-"        # energy-flow readouts (L/100, amps)
gen dinnext_36_value     36 "0123456789."         # G-meter headline value
gen dinnext_20_value     20 "0123456789. G"       # G-meter peaks and the "G" unit
gen_from "$REGULAR" dinnext_12_label 12 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/.%- " # muted captions and units on the secondary screens

echo "done -- regenerated src/ui/fonts/*.c"
