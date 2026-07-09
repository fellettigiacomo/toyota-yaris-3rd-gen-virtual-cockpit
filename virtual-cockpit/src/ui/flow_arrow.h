#pragma once

#include <lvgl.h>

// A thick, directional "energy flow" arrow between two fixed points,
// rendered as a canvas (same rasterization technique as BarGauge -- no
// bitmap assets, LV_USE_LINE/LV_USE_ARC stay off). When active, a bright
// band travels along the shaft toward the head to read as motion, similar
// to the Prius/Toyota energy-monitor style; when Off the segment renders as
// a dim, static outline.
namespace FlowArrow {

enum class Dir {
    Off,     // not flowing -- dim static outline
    Forward, // flow travels from the (x1,y1) endpoint toward (x2,y2)
    Reverse, // flow travels from (x2,y2) toward (x1,y1)
};

struct Handle {
    lv_obj_t *canvas = nullptr;
    lv_color_t *buf = nullptr;
    int16_t canvasX = 0;
    int16_t canvasY = 0;
    int16_t w = 0;
    int16_t h = 0;
    float x1 = 0, y1 = 0; // endpoints, in canvas-local pixel coords
    float x2 = 0, y2 = 0;
    int16_t thickness = 10;
    lv_color_t color = lv_color_white();
    Dir dir = Dir::Off;
    lv_color_t lastColor = lv_color_black();
    Dir lastDir = static_cast<Dir>(-1); // forces the first redraw
    float phase = 0.0f;                 // animation position along the shaft, px
};

// Creates the canvas sized to fit the (gx1,gy1)-(gx2,gy2) segment (in
// `parent`-local coordinates) plus enough margin for the shaft thickness
// and arrowhead. Initial state is Off.
void create(Handle *hd, lv_obj_t *parent, int16_t gx1, int16_t gy1, int16_t gx2, int16_t gy2,
            int16_t thickness, lv_color_t color);

// Sets direction + color; redraws once immediately. Cheap early-out if
// neither changed since the last call.
void setState(Handle *hd, Dir dir, lv_color_t color);

// Advances the flow animation by dtS seconds and re-rasterizes. No-op when
// Dir::Off (nothing to animate, and the static Off render already happened
// in setState).
void tick(Handle *hd, float dtS);

} // namespace FlowArrow
