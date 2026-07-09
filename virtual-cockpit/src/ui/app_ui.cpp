#include "app_ui.h"
#include "cockpit_ui.h"
#include "energy_flow_ui.h"
#include "colors.h"

#include <lvgl.h>

namespace AppUi {

namespace {
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;
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

    // Tile 0: cockpit (default, at boot). Tile 1: energy-flow diagram,
    // reached by swiping left; swiping right from tile 1 returns to tile 0.
    lv_obj_t *tileCockpit = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    lv_obj_t *tileEnergy = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);

    CockpitUi::build(tileCockpit);
    EnergyFlowUi::build(tileEnergy);
}

void update(const VehicleState &state, time_t clockEpoch) {
    CockpitUi::update(state, clockEpoch);
    EnergyFlowUi::update(state);
}

} // namespace AppUi
