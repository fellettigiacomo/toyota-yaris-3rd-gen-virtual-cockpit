#include "app_ui.h"
#include "cockpit_ui.h"
#include "energy_flow_ui.h"
#include "colors.h"
#include "screen_nav.h"

#include <lvgl.h>

// Screen switching used to be an lv_tileview swipe (touch-driven); both the
// swipe transition and the energy-flow tile itself were reported to be
// badly laggy on real hardware. Touch is dropped entirely now -- this
// switches between two plain, always-built full-screen containers with an
// instant lv_obj_add_flag/clear_flag(LV_OBJ_FLAG_HIDDEN) toggle (no scroll
// animation at all) driven by the board's BOOT button (see screen_nav.h).
namespace AppUi {

namespace {
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

lv_obj_t *g_cockpitScreen = nullptr;
lv_obj_t *g_energyScreen = nullptr;
bool g_energyActive = false;

lv_obj_t *createScreenContainer(lv_obj_t *parent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, kScreenW, kScreenH);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_style_bg_color(c, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}
} // namespace

void build() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    g_cockpitScreen = createScreenContainer(scr);
    g_energyScreen = createScreenContainer(scr);

    CockpitUi::build(g_cockpitScreen);
    EnergyFlowUi::build(g_energyScreen);

    g_energyActive = false;
    lv_obj_add_flag(g_energyScreen, LV_OBJ_FLAG_HIDDEN);
}

void update(const VehicleState &state, time_t clockEpoch) {
    if (ScreenNav::pressed()) {
        g_energyActive = !g_energyActive;
        if (g_energyActive) {
            lv_obj_clear_flag(g_energyScreen, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_cockpitScreen, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_cockpitScreen, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_energyScreen, LV_OBJ_FLAG_HIDDEN);
        }
    }

    CockpitUi::update(state, clockEpoch);

    // Only pay for EnergyFlowUi's animated canvas redraws while its screen
    // is actually the one shown -- exact now (no mid-transition ambiguity
    // to guess at, unlike the old tileview-scroll-position heuristic).
    if (g_energyActive) {
        EnergyFlowUi::update(state);
    }
}

} // namespace AppUi
