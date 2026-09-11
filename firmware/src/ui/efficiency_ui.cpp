#include "efficiency_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "hybrid_stats.h"

#include <cstdio>

namespace EfficiencyUi {

namespace {

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

// The split IS the screen: the EV share fills from the left edge, the engine
// share holds the rest, and the seam between them is the whole point of the
// layout -- so the two percentages sit right against it, at the same 120px
// size the cockpit gives the speed. Full-bleed washes rather than saturated
// fills: this is a dashboard at night, and half a screen of #3ddc84 would
// be a torch. The seam itself carries the full-strength colour.
constexpr uint8_t kEvWashMix = 18;     // % of kEvGreen mixed into the background
constexpr uint8_t kEngineWashMix = 9;  // % of kText, enough to read as a second region
constexpr int16_t kSeamW = 2;

// Percentages straddle the seam: EV's block ends just before it, ENGINE's
// starts just after.
constexpr int16_t kSeamPad = 18;
constexpr int16_t kEdgeMargin = 16; // the pair slides inwards rather than off-screen
constexpr int16_t kPctGap = 5;      // between a number and its "%" suffix
constexpr int16_t kNumberTracking = -3; // same tightening the cockpit's speed number uses

// Rows. dinnext_120_speed's real line_height is 85 and montserrat_14's is
// 16, so the number band runs 19..104 and the stat rows 124..163 -- the
// numbers centred in the space above the stats now that nothing sits over
// them. The two bands never overlap, which matters at the extremes: when the
// seam is near an edge the percentage pair slides inwards and ends up
// directly above a corner stat, so the 20px between the bands is what keeps
// that from reading as a collision.
constexpr int16_t kNumberY = 19;
constexpr int16_t kStatCaptionY = 124;
constexpr int16_t kStatValueY = 142;

lv_obj_t *g_evFill = nullptr;
lv_obj_t *g_seam = nullptr;
lv_obj_t *g_evNumber = nullptr;
lv_obj_t *g_evPct = nullptr;
lv_obj_t *g_engineNumber = nullptr;
lv_obj_t *g_enginePct = nullptr;
lv_obj_t *g_distanceValue = nullptr;
lv_obj_t *g_avgSpeedValue = nullptr;

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

// One percentage = a big number with a small "%" hung off its bottom-right,
// the same pairing the cockpit uses for the battery and power readouts. The
// block's width is the number plus the gap plus the suffix.
int16_t blockWidth(const char *number) {
    // Measured WITH the tracking the number is actually styled with -- the
    // blocks are placed off these widths, so a mismatch would show up as the
    // pair drifting away from the seam.
    return static_cast<int16_t>(textW(number, &dinnext_120_speed, kNumberTracking) + kPctGap +
                                textW("%", &dinnext_28_stat));
}

// No EV/ENGINE captions: the green fill starts at the left edge and the
// numbers carry the same two colours, which already says which share is
// which -- a word over each would only repeat what the colour states.
void placeBlock(lv_obj_t *number, lv_obj_t *pct, const char *numberTxt, int16_t left) {
    lv_label_set_text(number, numberTxt);
    lv_obj_set_pos(number, left, kNumberY);
    lv_obj_align_to(pct, number, LV_ALIGN_OUT_RIGHT_BOTTOM, kPctGap, -14);
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

    // Engine wash covers everything; the EV fill is drawn over it and only
    // its width changes, so a redraw touches one object, not two.
    makeRect(root, 0, 0, kScreenW, kScreenH, lv_color_mix(Colors::kText, Colors::kBg, kEngineWashMix));
    g_evFill = makeRect(root, 0, 0, 1, kScreenH,
                        lv_color_mix(Colors::kEvGreen, Colors::kBg, kEvWashMix));

    // The seam, at full strength with the same glow the cockpit's gauges use
    // -- it is the one edge on this screen that means something.
    g_seam = makeRect(root, 0, 0, kSeamW, kScreenH, Colors::kEvGreen);
    lv_obj_set_style_shadow_color(g_seam, Colors::kEvGreen, 0);
    lv_obj_set_style_shadow_width(g_seam, 12, 0);
    lv_obj_set_style_shadow_spread(g_seam, 1, 0);
    lv_obj_set_style_shadow_opa(g_seam, LV_OPA_50, 0);

    g_evNumber = makeLabel(root, "0", &dinnext_120_speed, Colors::kEvGreen, 0, kNumberY, AnchorLeft);
    lv_obj_set_style_text_letter_space(g_evNumber, kNumberTracking, 0);
    g_evPct = makeLabel(root, "%", &dinnext_28_stat, Colors::kMutedText, 0, kNumberY, AnchorLeft);

    g_engineNumber = makeLabel(root, "0", &dinnext_120_speed, Colors::kText, 0, kNumberY, AnchorLeft);
    lv_obj_set_style_text_letter_space(g_engineNumber, kNumberTracking, 0);
    g_enginePct = makeLabel(root, "%", &dinnext_28_stat, Colors::kMutedText, 0, kNumberY, AnchorLeft);

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
    HybridStats::Snapshot s = HybridStats::getSnapshot();

    int evPct = static_cast<int>(s.evSharePct + 0.5f);
    if (evPct < 0) evPct = 0;
    if (evPct > 100) evPct = 100;

    int16_t seamX = static_cast<int16_t>(static_cast<int32_t>(evPct) * kScreenW / 100);

    // A zero-width fill is not a thing LVGL draws sensibly, and neither edge
    // of the screen has room for a 2px seam drawn half off it.
    if (seamX <= 0) {
        lv_obj_add_flag(g_evFill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g_evFill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(g_evFill, seamX);
    }
    int16_t seamDrawX = seamX - kSeamW / 2;
    if (seamDrawX < 0) seamDrawX = 0;
    if (seamDrawX > kScreenW - kSeamW) seamDrawX = kScreenW - kSeamW;
    lv_obj_set_x(g_seam, seamDrawX);

    char evTxt[8], engineTxt[8];
    snprintf(evTxt, sizeof(evTxt), "%d", evPct);
    snprintf(engineTxt, sizeof(engineTxt), "%d", 100 - evPct);

    // Both percentages hang off the seam -- EV's block ending just before it,
    // ENGINE's starting just after. At the extremes that pair would run off
    // an edge, so slide the pair back on screen as a unit: the numbers stop
    // touching the seam, which is unavoidable when the seam is at the very
    // edge, but they stay in reading order and keep their own colours.
    int16_t evW = blockWidth(evTxt);
    int16_t engineW = blockWidth(engineTxt);
    int16_t evLeft = static_cast<int16_t>(seamX - kSeamPad - evW);
    int16_t engineLeft = static_cast<int16_t>(seamX + kSeamPad);

    int16_t shift = 0;
    if (evLeft < kEdgeMargin) shift = static_cast<int16_t>(kEdgeMargin - evLeft);
    int16_t engineRight = static_cast<int16_t>(engineLeft + engineW + shift);
    if (engineRight > kScreenW - kEdgeMargin) {
        shift = static_cast<int16_t>(shift - (engineRight - (kScreenW - kEdgeMargin)));
    }
    evLeft = static_cast<int16_t>(evLeft + shift);
    engineLeft = static_cast<int16_t>(engineLeft + shift);

    placeBlock(g_evNumber, g_evPct, evTxt, evLeft);
    placeBlock(g_engineNumber, g_enginePct, engineTxt, engineLeft);

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
