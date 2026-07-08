#pragma once

#include <lvgl.h>

// The CHG/PWR power-flow bar. Each side (CHG=left, PWR=right) is one of
// these, sharing the same trapezoid-rasterization logic mirrored left/right.
//
// Verified against Cockpit.dc.html's actual clip-path geometry (not the
// project's own README prose summary, which describes the bevel as being on
// the "inner" edge -- tracing the polygon() point order shows the angled
// cut is actually on each side's OUTER edge (away from the center divider),
// with the inner edge (facing the divider) straight/full-height. The fill
// itself is anchored at the inner/divider edge and grows outward as its
// value increases (both halves grow away from the zero-point at center).
namespace BarGauge {

struct Handle {
    lv_obj_t *canvas = nullptr;
    lv_obj_t *label = nullptr;
    lv_color_t *buf = nullptr;
    int16_t w = 0;
    int16_t h = 0;
    bool isChg = false; // true=CHG (left, fill anchored right/inner, bevel on left/outer)
                         // false=PWR (right, fill anchored left/inner, bevel on right/outer)
    float lastPct = -1.0f; // -1 forces the first draw
};

// Creates the canvas + overlaid label, parented to `parent`, at (x,y,w,h).
void create(Handle *h, lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
            bool isChg, const char *labelText);

// pct in 0..100. Re-rasterizes only if pct changed by a visible amount since
// the last call (cheap early-out, since the value is often static).
void setFillPct(Handle *h, float pct);

} // namespace BarGauge
