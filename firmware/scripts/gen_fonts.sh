#!/usr/bin/env bash
# Regenerates src/ui/fonts/*.c from fonts_src/. Dev-time only, not part of the
# PlatformIO build -- the generated .c files are checked into the repo.
#
# Requires: node/npm (for `npx lv_font_conv`).
set -euo pipefail
cd "$(dirname "$0")/.."

MEDIUM=fonts_src/SpaceGrotesk-Medium.ttf
SEMIBOLD=fonts_src/SpaceGrotesk-SemiBold.ttf
BOLD=fonts_src/SpaceGrotesk-Bold.ttf

# --no-compress: with full_refresh every glyph on screen is drawn again each
# frame, and decompressing RLE glyphs was about a third of a frame's CPU time.
gen() {
  local font="$1" name="$2" size="$3" symbols="$4"
  npx --yes lv_font_conv --font "$font" --size "$size" --bpp 4 --format lvgl \
    --symbols "$symbols" -o "src/ui/fonts/${name}.c" --lv-font-name "$name" --no-kerning --no-compress
}

CAPS=ABCDEFGHIJKLMNOPQRSTUVWXYZ

# One font per pixel size and role, each subsetted to only the glyphs that
# role ever renders (keeps flash usage down -- e.g. the speed fonts only need
# digits). Sizes keep the cap and digit heights the layout was drawn around;
# the two speed sizes are capped by width instead, Space Grotesk's digits
# being wide.
gen "$BOLD"     spacegrotesk_106_speed       106 "0123456789"            # speed number (cockpit)
gen "$BOLD"     spacegrotesk_96_speed         96 "0123456789"            # speed in the compact centre column (every other screen)
gen "$SEMIBOLD" spacegrotesk_39_gear          39 "PRNDB"                 # gear letter (P/R/N/D/B)
gen "$SEMIBOLD" spacegrotesk_29_units         29 "KM/H"                  # "KM/H" unit label
gen "$BOLD"     spacegrotesk_25_battery       25 "0123456789"            # HV battery SOC numeric
gen "$SEMIBOLD" spacegrotesk_27_ev            27 "EV"                    # "EV" label
gen "$SEMIBOLD" spacegrotesk_25_rpm           25 "RPM0123456789:°C ANDUSBEV·" # RPM label+value row; "CAN DUMP USB · EV" while dumping
gen "$SEMIBOLD" spacegrotesk_14_chgpwr        14 "CHGPWR0123456789% "    # "CHG"/"PWR" caption above the power gauge
gen "$SEMIBOLD" spacegrotesk_13_pct           13 "%A"                    # "%" suffix; "A" when the power readout shows battery amps
gen "$BOLD"     spacegrotesk_23_label         23 "${CAPS}0123456789./% "  # trip distance, G-meter status words
gen "$BOLD"     spacegrotesk_27_stat          27 "${CAPS}0123456789./%- " # efficiency and trip-summary values
gen "$BOLD"     spacegrotesk_39_accel_time    39 "0123456789."           # 0-50/0-100 timer: elapsed seconds (replaces the gear letter while shown)
gen "$SEMIBOLD" spacegrotesk_39_accel_label   39 "0123456789-"           # 0-50/0-100 timer: "0-50"/"0-100" threshold caption, below the seconds
gen "$SEMIBOLD" spacegrotesk_20_rpm           20 "RPM0123456789 EV"      # RPM/EV row in the compact centre column
gen "$SEMIBOLD" spacegrotesk_18_tag           18 "$CAPS"                 # energy-flow source names (ENGINE / MOTOR / CHARGE)
gen "$BOLD"     spacegrotesk_35_value         35 "0123456789.-"          # G-meter headline value, energy-flow readouts (L/100, amps)
gen "$BOLD"     spacegrotesk_19_value         19 "0123456789. G"         # G-meter peaks and the "G" unit
gen "$MEDIUM"   spacegrotesk_12_label         12 "${CAPS}0123456789/.%- " # muted captions and units on the secondary screens
gen "$MEDIUM"   spacegrotesk_18_unit          18 "L/100A"                # energy-flow units
gen "$MEDIUM"   spacegrotesk_14_caption       14 "${CAPS}0123456789/.%- " # trip-summary captions

echo "done -- regenerated src/ui/fonts/*.c"
