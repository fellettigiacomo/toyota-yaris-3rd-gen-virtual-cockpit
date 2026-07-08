#pragma once

#include <lvgl.h>

// Design tokens straight from the Cockpit.dc.html handoff spec.
namespace Colors {

const lv_color_t kBg          = LV_COLOR_MAKE(0x0a, 0x0b, 0x0d);
const lv_color_t kText        = LV_COLOR_MAKE(0xea, 0xf1, 0xf5);
const lv_color_t kAccentCyan  = LV_COLOR_MAKE(0x00, 0xe5, 0xff);
const lv_color_t kChgGreen    = LV_COLOR_MAKE(0x35, 0xd9, 0x4b);
const lv_color_t kPwrWhite    = LV_COLOR_MAKE(0xea, 0xf1, 0xf5);
const lv_color_t kDivider     = LV_COLOR_MAKE(0x00, 0x00, 0x00);
const lv_color_t kBatteryBlue = LV_COLOR_MAKE(0x3a, 0xa0, 0xff);
const lv_color_t kEvGreen     = LV_COLOR_MAKE(0x3d, 0xdc, 0x84);
const lv_color_t kBarTrack    = LV_COLOR_MAKE(0x1a, 0x1b, 0x1d); // approximates rgba(255,255,255,.06) on this bg
const lv_color_t kTickDim     = LV_COLOR_MAKE(0x4d, 0x4f, 0x52); // approximates rgba(255,255,255,.3)
const lv_color_t kMutedText   = LV_COLOR_MAKE(0x8f, 0x9c, 0xa3); // approximates rgba(255,255,255,.5)

} // namespace Colors
