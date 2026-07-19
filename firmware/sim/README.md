# Desktop simulator

Runs the real cluster UI (`src/ui/*`) in an SDL2 window on your Mac, fed by
the same real-drive-log replay the `pio run -e demo` firmware build uses
(`can_decoder.cpp` + `demo_log_data.cpp`, unmodified) -- no ESP32 board, no
flashing. It does **not** exercise `display_driver.cpp` or anything
ESP-IDF/hardware-specific; that stays firmware-only. The BOOT-button screen
switch (`screen_nav.cpp`) is simulated via the spacebar, and the panel's
tap-anywhere touch switch (`touch_nav.cpp`) via a left click anywhere in
the window.

## Build

```sh
brew install sdl2 cmake
cd firmware/sim
cmake -B build
cmake --build build -j
./build/sim
```

First build takes a little longer since CMake fetches LVGL 8.3.11 from
GitHub (same version `platformio.ini` pins for the firmware).

If `cmake -B build` can't find SDL2 (rare, usually only if Homebrew isn't on
your default `PATH`/`CMAKE_PREFIX_PATH`), retry with:

```sh
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix)
```

## Use

- Press SPACE, or click anywhere in the window, to cycle screens -- the
  spacebar stands in for the board's BOOT button (GPIO0), the click for a
  tap on the touch panel, both drive the same cycle on real hardware.
- The drive log loops automatically (~5.7 minutes/lap) and includes real
  EV/regen/charge/cruise/kick-down transitions, so the energy-flow arrows
  should visibly change direction and color as it plays.
- Close the window (or Cmd+Q) to quit.

## Caveat

This was written and reviewed by hand against LVGL 8.3's documented API and
this repo's own `display_driver.cpp` (for the LVGL display-driver wiring)
but was **not compiled or run** in the environment it was written in (no
SDL2/CMake toolchain, no Mac, no display available there). If `cmake`/the
build fails, that's expected to be entirely fixable -- it just hasn't been
exercised yet. The most likely rough edges, in rough order of likelihood:

- The `LV_CONF_PATH`/`LV_CONF_INCLUDE_SIMPLE` wiring in `CMakeLists.txt` was
  checked against LVGL's actual `env_support/cmake/custom.cmake` for v8.3.11,
  not assumed -- but CMake variable-scoping subtleties around
  `FetchContent_MakeAvailable` are exactly the kind of thing that looks right
  on paper and isn't.
- `SDL_PIXELFORMAT_RGB565` streaming-texture support should be universal on
  macOS's default (Metal-backed) SDL2 renderer, but hasn't been confirmed.
