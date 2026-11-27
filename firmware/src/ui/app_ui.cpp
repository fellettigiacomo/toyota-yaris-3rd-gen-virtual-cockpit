#include "app_ui.h"
#include "cockpit_ui.h"
#include "energy_flow_ui.h"
#include "efficiency_ui.h"
#include "gmeter_ui.h"
#include "boot_ui.h"
#include "summary_ui.h"
#include "glow.h"
#include "colors.h"
#include "screen_nav.h"
#include "steering_buttons.h"
#include "touch_nav.h"
#include "hybrid_stats.h"
#include "pwr_history.h"
#include "accel_timer.h"
#include "g_meter.h"

#include <Arduino.h>
#include <lvgl.h>

// The BOOT button, the steering-wheel MODE button and a tap anywhere on the
// panel (TouchNav -- presence-only, no coordinates/gestures) all cycle the screens: cockpit -> efficiency
// (PWR graph) -> energy flow -> G-meter -> cockpit.
//
// The cockpit cluster is never hidden: it sits above the other screens'
// content and CockpitUi::setCompact() animates it between its full layout and
// the narrow centre column the other screens are built around. Their content
// lives in plain always-built containers that are shown or hidden, the newly
// shown one fading in once the cluster has made room for it.
namespace AppUi {

namespace {
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

constexpr uint32_t kFadeMs = 280;
constexpr uint32_t kFadeDelayFromCockpitMs = 250; // let the full-size cluster clear the content first
constexpr uint32_t kFadeDelayMs = 60;
constexpr lv_coord_t kFadeRisePx = 6;

enum Screen { ScreenCockpit = 0, ScreenEfficiency, ScreenEnergy, ScreenGMeter, ScreenCount };
lv_obj_t *g_content[ScreenCount] = {}; // ScreenCockpit has none: it is the cluster alone
int g_active = ScreenCockpit;
bool g_bootShown = true; // the splash was still up at the previous update()

// Trip summary. The board stays powered for some seconds after the car is
// switched off; the powertrain messages stop at once, which is the cue.
// kCarOffMs is long enough to ride out a dropped frame or two (0x0B4 comes
// every 24 ms) and short enough to leave the summary most of that window.
// A trip shorter than kSummaryMinKm is not worth a screen.
constexpr uint32_t kCarOffMs = 1500;
constexpr float kSummaryMinKm = 0.1f;
bool g_summaryShown = false;
bool g_summaryDismissed = false; // tapped away; stays away until the car is back on

lv_obj_t *createContentContainer(lv_obj_t *parent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, kScreenW, kScreenH);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}

void fadeExec(void *obj, int32_t v) {
    lv_obj_t *c = static_cast<lv_obj_t *>(obj);
    lv_obj_set_style_opa(c, static_cast<lv_opa_t>(v), 0);
    lv_obj_set_style_translate_y(c, static_cast<lv_coord_t>(kFadeRisePx - kFadeRisePx * v / 255), 0);
}

void fadeIn(lv_obj_t *c, uint32_t delayMs) {
    fadeExec(c, LV_OPA_TRANSP);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, fadeExec);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, kFadeMs);
    lv_anim_set_delay(&a, delayMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void showScreen(int active, int previous) {
    for (int i = 0; i < ScreenCount; i++) {
        if (!g_content[i] || i == active) continue;
        lv_anim_del(g_content[i], nullptr);
        lv_obj_add_flag(g_content[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_content[active]) {
        fadeIn(g_content[active], previous == ScreenCockpit ? kFadeDelayFromCockpitMs : kFadeDelayMs);
    }
    CockpitUi::setCompact(active != ScreenCockpit);
}
} // namespace

void build() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    Glow::create(scr);

    for (int i = 0; i < ScreenCount; i++) {
        if (i != ScreenCockpit) g_content[i] = createContentContainer(scr);
    }
    EfficiencyUi::build(g_content[ScreenEfficiency]);
    EnergyFlowUi::build(g_content[ScreenEnergy]);
    GMeterUi::build(g_content[ScreenGMeter]);

    // Above the content, so the shrinking speed passes over it, never under.
    CockpitUi::build(scr);
    g_active = ScreenCockpit;

    // Above the screens, below the splash.
    SummaryUi::build(scr);

    // Last, so it is drawn above every screen; it removes itself when done.
    BootUi::build(scr);
}

void update(const VehicleState &state) {
    // Both pollers must run unconditionally (not short-circuited) -- each is
    // its own debouncer sampling live hardware state, and skipping a poll on
    // one input just because the other already fired would let its raw-state
    // timing drift. Steering presses are debounced in their own task and only
    // collected here.
    bool bootPressed = ScreenNav::pressed();
    bool tapped = TouchNav::pressed();
    bool modePressed = SteeringButtons::pressed() == SteeringButtons::Button::Mode;
    bool advance = bootPressed || tapped || modePressed;
    // The splash just went (played out or skipped): run the gauge sweep now,
    // so it is seen rather than played behind the logos.
    if (g_bootShown && !BootUi::active()) {
        g_bootShown = false;
        CockpitUi::startSweep();
    }

    if (advance && BootUi::active()) {
        // A press during the splash just skips it -- it shouldn't also
        // land the driver one screen past the cockpit.
        BootUi::skip();
    } else if (advance && g_summaryShown) {
        // A press on the summary puts it away instead of changing screen.
        g_summaryDismissed = true;
    } else if (advance) {
        int previous = g_active;
        g_active = (g_active + 1) % ScreenCount;
        showScreen(g_active, previous);
    }

    // Session stats, the PWR history and the 0-50/0-100 timer integrate
    // continuously, regardless of the visible screen -- a run started while
    // the energy or efficiency screen is shown must still be caught, and the
    // efficiency graph has to cover the whole drive, not just the parts it
    // was looked at.
    HybridStats::update(state);
    PwrHistory::update(state);
    AccelTimer::update(state);
    // Same rule, and for this one it is load-bearing twice over: the G-meter
    // only learns the board's mounting from stationary and straight-line
    // driving (see g_meter.cpp), which mostly happens while the driver is
    // looking at some other screen.
    GMeter::update(state);

    bool carOff = state.powertrain_seen && millis() - state.powertrain_ms > kCarOffMs;
    if (!carOff) g_summaryDismissed = false;
    bool wantSummary = carOff && !g_summaryDismissed &&
                       HybridStats::getSnapshot().distanceKm >= kSummaryMinKm;
    if (wantSummary && !g_summaryShown) {
        SummaryUi::show();
    } else if (!wantSummary && g_summaryShown) {
        SummaryUi::hide(); // dismissed, or the car came back on: the trip goes on
    }
    g_summaryShown = wantSummary;

    CockpitUi::update(state);

    // The other screens are only updated while shown -- the energy particles
    // and the graph cost a redraw, and there's no reason to format labels for
    // a hidden screen.
    if (g_active == ScreenEnergy) {
        EnergyFlowUi::update(state);
    } else if (g_active == ScreenEfficiency) {
        EfficiencyUi::update(state);
    } else if (g_active == ScreenGMeter) {
        GMeterUi::update(state);
    }
}

} // namespace AppUi
