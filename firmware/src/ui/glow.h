#pragma once

#include <lvgl.h>

// The blue glow rising from the bottom edge, behind every screen. LVGL 8.3
// has no radial gradients, so it is a full-screen RGB565 image computed once
// (ordered-dithered, LV_DITHER_GRADIENT is off) and shared by every user.
namespace Glow {

// Adds the glow to `parent` as a full-screen image at (0, 0). Create it before
// anything that should be drawn over it.
lv_obj_t *create(lv_obj_t *parent);

} // namespace Glow
