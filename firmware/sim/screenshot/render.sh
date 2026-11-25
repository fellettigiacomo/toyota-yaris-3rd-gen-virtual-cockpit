#!/usr/bin/env bash
# Rebuilds the screenshots in screenshots/ from the current UI code.
#   ./render.sh          dark theme  -> screenshots/
#   ./render.sh --light  light theme -> screenshots/light/
#
# Takes a couple of minutes: the scenes play out in real time, because the UI
# modules integrate against millis() (see screenshot.cpp).
set -euo pipefail
cd "$(dirname "$0")"

build=build out=screenshots light=OFF
if [[ "${1:-}" == "--light" ]]; then
    build=build-light out=screenshots/light light=ON
fi

cmake -B "$build" -DCMAKE_BUILD_TYPE=Release -DCOCKPIT_THEME_LIGHT="$light"
cmake --build "$build" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

mkdir -p "$out"
./"$build"/screenshot "$out"
python3 topng.py "$out"/*.rgb
rm -f "$out"/*.rgb
# The boot splash is dark in both themes, so the light run keeps no copy of it.
[[ "$light" == ON ]] && rm -f "$out"/00_boot_*.png

echo "done -- $out/ updated"
