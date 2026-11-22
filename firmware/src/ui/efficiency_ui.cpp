#include "efficiency_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "hybrid_stats.h"
#include "pwr_history.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace EfficiencyUi {

namespace {

// Left of the cockpit's centre column: trip distance over the drive's PWR
// graph. Right of it: average and instantaneous consumption, range and fuel.
constexpr int16_t kTripX = 22;
constexpr int16_t kTripBaseline = 32;
constexpr int16_t kTripGap = 8;

constexpr int16_t kGraphX = 20;
constexpr int16_t kGraphY = 54;
constexpr int16_t kGraphW = 200;
constexpr int16_t kGraphH = 108;

constexpr int16_t kStatX[2] = {424, 528};
constexpr int16_t kCaptionBaseline[2] = {36, 110};
constexpr int16_t kValueBaseline[2] = {66, 140};

// The graph: the whole session's PWR demand as one line, green where the car
// was electric (regen included -- it is drawn at zero, but it is the pack's
// time), white where the engine was driving.
//
// The plot keeps itself full in both directions. Horizontally, whatever
// points PwrHistory holds are stretched across the whole width, so the line
// runs edge to edge from the first seconds of a drive. Vertically, the
// session's peak (after smoothing) is scaled up to kGraphTop, and the scale
// eases towards a new peak over a few tenths of a second instead of jumping.
// kMinScalePct keeps an almost-idle drive from blowing noise up to full height.
constexpr int16_t kGraphTop = 4;    // inside the graph object, which leaves room for the caps
constexpr int16_t kGraphBase = 104; // y of a zero reading, inside the graph object
constexpr int16_t kLineW = 2;
constexpr float kMinScalePct = 10.0f;
constexpr float kScaleEase = 0.25f;      // share of the gap to the target closed per UI tick
constexpr float kScaleSettlePct = 0.2f;  // close enough: stop animating
constexpr uint32_t kAnimRedrawMs = 100;  // redraw cap while the scale is moving

// The area under the line fades from kFillOpa at the plot's top to nothing at
// the zero line, tied to the plot's height rather than each column's, so the
// whole area reads as one gradient over the glow behind it.
constexpr lv_opa_t kFillOpa = 120;

enum Stat { StatAvg, StatNow, StatRange, StatFuel, StatCount };
const char *const kStatCaptions[StatCount] = {"AVG L/100", "NOW L/100", "RANGE KM", "FUEL %"};

lv_obj_t *g_graph = nullptr;
lv_obj_t *g_tripValue = nullptr;
lv_obj_t *g_statValue[StatCount] = {};

// What the graph last showed, so update() only invalidates it when a point
// has actually moved -- most ticks nothing has.
uint32_t g_drawnRevision = UINT32_MAX;
PwrHistory::Bucket g_drawnOpen = {0xff, 0xff};

int16_t textW(const char *txt, const lv_font_t *font, int16_t letterSpace) {
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, letterSpace, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return static_cast<int16_t>(sz.x);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                    int16_t x, int16_t baseline, int16_t letterSpace = 0) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, letterSpace, 0);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, static_cast<int16_t>(baseline - (font->line_height - font->base_line)));
    return l;
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
int g_count = 0;          // points prepared by update(), read by drawGraph()
float g_peakPct = 0.0f;   // max of g_smooth
float g_yScale = kMinScalePct; // pct that maps to kGraphTop, eased towards the peak
float g_drawnScale = 0.0f;
uint32_t g_lastAnimRedrawMs = 0;
lv_coord_t g_colY[kGraphW]; // curve y per pixel column, for the two drawing passes
bool g_colEv[kGraphW];

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
    float h = pct / g_yScale;
    if (h > 1.0f) h = 1.0f; // the scale is still easing up to a new peak
    return static_cast<lv_coord_t>(origin.y1 + kGraphBase -
                                   h * static_cast<float>(kGraphBase - kGraphTop) + 0.5f);
}

