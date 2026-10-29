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
// pixels and a full buffer spans the screen edge to edge.
constexpr int16_t kPointStepX = kScreenW / PwrHistory::kCapacity;
constexpr int16_t kGraphTop = 10;
constexpr int16_t kGraphBase = 110; // y of a zero reading
constexpr int16_t kLineW = 2;

// Stat rows, unchanged from the split layout: montserrat_14 captions over
// dinnext_28_stat values, in the two bottom corners.
constexpr int16_t kStatCaptionY = 124;
constexpr int16_t kStatValueY = 142;

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

lv_point_t pointAt(const lv_area_t &origin, int i, const PwrHistory::Bucket &b) {
    lv_point_t p;
    p.x = static_cast<lv_coord_t>(origin.x1 + i * kPointStepX + kPointStepX / 2);
    p.y = static_cast<lv_coord_t>(origin.y1 + kGraphBase -
                                  static_cast<int32_t>(b.pwrPct) * (kGraphBase - kGraphTop) / 100);
    return p;
}

bool electric(const PwrHistory::Bucket &b) {
    return b.evPct >= 50;
}

// One segment per pair of neighbouring points, coloured by the point it runs
// into, so the colour changes exactly where the drive mode did. Round caps
// close the joints between segments of different slope.
void drawGraph(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t origin;
    lv_obj_get_coords(obj, &origin);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width = kLineW;
    dsc.round_start = 1;
    dsc.round_end = 1;

    int n = PwrHistory::size();
    PwrHistory::Bucket openBucket;
    bool hasOpen = PwrHistory::open(openBucket);
    int total = n + (hasOpen ? 1 : 0);
    if (total < 2) return;

    PwrHistory::Bucket prev = PwrHistory::at(0);
    lv_point_t prevPt = pointAt(origin, 0, prev);
    for (int i = 1; i < total; i++) {
        PwrHistory::Bucket cur = (i < n) ? PwrHistory::at(i) : openBucket;
        lv_point_t curPt = pointAt(origin, i, cur);
        dsc.color = electric(cur) ? Colors::kEvGreen : Colors::kPwrWhite;
        lv_draw_line(ctx, &dsc, &prevPt, &curPt);
        prevPt = curPt;
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

    // The graph object spans the plot's full height plus room for the
    // stroke, so the line's caps are never clipped at the top or bottom.
    g_graph = lv_obj_create(root);
    lv_obj_remove_style_all(g_graph);
    lv_obj_set_size(g_graph, kScreenW, kGraphBase + kLineW);
    lv_obj_set_pos(g_graph, 0, 0);
    lv_obj_clear_flag(g_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_graph, drawGraph, LV_EVENT_DRAW_MAIN, nullptr);

    // The two session stats keep the bottom corners, one per side.
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
