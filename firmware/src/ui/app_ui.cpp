#include "app_ui.h"
#include "cockpit_ui.h"
#include "energy_flow_ui.h"
#include "efficiency_ui.h"
#include "colors.h"
#include "screen_nav.h"
#include "touch_nav.h"
#include "hybrid_stats.h"
#include "accel_timer.h"

#include <lvgl.h>

// The BOOT button and a tap anywhere on the panel (TouchNav -- presence-only,
// no coordinates/gestures) both cycle between plain, always-built full-screen
// containers with an instant lv_obj_add/clear_flag(LV_OBJ_FLAG_HIDDEN) toggle
// (no scroll animation -- an earlier lv_tileview swipe was dropped for being
// laggy on real hardware; a discrete tap-anywhere doesn't have that problem,
// it's a single full-refresh flush same as a BOOT press already causes).
// Screens, in cycle order: cockpit -> energy flow -> efficiency -> cockpit.
namespace AppUi {

namespace {
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

enum Screen { ScreenCockpit = 0, ScreenEnergy, ScreenEfficiency, ScreenCount };
lv_obj_t *g_screens[ScreenCount] = {nullptr, nullptr, nullptr};
int g_active = ScreenCockpit;

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

void showOnly(int active) {
    for (int i = 0; i < ScreenCount; i++) {
        if (i == active) lv_obj_clear_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
}
} // namespace

void build() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < ScreenCount; i++) g_screens[i] = createScreenContainer(scr);

    CockpitUi::build(g_screens[ScreenCockpit]);
    EnergyFlowUi::build(g_screens[ScreenEnergy]);
    EfficiencyUi::build(g_screens[ScreenEfficiency]);

    g_active = ScreenCockpit;
    showOnly(g_active);
}

void update(const VehicleState &state) {
    // Both must run unconditionally (not short-circuited) -- each is its own
    // debouncer sampling live hardware state, and skipping a poll on one
    // input just because the other already fired would let its raw-state
    // timing drift.
    bool bootPressed = ScreenNav::pressed();
    bool tapped = TouchNav::pressed();
    if (bootPressed || tapped) {
        g_active = (g_active + 1) % ScreenCount;
        showOnly(g_active);
    }

    // Session stats and the 0-50/0-100 timer integrate continuously,
    // regardless of the visible screen -- a run started while the energy or
    // efficiency screen is shown must still be caught.
    HybridStats::update(state);
    AccelTimer::update(state);

    CockpitUi::update(state);

    // The energy and efficiency screens are only updated while shown -- the
    // energy arrows are expensive to redraw, and there's no reason to format
    // labels for a hidden screen.
    if (g_active == ScreenEnergy) {
        EnergyFlowUi::update(state);
    } else if (g_active == ScreenEfficiency) {
        EfficiencyUi::update(state);
    }
}

} // namespace AppUi