// Snapshot the history into g_smooth/g_ev and find the peak the vertical
// scale follows.
void prepare() {
    int n = PwrHistory::size();
    PwrHistory::Bucket openBucket;
    bool hasOpen = PwrHistory::open(openBucket);
    g_count = n + (hasOpen ? 1 : 0);
    for (int i = 0; i < g_count; i++) {
        PwrHistory::Bucket b = (i < n) ? PwrHistory::at(i) : openBucket;
        g_raw[i] = static_cast<float>(b.pwrPct);
        g_ev[i] = electric(b);
    }
    smooth(g_count);
    g_peakPct = 0.0f;
    for (int i = 0; i < g_count; i++) {
        if (g_smooth[i] > g_peakPct) g_peakPct = g_smooth[i];
    }
}

// The points are spread over the full width, first point on the left edge and
// newest on the right. Every pixel column gets its curve value; the fill
// columns go down first, under a fade mask spanning the plot, then the line
// over them as 1px segments between neighbouring columns (a second pass, so
// no fill column covers stroke that spills past its own column). Each segment
// takes the nearest point's drive mode, so green/white still switch sharply
// where the mode did. Round caps close the joints between segments.
void drawGraph(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t origin;
    lv_obj_get_coords(obj, &origin);

    int count = g_count;
    lv_opa_t opa = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    if (count < 2 || opa <= LV_OPA_MIN) return;

    const float pointsPerPx = static_cast<float>(count - 1) / static_cast<float>(kGraphW - 1);
    for (int x = 0; x < kGraphW; x++) {
        float u = static_cast<float>(x) * pointsPerPx;
        int nearest = static_cast<int>(u + 0.5f);
        if (nearest > count - 1) nearest = count - 1;
        g_colEv[x] = g_ev[nearest];
        g_colY[x] = yOf(origin, curveAt(u, count));
    }

    lv_coord_t top = static_cast<lv_coord_t>(origin.y1 + kGraphTop);
    lv_coord_t base = static_cast<lv_coord_t>(origin.y1 + kGraphBase);
    lv_area_t plot = {origin.x1, top, origin.x2, base};
    lv_draw_mask_fade_param_t fade;
    lv_draw_mask_fade_init(&fade, &plot, LV_OPA_COVER, top, LV_OPA_TRANSP, base);
    int16_t fadeId = lv_draw_mask_add(&fade, nullptr);

    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_opa = static_cast<lv_opa_t>(kFillOpa * opa / LV_OPA_COVER);
    for (int x = 0; x < kGraphW; x++) {
        if (g_colY[x] >= base) continue; // nothing under a zero reading
        lv_area_t col = {static_cast<lv_coord_t>(origin.x1 + x), g_colY[x],
                         static_cast<lv_coord_t>(origin.x1 + x), base};
        fill.bg_color = colorFor(g_colEv[x]);
        lv_draw_rect(ctx, &fill, &col);
    }

    lv_draw_mask_free_param(&fade);
    lv_draw_mask_remove_id(fadeId);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = kLineW;
    line.round_start = 1;
    line.round_end = 1;
    line.opa = opa;
    for (int x = 1; x < kGraphW; x++) {
        lv_point_t a = {static_cast<lv_coord_t>(origin.x1 + x - 1), g_colY[x - 1]};
        lv_point_t b = {static_cast<lv_coord_t>(origin.x1 + x), g_colY[x]};
        line.color = colorFor(g_colEv[x]);
        lv_draw_line(ctx, &line, &a, &b);
    }
}

} // namespace

