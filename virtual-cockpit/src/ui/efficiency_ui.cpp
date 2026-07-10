#include "efficiency_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "hybrid_stats.h"

#include <cstdio>

namespace EfficiencyUi {

namespace {

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

// EV / ENGINE headline + split bar.
constexpr int16_t kHeadY = 22; // EV / ENGINE labels row
constexpr int16_t kBarX = 30;
constexpr int16_t kBarW = 580;
constexpr int16_t kBarY = 52;
constexpr int16_t kBarH = 26;

// Four stat tiles along the bottom.
constexpr int16_t kTileCount = 4;
constexpr int16_t kTileCaptionY = 108;
constexpr int16_t kTileValueY = 126;

lv_obj_t *g_bar = nullptr;
lv_obj_t *g_evLabel = nullptr;     // "EV 72%"
lv_obj_t *g_engineLabel = nullptr; // "ENGINE 28%"
lv_obj_t *g_tileValue[kTileCount] = {nullptr, nullptr, nullptr, nullptr};

int16_t tileCx(int i) {
    // Evenly spaced across the bar's span.
    return static_cast<int16_t>(kBarX + (kBarW * (2 * i + 1)) / (2 * kTileCount));
}

// Content-sized label (no wrap), positioned by anchor (left/centre/right).
enum Anchor { AnchorLeft, AnchorCenter, AnchorRight };
lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                    int16_t anchorX, int16_t y, Anchor anchor) {
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int16_t x = anchorX;
    if (anchor == AnchorCenter) x = static_cast<int16_t>(anchorX - sz.x / 2);
    else if (anchor == AnchorRight) x = static_cast<int16_t>(anchorX - sz.x);
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

// Re-centres a dynamic label around cx after its text (hence width) changed.
void setCentered(lv_obj_t *l, const lv_font_t *font, int16_t cx, int16_t y, const char *txt) {
    lv_label_set_text(l, txt);
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(l, static_cast<int16_t>(cx - sz.x / 2), y);
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

    // EV / ENGINE headline labels above the bar.
    g_evLabel = makeLabel(root, "EV", &dinnext_24_label, Colors::kEvGreen, kBarX, kHeadY, AnchorLeft);
    g_engineLabel = makeLabel(root, "ENGINE", &dinnext_24_label, Colors::kEngineRed,
                              static_cast<int16_t>(kBarX + kBarW), kHeadY, AnchorRight);

    // Split bar: track = ENGINE (red), indicator = EV (green) growing from the
    // left, so a full-EV drive is all green. The indicator's right edge is
    // square -> a crisp EV|ENGINE boundary.
    g_bar = lv_bar_create(root);
    lv_obj_remove_style_all(g_bar);
    lv_obj_set_size(g_bar, kBarW, kBarH);
    lv_obj_set_pos(g_bar, kBarX, kBarY);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_bar, Colors::kEngineRed, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_bar, kBarH / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, Colors::kEvGreen, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar, kBarH / 2, LV_PART_INDICATOR);

    // Stat tiles: static captions (small, muted) + dynamic values (filled in
    // update()).
    static const char *caps[kTileCount] = {"DISTANCE", "REGEN", "AVG SPEED", "MAX SPEED"};
    for (int i = 0; i < kTileCount; i++) {
        makeLabel(root, caps[i], &lv_font_montserrat_14, Colors::kMutedText, tileCx(i), kTileCaptionY,
                  AnchorCenter);
        g_tileValue[i] = makeLabel(root, "--", &dinnext_24_label, Colors::kText, tileCx(i),
                                   kTileValueY, AnchorCenter);
    }
}

void update(const VehicleState &) {
    HybridStats::Snapshot s = HybridStats::getSnapshot();

    int evPct = static_cast<int>(s.evSharePct + 0.5f);
    if (evPct < 0) evPct = 0;
    if (evPct > 100) evPct = 100;
    lv_bar_set_value(g_bar, evPct, LV_ANIM_OFF);

    char buf[16];
    snprintf(buf, sizeof(buf), "EV %d%%", evPct);
    lv_label_set_text(g_evLabel, buf);
    snprintf(buf, sizeof(buf), "ENGINE %d%%", 100 - evPct);
    lv_label_set_text(g_engineLabel, buf);
    // Right label is right-anchored at the bar's end; re-place after text change.
    lv_point_t sz;
    lv_txt_get_size(&sz, buf, &dinnext_24_label, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(g_engineLabel, static_cast<int16_t>(kBarX + kBarW - sz.x), kHeadY);

    snprintf(buf, sizeof(buf), "%.1f KM", static_cast<double>(s.distanceKm));
    setCentered(g_tileValue[0], &dinnext_24_label, tileCx(0), kTileValueY, buf);
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(s.regenSharePct + 0.5f));
    setCentered(g_tileValue[1], &dinnext_24_label, tileCx(1), kTileValueY, buf);
    snprintf(buf, sizeof(buf), "%d KM/H", static_cast<int>(s.avgSpeedKph + 0.5f));
    setCentered(g_tileValue[2], &dinnext_24_label, tileCx(2), kTileValueY, buf);
    snprintf(buf, sizeof(buf), "%d KM/H", static_cast<int>(s.maxSpeedKph + 0.5f));
    setCentered(g_tileValue[3], &dinnext_24_label, tileCx(3), kTileValueY, buf);
}

} // namespace EfficiencyUi
