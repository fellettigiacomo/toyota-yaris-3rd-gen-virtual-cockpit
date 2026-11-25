#pragma once

#include <lvgl.h>

// Design tokens straight from the Cockpit.dc.html handoff spec.
//
// Two themes, picked at build time: the default dark one, and a light one for
// daytime (-DCOCKPIT_THEME_LIGHT). Every screen reads its colours from here,
// so switching theme is a rebuild, not a code change. The *Rgb arrays are the
// same colours as 8-bit channels for glow.cpp, which computes its gradient
// before RGB565 truncation.
namespace Colors {

#ifndef COCKPIT_THEME_LIGHT

const lv_color_t kBg          = LV_COLOR_MAKE(0x0a, 0x0b, 0x0d);
const lv_color_t kText        = LV_COLOR_MAKE(0xea, 0xf1, 0xf5);
const lv_color_t kTextBright  = LV_COLOR_MAKE(0xff, 0xff, 0xff); // RPM row: a touch above kText
const lv_color_t kAccentCyan  = LV_COLOR_MAKE(0x00, 0xe5, 0xff);
const lv_color_t kChgGreen    = LV_COLOR_MAKE(0x35, 0xd9, 0x4b); // CHG side of the power gauge, regen flow arrows
const lv_color_t kPwrWhite    = LV_COLOR_MAKE(0xea, 0xf1, 0xf5); // PWR side of the power gauge
const lv_color_t kPwrRed      = LV_COLOR_MAKE(0xe0, 0x3a, 0x2f); // PWR side past its redline threshold
const lv_color_t kBatteryBlue = LV_COLOR_MAKE(0x3a, 0xa0, 0xff);
const lv_color_t kEvGreen     = LV_COLOR_MAKE(0x3d, 0xdc, 0x84);
const lv_color_t kBarTrack    = LV_COLOR_MAKE(0x1a, 0x1b, 0x1d); // approximates rgba(255,255,255,.06) on this bg
const lv_color_t kTickDim     = LV_COLOR_MAKE(0x4d, 0x4f, 0x52); // approximates rgba(255,255,255,.3)
const lv_color_t kMutedText   = LV_COLOR_MAKE(0x8f, 0x9c, 0xa3); // approximates rgba(255,255,255,.5)

const lv_color_t kEngineRed = LV_COLOR_MAKE(0xe0, 0x3a, 0x2f); // ICE-driven segments/icon
const lv_color_t kEngineText = LV_COLOR_MAKE(0xff, 0x6b, 0x5f); // engine name + particles: kEngineRed lifted for legibility on the glow
const lv_color_t kFlowOff   = LV_COLOR_MAKE(0x2a, 0x2c, 0x30); // inactive flow-arrow outline

const uint8_t kBgRgb[3]   = {0x0a, 0x0b, 0x0d};
const uint8_t kGlowRgb[3] = {0x1f, 0xb0, 0xff};
constexpr float kGlowStrength = 0.40f;

// Recolour markup for the CAN-dump row (lv_label recolor syntax).
#define COCKPIT_HEX_ACCENT "#00e5ff"
#define COCKPIT_HEX_EV     "#3ddc84"

#else // COCKPIT_THEME_LIGHT

// Off-white rather than white: less glare from a backlit panel in daylight.
// Accents are the dark-theme hues pulled down until they hold ~4.5:1 or more
// against the background; the white PWR bar becomes graphite.
const lv_color_t kBg          = LV_COLOR_MAKE(0xf1, 0xf3, 0xf5);
const lv_color_t kText        = LV_COLOR_MAKE(0x10, 0x16, 0x1b);
const lv_color_t kTextBright  = LV_COLOR_MAKE(0x10, 0x16, 0x1b);
const lv_color_t kAccentCyan  = LV_COLOR_MAKE(0x00, 0x7f, 0x9e);
const lv_color_t kChgGreen    = LV_COLOR_MAKE(0x1a, 0x93, 0x36);
const lv_color_t kPwrWhite    = LV_COLOR_MAKE(0x26, 0x2d, 0x33);
const lv_color_t kPwrRed      = LV_COLOR_MAKE(0xcf, 0x2e, 0x24);
const lv_color_t kBatteryBlue = LV_COLOR_MAKE(0x1d, 0x6c, 0xe0);
const lv_color_t kEvGreen     = LV_COLOR_MAKE(0x0e, 0x96, 0x55);
const lv_color_t kBarTrack    = LV_COLOR_MAKE(0xdb, 0xe0, 0xe4);
const lv_color_t kTickDim     = LV_COLOR_MAKE(0xa4, 0xad, 0xb5);
const lv_color_t kMutedText   = LV_COLOR_MAKE(0x63, 0x70, 0x7a);

const lv_color_t kEngineRed  = LV_COLOR_MAKE(0xcf, 0x2e, 0x24);
const lv_color_t kEngineText = LV_COLOR_MAKE(0xc4, 0x32, 0x27);
const lv_color_t kFlowOff    = LV_COLOR_MAKE(0xd2, 0xd8, 0xdd);

const uint8_t kBgRgb[3]   = {0xf1, 0xf3, 0xf5};
const uint8_t kGlowRgb[3] = {0xa8, 0xdc, 0xf4}; // a pale sky haze instead of the dark theme's blue bloom
constexpr float kGlowStrength = 0.55f;

#define COCKPIT_HEX_ACCENT "#007f9e"
#define COCKPIT_HEX_EV     "#0e9655"

#endif

// The boot logos are pre-composited over the dark background (see
// scripts/gen_boot_images.py), so the splash stays dark in both themes.
const lv_color_t kBootBg = LV_COLOR_MAKE(0x0a, 0x0b, 0x0d);

} // namespace Colors
