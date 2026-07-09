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

// Rasterizes a single straight segment (hd->x1,y1)-(hd->x2,y2) into
// hd->buf. Every pixel in the canvas bbox is classified in (t, s) space --
// t = distance projected along the segment's axis, s = perpendicular
// offset -- which turns "is this pixel inside a thick line" and "is this
// pixel inside the arrowhead triangle" into simple linear tests.
void redrawStraight(Handle *hd) {
    float dx = hd->x2 - hd->x1;
    float dy = hd->y2 - hd->y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) len = 1.0f;
    float ux = dx / len, uy = dy / len; // unit vector start->end
    float nx = -uy, ny = ux;            // unit normal

    bool active = (hd->dir != Dir::Off);
    bool forward = (hd->dir == Dir::Forward);

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

// Same idea as redrawStraight(), but for a path bent 90 degrees at
// (hd->xm, hd->ym): segment A runs (x1,y1)->(xm,ym), segment B runs
// (xm,ym)->(x2,y2). The arrowhead only ever sits at the path's overall
// head end (x2,y2 if Forward, x1,y1 if Reverse -- never at the elbow), and
// the animated highlight band's position is measured continuously along
// the concatenated path length (segment A then segment B).
void redrawBent(Handle *hd) {
    float dxA = hd->xm - hd->x1, dyA = hd->ym - hd->y1;
    float lenA = std::sqrt(dxA * dxA + dyA * dyA);
    if (lenA < 1.0f) lenA = 1.0f;
    float uAx = dxA / lenA, uAy = dyA / lenA, nAx = -uAy, nAy = uAx;

    float dxB = hd->x2 - hd->xm, dyB = hd->y2 - hd->ym;
    float lenB = std::sqrt(dxB * dxB + dyB * dyB);
    if (lenB < 1.0f) lenB = 1.0f;
    float uBx = dxB / lenB, uBy = dyB / lenB, nBx = -uBy, nBy = uBx;

    float totalLen = lenA + lenB;
    bool active = (hd->dir != Dir::Off);
    bool forward = (hd->dir == Dir::Forward);
    float headLen = active ? kHeadLen : 0.0f;

    // The arrowhead only eats into whichever segment holds the head end;
    // the elbow-adjacent end of each segment is always plain shaft.
    float shaftLoA = (active && !forward) ? headLen : 0.0f;
    float shaftHiA = lenA;
    float shaftLoB = 0.0f;
    float shaftHiB = (active && forward) ? (lenB - headLen) : lenB;

    lv_color_t baseColor = active ? lv_color_mix(hd->color, Colors::kBg, 90) : Colors::kFlowOff;
    lv_color_t highlightColor = hd->color;

    for (int16_t y = 0; y < hd->h; y++) {
        for (int16_t x = 0; x < hd->w; x++) {
            float pxA = static_cast<float>(x) - hd->x1;
            float pyA = static_cast<float>(y) - hd->y1;
            float tA = pxA * uAx + pyA * uAy;
            float sA = pxA * nAx + pyA * nAy;

            float pxB = static_cast<float>(x) - hd->xm;
            float pyB = static_cast<float>(y) - hd->ym;
            float tB = pxB * uBx + pyB * uBy;
            float sB = pxB * nBx + pyB * nBy;

            lv_color_t pixel = Colors::kBg;
            bool onHead = false;
            if (active) {
                if (!forward && tA >= 0.0f && tA <= headLen) {
                    float halfWidthHere = kHeadHalfWidth * tA / headLen;
                    onHead = std::fabs(sA) <= halfWidthHere;
                } else if (forward && tB >= (lenB - headLen) && tB <= lenB) {
                    float halfWidthHere = kHeadHalfWidth * (lenB - tB) / headLen;
                    onHead = std::fabs(sB) <= halfWidthHere;
                }
            }

            bool onShaftA = (tA >= shaftLoA && tA <= shaftHiA && std::fabs(sA) <= hd->thickness / 2.0f);
            bool onShaftB = (tB >= shaftLoB && tB <= shaftHiB && std::fabs(sB) <= hd->thickness / 2.0f);

            if (onHead) {
                pixel = highlightColor;
            } else if (onShaftA || onShaftB) {
                if (active) {
                    float absolutePos = onShaftA ? tA : (lenA + tB);
                    float flowT = forward ? absolutePos : (totalLen - absolutePos);
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

void redraw(Handle *hd) {
    if (hd->bent) {
        redrawBent(hd);
    } else {
        redrawStraight(hd);
    }
}

void initCommon(Handle *hd, lv_obj_t *parent, int16_t minX, int16_t minY, int16_t maxX, int16_t maxY,
                int16_t thickness, lv_color_t color) {
    int16_t margin = static_cast<int16_t>(thickness / 2 + kHeadHalfWidth + 2);
    hd->thickness = thickness;
    hd->color = color;
    hd->canvasX = static_cast<int16_t>(minX - margin);
    hd->canvasY = static_cast<int16_t>(minY - margin);
    hd->w = static_cast<int16_t>((maxX - minX) + 2 * margin);
    hd->h = static_cast<int16_t>((maxY - minY) + 2 * margin);

    hd->buf = static_cast<lv_color_t *>(malloc(static_cast<size_t>(hd->w) * hd->h * sizeof(lv_color_t)));
    hd->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(hd->canvas, hd->buf, hd->w, hd->h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(hd->canvas, hd->canvasX, hd->canvasY);

    hd->dir = Dir::Off;
}

void finishInit(Handle *hd) {
    redraw(hd);
    hd->lastDir = hd->dir;
    hd->lastColor = hd->color;
}

} // namespace

void create(Handle *hd, lv_obj_t *parent, int16_t gx1, int16_t gy1, int16_t gx2, int16_t gy2,
            int16_t thickness, lv_color_t color) {
    hd->bent = false;
    int16_t minX = std::min(gx1, gx2), maxX = std::max(gx1, gx2);
    int16_t minY = std::min(gy1, gy2), maxY = std::max(gy1, gy2);
    initCommon(hd, parent, minX, minY, maxX, maxY, thickness, color);

    hd->x1 = static_cast<float>(gx1 - hd->canvasX);
    hd->y1 = static_cast<float>(gy1 - hd->canvasY);
    hd->x2 = static_cast<float>(gx2 - hd->canvasX);
    hd->y2 = static_cast<float>(gy2 - hd->canvasY);

    finishInit(hd);
}

void createBent(Handle *hd, lv_obj_t *parent, int16_t gx1, int16_t gy1, int16_t gxm, int16_t gym,
                int16_t gx2, int16_t gy2, int16_t thickness, lv_color_t color) {
    hd->bent = true;
    int16_t minX = std::min({gx1, gxm, gx2}), maxX = std::max({gx1, gxm, gx2});
    int16_t minY = std::min({gy1, gym, gy2}), maxY = std::max({gy1, gym, gy2});
    initCommon(hd, parent, minX, minY, maxX, maxY, thickness, color);

    hd->x1 = static_cast<float>(gx1 - hd->canvasX);
    hd->y1 = static_cast<float>(gy1 - hd->canvasY);
    hd->xm = static_cast<float>(gxm - hd->canvasX);
    hd->ym = static_cast<float>(gym - hd->canvasY);
    hd->x2 = static_cast<float>(gx2 - hd->canvasX);
    hd->y2 = static_cast<float>(gy2 - hd->canvasY);

    finishInit(hd);
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
