# Vendored from Waveshare's official example

`axs15231b/`, `touch/`, `lcd_bl_bsp/`, `i2c_bsp.c`, `i2c_bsp.h`, and
`user_config.h` in this directory are copied verbatim (not hand-retyped) from
Waveshare's official ESP32-S3-Touch-LCD-3.49 example bundle
(`Arduino/10_LVGL_V9_Test/src/`, plus its `user_config.h`) -- the same
hardware this board's `board_pins.h` cites.

Why: Arduino_GFX's generic AXS15231B driver does not drive this panel
correctly over QSPI (see `display_driver.cpp`'s header comment for the full
explanation -- in short, this panel has no row-seek command in QSPI mode and
requires the *entire* frame to be written sequentially every refresh).
Waveshare's own `esp_lcd`-based driver handles this correctly and is proven
to work on real hardware; porting it byte-for-byte (rather than
re-implementing it) avoids re-introducing the same class of bug.

`display_driver.cpp` is this project's own code, gluing the vendored panel
driver to LVGL and to `cockpit_ui`/`can_decoder`. Touch
(`touch/esp_lcd_touch.*`, the touch-related parts of `axs15231b/*` --
`esp_lcd_touch_new_i2c_axs15231b()` and friends -- and the I2C touch device
handle in `i2c_bsp.c`) is still not wired up or called anywhere in this
project: `../touch_nav.cpp` talks to the same AXS15231B touch die directly
over plain Arduino `Wire`, replicating the vendored driver's read protocol
byte-for-byte (read, not called -- see that file's header comment for why:
mainly to avoid depending on the exact `esp_lcd_panel_io_i2c` API shape of
whatever ESP-IDF minor version this Arduino core pins, which has changed
across IDF releases, unlike `Wire`). `user_config.h` is only needed to
satisfy `i2c_bsp.c` and `lcd_bl_bsp/lcd_bl_pwm_bsp.c`'s own `#include`; this
project's actual pin/task config lives in `include/board_pins.h` and
`include/app_config.h`, not here -- the two headers don't conflict (checked:
no shared macro names appear in translation units that include both).

Do not hand-edit these files piecemeal; if Waveshare updates their example,
re-copy wholesale from the new version instead of patching around drift.
