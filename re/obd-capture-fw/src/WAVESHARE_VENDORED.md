# Vendored from Waveshare's official example

`axs15231b/` and `touch/` in this directory are copied verbatim (not
hand-retyped) from Waveshare's official ESP32-S3-Touch-LCD-3.49 example
bundle (`Arduino/10_LVGL_V9_Test/src/`) -- identical to the copies in
`../../firmware/src/`, duplicated here rather than shared because each
PlatformIO project vendors its own copy of third-party code (see that
directory's own `WAVESHARE_VENDORED.md` for the full rationale).

Why: this project originally drove the display via Arduino_GFX's generic
AXS15231B driver (`Arduino_ESP32QSPI` + `Arduino_AXS15231B`, SPI2_HOST),
which produced flicker/split-screen/black-line corruption on real hardware
-- the same class of bug `../../firmware/src/display_driver.cpp`'s header
comment documents in detail: this panel has no row-seek command in QSPI
mode and requires the *entire* frame to be written sequentially every
refresh, which Arduino_GFX's partial-rectangle writes don't respect.
`display_ui.cpp` now uses the same fix as `firmware/`: Waveshare's own
`esp_lcd`-based panel driver (proven correct on real hardware), full-frame
software rotation, and chunked DMA writes -- see `display_ui.cpp`'s own
comments for this project's specifics.

`touch/esp_lcd_touch.*` is pulled in only because `axs15231b/`'s header
depends on its types (`esp_lcd_touch_new_i2c_axs15231b()` and friends);
this project doesn't call any touch API, same as `firmware/`.

Unlike `firmware/`, this project keeps `GFX Library for Arduino` as a
`lib_deps` -- not for its (buggy, on this panel) hardware panel driver, but
purely as a headless framebuffer + bitmap-font renderer (`Arduino_Canvas`,
constructed with a `nullptr` output so it never touches real hardware). See
`display_ui.cpp`'s header comment.

Do not hand-edit `axs15231b/`/`touch/` piecemeal; if Waveshare updates their
example, re-copy wholesale from the new version instead of patching around
drift (in both this directory and `firmware/src/`).
