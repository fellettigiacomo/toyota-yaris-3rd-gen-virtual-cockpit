#include "bar_gauge.h"
#include "colors.h"
#include "fonts/fonts.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BarGauge {

namespace {

constexpr int16_t kBevelPx = 10; // matches the design's ~10px angled corner cut

// Row inset of the beveled (outer) edge: 0 at the top, kBevelPx at the bottom.
inline int rowInset(int y, int h) {
    if (h <= 1) return 0;
    return (kBevelPx * y) / (h - 1);
}

} // namespace

void create(Handle *hd, lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
            bool isChg, const char *labelPrefix) {
    hd->w = w;
    hd->h = h;
    hd->isChg = isChg;
    hd->buf = static_cast<lv_color_t *>(malloc(static_cast<size_t>(w) * h * sizeof(lv_color_t)));
    std::strncpy(hd->labelPrefix, labelPrefix, sizeof(hd->labelPrefix) - 1);
    hd->labelPrefix[sizeof(hd->labelPrefix) - 1] = '\0';

    hd->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(hd->canvas, hd->buf, w, h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(hd->canvas, x, y);

    // Label sits in a (w-6)-wide box, text-aligned toward the divider side
    // and inset 6px from it, vertically centered within the bar's height.
    // Text itself ("<prefix> <pct>%") is set by setFillPct(), called below.
    hd->label = lv_label_create(parent);
    lv_obj_set_style_text_font(hd->label, &dinnext_14_chgpwr, 0);
    lv_obj_set_style_text_letter_space(hd->label, 1, 0);
    lv_obj_set_style_bg_opa(hd->label, LV_OPA_TRANSP, 0);
    lv_obj_set_width(hd->label, w - 6);
    if (isChg) {
        // CHG: divider is the canvas's right edge -- right-align text, box
        // flush to the canvas's own left edge so the 6px gap lands on the right.
        lv_obj_set_style_text_align(hd->label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(hd->label, x, y + 1);
    } else {
        // PWR: divider is the canvas's left edge -- left-align text, box
        // shifted 6px in from that edge.
        lv_obj_set_style_text_align(hd->label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(hd->label, x + 6, y + 1);
    }

    setFillPct(hd, 0.0f); // paint the initial empty state now, not a garbage buffer
}

void setFillPct(Handle *hd, float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    if (std::fabs(pct - hd->lastPct) < 0.5f && hd->lastPct >= 0.0f) {
        return; // skip redundant redraw, value hasn't meaningfully changed
    }
    hd->lastPct = pct;

    int w = hd->w;
    int h = hd->h;
    int fillPx = static_cast<int>((pct / 100.0f) * w + 0.5f);

    lv_color_t fillColor = hd->isChg ? Colors::kChgGreen : Colors::kPwrWhite;

    for (int y = 0; y < h; y++) {
        int inset = rowInset(y, h);
        int validStart, validEnd; // [validStart, validEnd) is inside the trapezoid this row
        if (hd->isChg) {
            validStart = inset;
            validEnd = w;
        } else {
            validStart = 0;
            validEnd = w - inset;
        }

        for (int x = 0; x < w; x++) {
            lv_color_t px;
            if (x < validStart || x >= validEnd) {
                px = Colors::kBg; // outside the beveled trapezoid -- matches screen background
            } else if (hd->isChg) {
                // Fill anchored at the right/inner edge, growing left/outward.
                int fillStart = w - fillPx;
                px = (x >= fillStart) ? fillColor : Colors::kBarTrack;
            } else {
                // Fill anchored at the left/inner edge, growing right/outward.
                px = (x < fillPx) ? fillColor : Colors::kBarTrack;
            }
            lv_canvas_set_px_color(hd->canvas, x, y, px);
        }
    }

    char text[16];
    std::snprintf(text, sizeof(text), "%s %d%%", hd->labelPrefix, static_cast<int>(pct + 0.5f));
    lv_label_set_text(hd->label, text);

    // Label color flips dark once the fill likely passes under it (>5%),
    // matching the spec's contrast rule.
    lv_color_t textColor = (pct > 5.0f) ? Colors::kBg : lv_color_white();
    lv_obj_set_style_text_color(hd->label, textColor, 0);
}

} // namespace BarGauge
