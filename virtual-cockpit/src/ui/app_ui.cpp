#include "app_ui.h"
#include "cockpit_ui.h"
#include "energy_flow_ui.h"
#include "efficiency_ui.h"
#include "colors.h"
#include "screen_nav.h"
#include "hybrid_stats.h"
#include "accel_timer.h"

#include <lvgl.h>

// The BOOT button (or the steering-wheel MODE button, see mode_button below)
// cycles between plain, always-built full-screen containers with an instant
// lv_obj_add/clear_flag(LV_OBJ_FLAG_HIDDEN) toggle (no scroll animation --
// touch/swipe was dropped for being laggy on real hardware). Screens, in
// cycle order: cockpit -> energy flow -> efficiency -> cockpit.
namespace AppUi {

namespace {
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

enum Screen { ScreenCockpit = 0, ScreenEnergy, ScreenEfficiency, ScreenCount };
lv_obj_t *g_screens[ScreenCount] = {nullptr, nullptr, nullptr};
int g_active = ScreenCockpit;

// mode_button (0x4AC) stays active for a whole press/repeat-tap window: the
// ECU latches it for ~2s after each press, retriggered per tap, so presses
// closer than ~2.5s apart merge into one window and can only ever produce
// one screen cycle -- that's a property of the signal itself, not of this
// code (see docs/signal_findings.md Addendum 5). The decoder counts the
// idle->active edges at CAN-frame granularity into mode_button_edges; here
// we just consume the counter delta, so a press registers even if this UI
// task ever stalls past a whole press window (which sampling the level at
// UI_SYNC_INTERVAL_MS could then miss).
uint32_t g_lastModeEdges = 0;

uint32_t modeButtonPresses(const VehicleState &state) {
    uint32_t delta = state.mode_button_edges - g_lastModeEdges;
    g_lastModeEdges = state.mode_button_edges;
    // Real edges are >=2.5s apart, so more than a few per 33ms sync tick can
    // only mean the counter restarted from 0 under us (demo replay loop) --
    // resync without cycling the screen.
    return delta <= 3 ? delta : 0;
}

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
    // Both must run unconditionally (not short-circuited) -- modeButtonPresses
    // has to consume the edge counter every call to stay in sync with it.
    uint32_t steps = modeButtonPresses(state);
    if (ScreenNav::pressed()) {
        steps++;
    }
    if (steps > 0) {
        g_active = (g_active + steps) % ScreenCount;
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
