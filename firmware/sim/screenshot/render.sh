#!/usr/bin/env bash
# Rebuilds the screenshots in screenshots/ from the current UI code.
#
# Takes a couple of minutes: the scenes play out in real time, because the UI
# modules integrate against millis() (see screenshot.cpp).
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

mkdir -p screenshots
./build/screenshot screenshots
python3 topng.py screenshots/*.rgb
rm -f screenshots/*.rgb

echo "done -- screenshots/ updated"
