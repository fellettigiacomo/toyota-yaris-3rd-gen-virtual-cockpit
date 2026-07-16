#pragma once

#include <lvgl.h>

// A thick, directional "energy flow" arrow between two fixed points (or
// three, for a 90-degree bent path), rendered as a canvas (same
// rasterization technique as BarGauge -- no bitmap assets, LV_USE_LINE/
// LV_USE_ARC stay off). When active, a bright band travels along the shaft
// toward the head to read as motion, similar to the Prius/Toyota
// energy-monitor style; when Off the segment renders as a dim, static
// outline.
namespace FlowArrow {

enum class Dir {
    Off,     // not flowing -- dim static outline
    Forward, // flow travels from the start point toward the end point
    Reverse, // flow travels from the end point toward the start point
};

struct Handle {
    lv_obj_t *canvas = nullptr;
    lv_color_t *buf = nullptr;
    int16_t canvasX = 0;
    int16_t canvasY = 0;
    int16_t w = 0;
    int16_t h = 0;
    float x1 = 0, y1 = 0; // start point, in canvas-local pixel coords
    float x2 = 0, y2 = 0; // end point (the Forward "head" end)
    float xm = 0, ym = 0; // elbow point -- only used when bent == true
    bool bent = false;
    int16_t thickness = 10;
    lv_color_t color = lv_color_white();
    Dir dir = Dir::Off;
    lv_color_t lastColor = lv_color_black();
    Dir lastDir = static_cast<Dir>(-1); // forces the first redraw
    float phase = 0.0f;                 // animation position along the path, px
};

// Creates the canvas sized to fit the (gx1,gy1)-(gx2,gy2) straight segment
// (in `parent`-local coordinates) plus enough margin for the shaft
// thickness and arrowhead. Initial state is Off.
void create(Handle *hd, lv_obj_t *parent, int16_t gx1, int16_t gy1, int16_t gx2, int16_t gy2,
            int16_t thickness, lv_color_t color);

// Same as create(), but the path bends 90 degrees at (gxm,gym) instead of
// running straight -- two collinear-with-thickness segments
// (gx1,gy1)-(gxm,gym) and (gxm,gym)-(gx2,gy2), animated/arrowed as one
// continuous path. Use for a corner (e.g. down-then-across) rather than a
// diagonal line.
void createBent(Handle *hd, lv_obj_t *parent, int16_t gx1, int16_t gy1, int16_t gxm, int16_t gym,
                 int16_t gx2, int16_t gy2, int16_t thickness, lv_color_t color);

// Sets direction + color; redraws once immediately. Cheap early-out if
// neither changed since the last call.
void setState(Handle *hd, Dir dir, lv_color_t color);

// Advances the flow animation by dtS seconds and re-rasterizes. No-op when
// Dir::Off (nothing to animate, and the static Off render already happened
// in setState).
void tick(Handle *hd, float dtS);

} // namespace FlowArrow
