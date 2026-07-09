#include "flow_arrow.h"
#include "colors.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace FlowArrow {

namespace {

constexpr float kHeadLen = 14.0f;
constexpr float kHeadHalfWidth = 9.0f;
constexpr float kHighlightWidthPx = 10.0f;
constexpr float kChevronPeriodPx = 26.0f;
constexpr float kAnimSpeedPxPerSec = 70.0f;

float floatMod(float a, float m) {
    float r = std::fmod(a, m);
    if (r < 0.0f) r += m;
    return r;
}

// Rasterizes the current dir/color/phase into hd->buf. Every pixel in the
// canvas bbox is classified in (t, s) space -- t = distance projected along
// the A->B axis, s = perpendicular offset from it -- which turns "is this
// pixel inside a thick line" and "is this pixel inside the arrowhead
// triangle" into simple linear tests.
void redraw(Handle *hd) {
    float dx = hd->x2 - hd->x1;
    float dy = hd->y2 - hd->y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) len = 1.0f;
    float ux = dx / len, uy = dy / len; // unit vector A->B
    float nx = -uy, ny = ux;            // unit normal

    bool active = (hd->dir != Dir::Off);
    bool forward = (hd->dir == Dir::Forward);

    // Shaft occupies the full A-B span minus room for the arrowhead at
    // whichever end the flow currently points to; when Off there's no
    // arrowhead at all, the outline just runs the full length.
    float headLen = active ? kHeadLen : 0.0f;
    float shaftLoT = (active && !forward) ? headLen : 0.0f;
    float shaftHiT = (active && forward) ? (len - headLen) : len;

    lv_color_t baseColor = active ? lv_color_mix(hd->color, Colors::kBg, 90) : Colors::kFlowOff;
    lv_color_t highlightColor = hd->color;

    for (int16_t y = 0; y < hd->h; y++) {
        for (int16_t x = 0; x < hd->w; x++) {
            float px = static_cast<float>(x) - hd->x1;
            float py = static_cast<float>(y) - hd->y1;
            float t = px * ux + py * uy;
            float s = px * nx + py * ny;

            lv_color_t pixel = Colors::kBg;
            bool onShaft = (t >= shaftLoT && t <= shaftHiT && std::fabs(s) <= hd->thickness / 2.0f);
            bool onHead = false;
            if (active) {
                if (forward && t >= (len - headLen) && t <= len) {
                    float halfWidthHere = kHeadHalfWidth * (len - t) / headLen;
                    onHead = std::fabs(s) <= halfWidthHere;
                } else if (!forward && t >= 0.0f && t <= headLen) {
                    float halfWidthHere = kHeadHalfWidth * t / headLen;
                    onHead = std::fabs(s) <= halfWidthHere;
                }
            }

            if (onHead) {
                pixel = highlightColor;
            } else if (onShaft) {
                if (active) {
                    float flowT = forward ? t : (len - t);
                    pixel = (floatMod(flowT - hd->phase, kChevronPeriodPx) < kHighlightWidthPx)
                                ? highlightColor
                                : baseColor;
                } else {
                    pixel = baseColor;
                }
            }

            lv_canvas_set_px_color(hd->canvas, x, y, pixel);
        }
    }
}

} // namespace

void create(Handle *hd, lv_obj_t *parent, int16_t gx1, int16_t gy1, int16_t gx2, int16_t gy2,
            int16_t thickness, lv_color_t color) {
    hd->thickness = thickness;
    hd->color = color;

    int16_t margin = static_cast<int16_t>(thickness / 2 + kHeadHalfWidth + 2);
    int16_t minX = std::min(gx1, gx2) - margin;
    int16_t minY = std::min(gy1, gy2) - margin;
    hd->canvasX = minX;
    hd->canvasY = minY;
    hd->w = static_cast<int16_t>(std::abs(gx2 - gx1) + 2 * margin);
    hd->h = static_cast<int16_t>(std::abs(gy2 - gy1) + 2 * margin);
    hd->x1 = static_cast<float>(gx1 - minX);
    hd->y1 = static_cast<float>(gy1 - minY);
    hd->x2 = static_cast<float>(gx2 - minX);
    hd->y2 = static_cast<float>(gy2 - minY);

    hd->buf = static_cast<lv_color_t *>(malloc(static_cast<size_t>(hd->w) * hd->h * sizeof(lv_color_t)));
    hd->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(hd->canvas, hd->buf, hd->w, hd->h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(hd->canvas, hd->canvasX, hd->canvasY);

    hd->dir = Dir::Off;
    redraw(hd);
    hd->lastDir = hd->dir;
    hd->lastColor = hd->color;
}

void setState(Handle *hd, Dir dir, lv_color_t color) {
    bool colorChanged = (dir != Dir::Off) && (color.full != hd->lastColor.full);
    if (dir == hd->lastDir && !colorChanged) {
        return; // nothing visibly changed
    }
    hd->dir = dir;
    hd->color = color;
    hd->phase = 0.0f;
    redraw(hd);
    hd->lastDir = dir;
    hd->lastColor = color;
}

void tick(Handle *hd, float dtS) {
    if (hd->dir == Dir::Off) {
        return; // static outline already drawn, nothing to animate
    }
    hd->phase = floatMod(hd->phase + dtS * kAnimSpeedPxPerSec, kChevronPeriodPx);
    redraw(hd);
}

} // namespace FlowArrow
