#include "efficiency_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "hybrid_stats.h"
#include "pwr_history.h"

#include <cstdint>
#include <cstdio>

namespace EfficiencyUi {

namespace {

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;
constexpr int16_t kEdgeMargin = 16;

// The graph: the whole session's PWR demand as one line, green where the car
// was electric (regen included -- it is drawn at zero, but it is the pack's
// time), white where the engine was driving. PwrHistory keeps the session in
// kCapacity points however long it runs, so each point gets kPointStepX
// pixels and a full buffer spans the screen edge to edge. The plot takes the
// whole screen; the stats sit over its top corners.
constexpr int16_t kPointStepX = kScreenW / PwrHistory::kCapacity;
constexpr int16_t kGraphTop = 8;
constexpr int16_t kGraphBase = 168; // y of a zero reading
constexpr int16_t kLineW = 2;

// The area under the line fades from kFillMaxMix (0..255 share of the line's
// colour, at the plot's top) down to the plain background at the zero line.
// The fade is tied to screen height, not to each column's own height, so the
// whole area reads as one gradient. LVGL 8.3 has no per-stop opacity, but the
// background is a solid colour, so mixing towards it gives the same result.
constexpr uint8_t kFillMaxMix = 120;

// Stat rows: montserrat_14 captions over dinnext_28_stat values, in the two
// top corners.
constexpr int16_t kStatCaptionY = 8;
constexpr int16_t kStatValueY = 26;

lv_obj_t *g_graph = nullptr;
lv_obj_t *g_distanceValue = nullptr;
lv_obj_t *g_avgSpeedValue = nullptr;

// What the graph last showed, so update() only invalidates it when a point
// has actually moved -- most ticks nothing has.
uint32_t g_drawnRevision = UINT32_MAX;
PwrHistory::Bucket g_drawnOpen = {0xff, 0xff};

int16_t textW(const char *txt, const lv_font_t *font, int16_t letterSpace = 0) {
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, letterSpace, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return static_cast<int16_t>(sz.x);
}

enum Anchor { AnchorLeft, AnchorRight };
lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                    int16_t anchorX, int16_t y, Anchor anchor) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, anchor == AnchorRight ? static_cast<int16_t>(anchorX - textW(txt, font))
                                            : anchorX,
                   y);
    return l;
}

lv_obj_t *makeRect(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, lv_color_t color) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, color, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// Drawing is smoothed, the history is not: the points get a small weighted
// moving average (kSmoothRadius each side, triangular weights) to take the
// edge off per-bucket noise, and are then joined by a Catmull-Rom curve
// evaluated at every pixel column instead of straight segments.
constexpr int kSmoothRadius = 2;
constexpr int kMaxPoints = PwrHistory::kCapacity + 1; // closed buckets + the open one

float g_raw[kMaxPoints];
float g_smooth[kMaxPoints];
bool g_ev[kMaxPoints];
lv_coord_t g_colY[kScreenW]; // curve y per pixel column, for the two drawing passes
bool g_colEv[kScreenW];

bool electric(const PwrHistory::Bucket &b) {
    return b.evPct >= 50;
}

lv_color_t colorFor(bool ev) {
    return ev ? Colors::kEvGreen : Colors::kPwrWhite;
}

// Near either end only the neighbours that exist are averaged, normalised by
// the weights actually used -- so the newest point still moves live instead
// of being dragged towards zero by points that aren't there yet.
void smooth(int count) {
    for (int i = 0; i < count; i++) {
        float sum = 0.0f, wsum = 0.0f;
        for (int k = -kSmoothRadius; k <= kSmoothRadius; k++) {
            int j = i + k;
            if (j < 0 || j >= count) continue;
            float w = static_cast<float>(kSmoothRadius + 1 - (k < 0 ? -k : k));
            sum += w * g_raw[j];
            wsum += w;
        }
        g_smooth[i] = sum / wsum;
    }
}

