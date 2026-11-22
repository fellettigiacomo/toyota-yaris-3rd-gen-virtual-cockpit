#include "summary_ui.h"
#include "colors.h"
#include "glow.h"
#include "fonts/fonts.h"
#include "hybrid_stats.h"

#include <cstdio>
#include <initializer_list>

namespace SummaryUi {

namespace {

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

// Two rows of four equal columns, centred on the screen as a block; caption
// over value, both centred in their column, same pairing as the efficiency
// screen's stat row. No title: nothing else ever looks like this screen.
constexpr int kCols = 4;
constexpr int kRows = 2;
constexpr int16_t kColW = kScreenW / kCols; // 160
constexpr int16_t kRowCaptionY[kRows] = {30, 100};
constexpr int16_t kValueDy = 18; // caption top to value top

enum Stat {
    StatDistance, StatTime, StatAvgCons, StatFuel,
    StatEvShare, StatAvgSpeed, StatMaxSpeed, StatBattery,
    StatCount
};
const char *const kCaptions[StatCount] = {"DISTANCE KM", "TIME MIN", "AVG L/100", "FUEL L",
                                          "EV %",        "AVG KM/H", "MAX KM/H",  "BATTERY %"};

lv_obj_t *g_root = nullptr;
lv_obj_t *g_value[StatCount] = {};

lv_obj_t *makeCentred(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                      int16_t x, int16_t y, int16_t w) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    return l;
}

} // namespace

void build(lv_obj_t *parent) {
    g_root = lv_obj_create(parent);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, kScreenW, kScreenH);
    lv_obj_set_pos(g_root, 0, 0);
    lv_obj_set_style_bg_color(g_root, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    Glow::create(g_root);

    for (int i = 0; i < StatCount; i++) {
        int16_t x = static_cast<int16_t>((i % kCols) * kColW);
        int16_t y = kRowCaptionY[i / kCols];
        makeCentred(g_root, kCaptions[i], &lv_font_montserrat_14, Colors::kMutedText, x, y, kColW);
        g_value[i] = makeCentred(g_root, "--", &dinnext_28_stat, Colors::kText, x,
                                 static_cast<int16_t>(y + kValueDy), kColW);
    }

    lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);
}

void show() {
    HybridStats::Snapshot s = HybridStats::getSnapshot();
    char buf[24];

    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(s.distanceKm));
    lv_label_set_text(g_value[StatDistance], buf);

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(s.tripTimeS / 60.0f + 0.5f));
    lv_label_set_text(g_value[StatTime], buf);

    if (s.avgConsumptionL100 < 0.0f) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        float v = s.avgConsumptionL100 > 99.9f ? 99.9f : s.avgConsumptionL100;
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    }
    lv_label_set_text(g_value[StatAvgCons], buf);

    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(s.fuelUsedL));
    lv_label_set_text(g_value[StatFuel], buf);

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(s.evSharePct + 0.5f));
    lv_label_set_text(g_value[StatEvShare], buf);

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(s.avgSpeedKph + 0.5f));
    lv_label_set_text(g_value[StatAvgSpeed], buf);

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(s.maxSpeedKph + 0.5f));
    lv_label_set_text(g_value[StatMaxSpeed], buf);

    // Start and end of the trip, so the reader sees whether the drive left
    // the pack fuller or emptier than it found it.
    snprintf(buf, sizeof(buf), "%d - %d", static_cast<int>(s.socStartPct + 0.5f),
             static_cast<int>(s.socNowPct + 0.5f));
    lv_label_set_text(g_value[StatBattery], buf);

    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_HIDDEN);
}

void hide() {
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);
}

} // namespace SummaryUi
