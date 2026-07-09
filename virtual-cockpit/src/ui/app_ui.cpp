#include "app_ui.h"
#include "cockpit_ui.h"
#include "energy_flow_ui.h"
#include "colors.h"

#include <lvgl.h>

namespace AppUi {

namespace {
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

lv_obj_t *g_tileview = nullptr;
} // namespace

void build() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, kScreenW, kScreenH);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_style_bg_color(tv, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    g_tileview = tv;

    // Tile 0: cockpit (default, at boot). Tile 1: energy-flow diagram,
    // reached by swiping left; swiping right from tile 1 returns to tile 0.
    lv_obj_t *tileCockpit = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    lv_obj_t *tileEnergy = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);

    CockpitUi::build(tileCockpit);
    EnergyFlowUi::build(tileEnergy);
}

void update(const VehicleState &state, time_t clockEpoch) {
    CockpitUi::update(state, clockEpoch);

    // EnergyFlowUi::update() redraws up to 4 animated canvas arrows every
    // call -- unlike BarGauge (which early-outs when its value hasn't
    // meaningfully changed), the flow arrows must keep redrawing to animate,
    // so doing that unconditionally regardless of which tile is on screen
    // was wasted work causing a real, noticeable slowdown. Only pay for it
    // while the energy tile is at least partially in view: with exactly 2
    // tiles, the tileview rests at scroll_x==0 only while fully parked on
    // tile 0 (cockpit) -- any nonzero scroll (mid-swipe or settled on tile 1)
    // means the energy tile is showing at least in part. Checking != 0
    // rather than a directional comparison sidesteps needing to know
    // whether LVGL's scroll_x increases or decreases towards tile 1, which
    // isn't verified on hardware.
    if (g_tileview != nullptr && lv_obj_get_scroll_x(g_tileview) != 0) {
        EnergyFlowUi::update(state);
    }
}

} // namespace AppUi
