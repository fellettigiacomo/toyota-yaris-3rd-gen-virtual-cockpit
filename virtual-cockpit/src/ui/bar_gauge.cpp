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
    hd->x = x;
    hd->y = y;
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
    //
    // Centering uses the font's actual line_height (10px for this
    // letters+digits subset at nominal 14px -- uppercase-only/no-descender
    // subsets render shorter than their nominal point size, same as the
    // other custom fonts in this project), not the nominal 14px size --
    // using the nominal size left the text sitting 2px too high, looking
    // "stuck to the top edge" of the 16px-tall bar.
    constexpr int16_t kLabelLineHeight = 10;
    int16_t labelY = y + (h - kLabelLineHeight) / 2;
    int16_t labelBoxX = isChg ? x : static_cast<int16_t>(x + 6);
    int16_t labelBoxW = static_cast<int16_t>(w - 6);
    lv_text_align_t align = isChg ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;

    // "Unfilled" layer: the full text, always drawn, colored to read against
    // the dark not-yet-filled track (white for PWR, purple for CHG).
    hd->label = lv_label_create(parent);
    lv_obj_set_style_text_font(hd->label, &dinnext_14_chgpwr, 0);
    lv_obj_set_style_text_letter_space(hd->label, 1, 0);
    lv_obj_set_style_bg_opa(hd->label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(hd->label, Colors::kPwrWhite, 0);
    lv_obj_set_width(hd->label, labelBoxW);
    lv_obj_set_style_text_align(hd->label, align, 0);
    lv_obj_set_pos(hd->label, labelBoxX, labelY);

    // "Filled" layer: an identical dark-text duplicate, parented inside
    // clipBox so LVGL's default child-clipping-to-parent-bounds reveals it
    // only where the bright fill color already sits underneath. clipBox's
    // box is resized/repositioned every setFillPct() call to track the fill
    // boundary exactly (per-pixel, so a letter can end up half-revealed),
    // mirroring each bar's own fill direction -- see setFillPct().
    hd->clipBox = lv_obj_create(parent);
    lv_obj_remove_style_all(hd->clipBox);
    lv_obj_clear_flag(hd->clipBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(hd->clipBox, x, y);
    lv_obj_set_size(hd->clipBox, 0, h);

    hd->labelBlack = lv_label_create(hd->clipBox);
    lv_obj_set_style_text_font(hd->labelBlack, &dinnext_14_chgpwr, 0);
    lv_obj_set_style_text_letter_space(hd->labelBlack, 1, 0);
    lv_obj_set_style_bg_opa(hd->labelBlack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(hd->labelBlack, isChg ? Colors::kBg : Colors::kBg, 0);  
    lv_obj_set_width(hd->labelBlack, labelBoxW);
    lv_obj_set_style_text_align(hd->labelBlack, align, 0);
    lv_obj_set_pos(hd->labelBlack, labelBoxX - x, labelY - y);

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
    lv_label_set_text(hd->labelBlack, text);

    // Slide clipBox to cover exactly the region currently under the bright
    // fill, so the dark labelBlack layer is revealed there and nowhere else
    // -- growing in the same direction as the fill itself. PWR's fill is
    // anchored at its left/inner edge (clipBox left edge fixed at hd->x,
    // widening rightward); CHG's fill is anchored at its right/inner edge
    // (clipBox right edge fixed at hd->x + w, widening leftward).
    int16_t clipX = hd->isChg ? static_cast<int16_t>(hd->x + (w - fillPx)) : hd->x;
    lv_obj_set_pos(hd->clipBox, clipX, hd->y);
    lv_obj_set_size(hd->clipBox, static_cast<int16_t>(fillPx), h);

    constexpr int16_t kLabelLineHeight = 10;
    int16_t labelY = hd->y + (h - kLabelLineHeight) / 2;
    int16_t labelBoxX = hd->isChg ? hd->x : static_cast<int16_t>(hd->x + 6);
    lv_obj_set_pos(hd->labelBlack, static_cast<int16_t>(labelBoxX - clipX), static_cast<int16_t>(labelY - hd->y));
}

} // namespace BarGauge