void build(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 640, 172);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    const char *tripCaption = "TRIP KM";
    makeLabel(root, tripCaption, &dinnext_12_label, Colors::kMutedText, kTripX, kTripBaseline, 1);
    g_tripValue = makeLabel(root, "--", &dinnext_24_label, Colors::kText,
                            static_cast<int16_t>(kTripX + textW(tripCaption, &dinnext_12_label, 1) + kTripGap),
                            kTripBaseline);

    g_graph = lv_obj_create(root);
    lv_obj_remove_style_all(g_graph);
    lv_obj_set_size(g_graph, kGraphW, kGraphH);
    lv_obj_set_pos(g_graph, kGraphX, kGraphY);
    lv_obj_clear_flag(g_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_graph, drawGraph, LV_EVENT_DRAW_MAIN, nullptr);

    for (int i = 0; i < StatCount; i++) {
        int16_t x = kStatX[i % 2];
        makeLabel(root, kStatCaptions[i], &dinnext_12_label, Colors::kMutedText, x,
                  kCaptionBaseline[i / 2], 1);
        g_statValue[i] = makeLabel(root, "--", &dinnext_28_stat, Colors::kText, x, kValueBaseline[i / 2]);
    }
}

namespace {

void setText(lv_obj_t *l, const char *txt, lv_color_t color = Colors::kText) {
    if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, color, 0);
}

// One decimal up to 99.9; a negative input means "no reading".
void formatL100(char *buf, size_t n, float v) {
    if (v < 0.0f) {
        snprintf(buf, n, "--");
    } else {
        snprintf(buf, n, "%.1f", static_cast<double>(v > 99.9f ? 99.9f : v));
    }
}

} // namespace

void update(const VehicleState &state) {
    PwrHistory::Bucket openBucket = {0xff, 0xff};
    PwrHistory::open(openBucket);
    bool openMoved = openBucket.pwrPct != g_drawnOpen.pwrPct ||
                     electric(openBucket) != electric(g_drawnOpen);
    bool dataChanged = PwrHistory::revision() != g_drawnRevision || openMoved;
    if (dataChanged) {
        g_drawnRevision = PwrHistory::revision();
        g_drawnOpen = openBucket;
        prepare();
    }

    float target = g_peakPct > kMinScalePct ? g_peakPct : kMinScalePct;
    float gap = target - g_yScale;
    bool animating = gap > kScaleSettlePct || gap < -kScaleSettlePct;
    g_yScale = animating ? g_yScale + gap * kScaleEase : target;

    // While easing, redraws are capped at kAnimRedrawMs; the step that
    // settles on the target always gets one, so the last frame is exact.
    uint32_t nowMs = lv_tick_get();
    bool scaleMoved = g_yScale != g_drawnScale;
    if (dataChanged ||
        (scaleMoved && (!animating || nowMs - g_lastAnimRedrawMs >= kAnimRedrawMs))) {
        g_drawnScale = g_yScale;
        g_lastAnimRedrawMs = nowMs;
        lv_obj_invalidate(g_graph);
    }

    HybridStats::Snapshot s = HybridStats::getSnapshot();

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(s.distanceKm));
    setText(g_tripValue, buf);

    formatL100(buf, sizeof(buf), s.avgConsumptionL100);
    setText(g_statValue[StatAvg], buf);

    // Right now: "EV" while no fuel is flowing, the car's own figure while
    // the engine runs, and "--" when the car reports none (standstill, where
    // litres per distance has no meaning).
    if (state.fuel_flow_mm3s <= 0.0f) {
        setText(g_statValue[StatNow], "EV", Colors::kEvGreen);
    } else {
        formatL100(buf, sizeof(buf), state.inst_consumption_l100);
        setText(g_statValue[StatNow], buf);
    }

    if (state.range_km < 0.0f) {
        setText(g_statValue[StatRange], "--");
    } else {
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(state.range_km + 0.5f));
        setText(g_statValue[StatRange], buf);
    }

    if (state.fuel_level_pct < 0.0f) {
        setText(g_statValue[StatFuel], "--");
    } else {
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(state.fuel_level_pct + 0.5f));
        setText(g_statValue[StatFuel], buf);
    }
}

} // namespace EfficiencyUi