// Curve value (pct) at fractional point index u, 0..count-1. The end points
// are repeated to give the spline its outer control points; the result is
// clamped because the curve can dip below zero next to a sharp regen drop.
float curveAt(float u, int count) {
    int i = static_cast<int>(u);
    if (i >= count - 1) return g_smooth[count - 1];
    float t = u - static_cast<float>(i);
    float p0 = g_smooth[i > 0 ? i - 1 : 0];
    float p1 = g_smooth[i];
    float p2 = g_smooth[i + 1];
    float p3 = g_smooth[i + 2 < count ? i + 2 : count - 1];
    float t2 = t * t, t3 = t2 * t;
    float v = 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                      (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
    if (v < 0.0f) v = 0.0f;
    if (v > 100.0f) v = 100.0f;
    return v;
}

lv_coord_t yOf(const lv_area_t &origin, float pct) {
    return static_cast<lv_coord_t>(origin.y1 + kGraphBase -
                                   pct * static_cast<float>(kGraphBase - kGraphTop) / 100.0f + 0.5f);
}

// One 1px gradient column, from the curve down to the zero line.
void drawFill(lv_draw_ctx_t *ctx, const lv_area_t &origin, lv_coord_t x, lv_coord_t y, bool ev) {
    lv_coord_t base = static_cast<lv_coord_t>(origin.y1 + kGraphBase);
    if (y >= base) return; // nothing under a zero reading

    lv_area_t col;
    col.x1 = x;
    col.x2 = x;
    col.y1 = y;
    col.y2 = base;

    uint8_t mix = static_cast<uint8_t>(static_cast<int32_t>(kFillMaxMix) * (base - y) /
                                       (kGraphBase - kGraphTop));
    lv_color_t top = lv_color_mix(colorFor(ev), Colors::kBg, mix);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = top;
    dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    dsc.bg_grad.stops_count = 2;
    dsc.bg_grad.stops[0].color = top;
    dsc.bg_grad.stops[0].frac = 0;
    dsc.bg_grad.stops[1].color = Colors::kBg;
    dsc.bg_grad.stops[1].frac = 255;
    lv_draw_rect(ctx, &dsc, &col);
}

// Points sit at the centre of their kPointStepX slot. Every pixel column
// between the first and last point gets its curve value; all the gradient
// columns go down first, then the line over them as 1px segments between
// neighbouring columns (a second pass, so no fill column covers stroke that
// spills past its own column). Each segment takes the nearest point's drive
// mode, so green/white still switch sharply where the mode did. Round caps
// close the joints between segments.
void drawGraph(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t origin;
    lv_obj_get_coords(obj, &origin);

    int n = PwrHistory::size();
    PwrHistory::Bucket openBucket;
    bool hasOpen = PwrHistory::open(openBucket);
    int count = n + (hasOpen ? 1 : 0);
    if (count < 2) return;

    for (int i = 0; i < count; i++) {
        PwrHistory::Bucket b = (i < n) ? PwrHistory::at(i) : openBucket;
        g_raw[i] = static_cast<float>(b.pwrPct);
        g_ev[i] = electric(b);
    }
    smooth(count);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width = kLineW;
    dsc.round_start = 1;
    dsc.round_end = 1;

    constexpr int kFirstX = kPointStepX / 2;
    int lastX = (count - 1) * kPointStepX + kFirstX;
    for (int x = kFirstX; x <= lastX; x++) {
        float u = static_cast<float>(x - kFirstX) / kPointStepX;
        int nearest = static_cast<int>(u + 0.5f);
        if (nearest > count - 1) nearest = count - 1;
        g_colEv[x] = g_ev[nearest];
        g_colY[x] = yOf(origin, curveAt(u, count));
        drawFill(ctx, origin, static_cast<lv_coord_t>(origin.x1 + x), g_colY[x], g_colEv[x]);
    }

    for (int x = kFirstX + 1; x <= lastX; x++) {
        lv_point_t a = {static_cast<lv_coord_t>(origin.x1 + x - 1), g_colY[x - 1]};
        lv_point_t b = {static_cast<lv_coord_t>(origin.x1 + x), g_colY[x]};
        dsc.color = colorFor(g_colEv[x]);
        lv_draw_line(ctx, &dsc, &a, &b);
    }
}

} // namespace

void build(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, kScreenW, kScreenH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // Zero line, just under where a zero reading's stroke sits.
    makeRect(root, 0, kGraphBase + kLineW, kScreenW, 1, Colors::kBarTrack);

    // The graph object spans the whole screen, which leaves room for the
    // stroke's caps above the top and below the zero line.
    g_graph = lv_obj_create(root);
    lv_obj_remove_style_all(g_graph);
    lv_obj_set_size(g_graph, kScreenW, kScreenH);
    lv_obj_set_pos(g_graph, 0, 0);
    lv_obj_clear_flag(g_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_graph, drawGraph, LV_EVENT_DRAW_MAIN, nullptr);

    // The two session stats take the top corners, one per side. Created after
    // the graph so they stay on top when the line runs up under them.
    makeLabel(root, "DISTANCE", &lv_font_montserrat_14, Colors::kMutedText, kEdgeMargin,
              kStatCaptionY, AnchorLeft);
    g_distanceValue = makeLabel(root, "0.0 KM", &dinnext_28_stat, Colors::kText, kEdgeMargin,
                                kStatValueY, AnchorLeft);
    makeLabel(root, "AVG SPEED", &lv_font_montserrat_14, Colors::kMutedText,
              static_cast<int16_t>(kScreenW - kEdgeMargin), kStatCaptionY, AnchorRight);
    g_avgSpeedValue = makeLabel(root, "0 KM/H", &dinnext_28_stat, Colors::kText,
                                static_cast<int16_t>(kScreenW - kEdgeMargin), kStatValueY,
                                AnchorRight);
}

void update(const VehicleState &) {
    PwrHistory::Bucket openBucket = {0xff, 0xff};
    PwrHistory::open(openBucket);
    bool openMoved = openBucket.pwrPct != g_drawnOpen.pwrPct ||
                     electric(openBucket) != electric(g_drawnOpen);
    if (PwrHistory::revision() != g_drawnRevision || openMoved) {
        g_drawnRevision = PwrHistory::revision();
        g_drawnOpen = openBucket;
        lv_obj_invalidate(g_graph);
    }

    HybridStats::Snapshot s = HybridStats::getSnapshot();

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f KM", static_cast<double>(s.distanceKm));
    lv_label_set_text(g_distanceValue, buf);
    lv_obj_set_x(g_distanceValue, kEdgeMargin);

    snprintf(buf, sizeof(buf), "%d KM/H", static_cast<int>(s.avgSpeedKph + 0.5f));
    lv_label_set_text(g_avgSpeedValue, buf);
    lv_obj_set_x(g_avgSpeedValue,
                 static_cast<int16_t>(kScreenW - kEdgeMargin - textW(buf, &dinnext_28_stat)));
}

} // namespace EfficiencyUi
