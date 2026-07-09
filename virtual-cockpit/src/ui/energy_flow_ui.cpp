#include "energy_flow_ui.h"
#include "colors.h"
#include "flow_arrow.h"

#include <Arduino.h>

namespace EnergyFlowUi {

namespace {

// --- Node layout, 640x172 tile ---
// Row 1 (ENGINE/MOTOR/BATTERY): labels at y=2..16, icons centered at y=42.
// Row 2 (WHEELS): icon centered at y=122, label at y=148..164. ENGINE and
// BATTERY sit closer to MOTOR (and further from the screen edges) than an
// earlier layout that stretched them out to x=100/555 -- 150px between each
// pair of centers, evenly spaced, with generous (~130px) margin from the
// left/right screen edges even counting the labels. ENGINE-WHEELS bends 90
// degrees at the ENGINE column's x, straight down to WHEELS' row then
// straight across -- a real mechanical link (planetary power-split), not
// meant to read as a diagonal shortcut through the diagram.
constexpr int16_t kEngineCx = 170, kEngineCy = 42;
constexpr int16_t kMotorCx = 320, kMotorCy = 42;
constexpr int16_t kBatteryCx = 470, kBatteryCy = 42;
constexpr int16_t kWheelsCx = 320, kWheelsCy = 122;

constexpr int16_t kEngineHalfW = 26, kEngineHalfH = 14; // body rect
constexpr int16_t kMotorRadius = 20;                    // outer rim
constexpr int16_t kBatteryHalfW = 13, kBatteryHalfH = 9; // body rect
constexpr int16_t kWheelsRadius = 20;                    // outer rim

constexpr int16_t kLabelRowY = 2;
constexpr int16_t kWheelsLabelY = 148;
constexpr int16_t kLabelW = 80;

// Small visible gap between each arrow endpoint and the node it connects
// to, so the (opaque, z-ordered-on-top) node icon never clips/overlaps the
// arrow's own rendered shaft -- keeps every segment's true thickness
// visible right up to the node, instead of the last couple px reading as
// thinner where an icon's edge cuts across it.
constexpr int16_t kEndpointGap = 3;

// Segment endpoints, derived from the node geometry above.
constexpr int16_t kEngineRightX = kEngineCx + kEngineHalfW + kEndpointGap;
constexpr int16_t kEngineBottomY = kEngineCy + kEngineHalfH + kEndpointGap;
constexpr int16_t kMotorLeftX = kMotorCx - kMotorRadius - kEndpointGap;
constexpr int16_t kMotorRightX = kMotorCx + kMotorRadius + kEndpointGap;
constexpr int16_t kMotorBottomY = kMotorCy + kMotorRadius + kEndpointGap;
constexpr int16_t kBatteryLeftX = kBatteryCx - kBatteryHalfW - kEndpointGap;
constexpr int16_t kWheelsTopY = kWheelsCy - kWheelsRadius - kEndpointGap;
constexpr int16_t kWheelsLeftX = kWheelsCx - kWheelsRadius - kEndpointGap;

constexpr int16_t kArrowThickness = 10;

FlowArrow::Handle g_engineMotorArrow;
FlowArrow::Handle g_motorBatteryArrow;
FlowArrow::Handle g_motorWheelsArrow;
FlowArrow::Handle g_engineWheelsArrow;

uint32_t g_lastUpdateMs = 0;

// Each FlowArrow::tick() re-rasterizes its canvas pixel-by-pixel -- doing
// that at the full ~30Hz AppUi::update() rate was reported, on real
// hardware, to make this tile (and the old touch-swipe transition into/out
// of it) badly laggy; a first pass at ~6-7Hz was still laggy, meaning the
// per-redraw cost itself (up to 4 full canvas repaints) is the dominant
// expense, not just how often it runs. Direction/color changes
// (FlowArrow::setState(), below) stay fully responsive every call
// regardless, since those are rare and cheap -- only the continuous
// flowing animation is throttled this hard.
constexpr float kAnimTickIntervalS = 0.5f; // ~2Hz redraw instead of ~30Hz
float g_animAccumS = 0.0f;

// --- Hysteresis for "is the vehicle stopped" (avoids flicker right at
// standstill, same idea as debouncing a noisy digital input) ---
constexpr float kStopEnterKph = 2.0f; // speed must drop below this to become "stopped"
constexpr float kStopExitKph = 4.0f;  // speed must rise above this to become "moving"
bool g_stoppedLatched = true;

constexpr float kDecelHsiThreshold = 5.0f;   // hsi_power <= -this counts as regen/decel
constexpr float kPwrHsiThreshold = 5.0f;     // hsi_power >= this, PLUS a confirmed falling SOC
                                              // trend, counts as "powering" (assist)
constexpr float kPwrHsiConfident = 25.0f;    // hsi_power >= this counts as "powering" on its own,
                                              // no SOC confirmation needed
constexpr int8_t kAccelDemandDecelThreshold = -8;
constexpr float kSocTrendEps = 0.05f;        // %/s, below this magnitude counts as "flat"

struct FlowState {
    FlowArrow::Dir engineMotor = FlowArrow::Dir::Off;
    FlowArrow::Dir motorBattery = FlowArrow::Dir::Off;
    FlowArrow::Dir motorWheels = FlowArrow::Dir::Off;
    FlowArrow::Dir engineWheels = FlowArrow::Dir::Off;
};

// The qualitative "brain": maps VehicleState onto the 4 segment directions.
// See docs/signal_findings.md and the plan this was designed against for the
// 10 physical cases each branch below covers (case numbers in comments).
FlowState deriveFlow(const VehicleState &state) {
    if (g_stoppedLatched) {
        if (state.speed_kph > kStopExitKph) g_stoppedLatched = false;
    } else {
        if (state.speed_kph < kStopEnterKph) g_stoppedLatched = true;
    }
    bool isStopped = g_stoppedLatched;

    bool decelerating = state.brake_pressed || state.accel_demand < kAccelDemandDecelThreshold ||
                         state.hsi_power <= -kDecelHsiThreshold;
    bool socRising = state.soc_trend_pct_per_s > kSocTrendEps;
    bool socFalling = state.soc_trend_pct_per_s < -kSocTrendEps;
    // The SOC-trend EMA only needs to arbitrate the ambiguous near-zero hsi_power
    // band (case 7 vs 8, per the request this was built against) -- a
    // confidently high hsi_power already means "assist" on its own, and
    // waiting on socFalling there just adds lag: SOC only ticks in coarse
    // ~0.5% steps every few seconds, so a brief kick-down's hsi spike would
    // often end before the EMA caught up (confirmed by an offline trace
    // against data/logs/session_0047.log -- case 7 never distinguished
    // itself from plain cruise on real kick-downs until this split was added).
    bool powering = state.ice_running &&
                    (state.hsi_power >= kPwrHsiConfident || (state.hsi_power >= kPwrHsiThreshold && socFalling));

    FlowState f;

    // Case 10 (gear B, descending): compression braking drives the engine
    // from the wheels, in addition to whatever regen is also happening.
    if (state.gear == Gear::B && decelerating && !isStopped) {
        f.engineWheels = FlowArrow::Dir::Reverse; // WHEELS -> ENGINE
    } else if (state.ice_running && !state.ev_drive && state.gear != Gear::R && !isStopped &&
               !decelerating) {
        // Cases 6/7/8: ICE driving the wheels, whether cruising, assisted,
        // or also charging -- this segment doesn't care which.
        f.engineWheels = FlowArrow::Dir::Forward; // ENGINE -> WHEELS
    }

    // Cases 3 (idle charge) and 8 (drive + charge): engine spinning the
    // generator whenever SOC is climbing and we're not in a regen/braking
    // event (that charge path is handled by motorBattery below instead).
    if (state.ice_running && socRising && !decelerating) {
        f.engineMotor = FlowArrow::Dir::Forward; // ENGINE -> MOTOR
    }

    // motorBattery / motorWheels: regen (decelerating) takes priority,
    // then engine-driven charging, then EV/reverse/kick-down assist.
    if (decelerating && !isStopped) {
        f.motorBattery = FlowArrow::Dir::Forward; // MOTOR -> BATTERY (regen)
        f.motorWheels = FlowArrow::Dir::Reverse;  // WHEELS -> MOTOR (regen)
    } else {
        if (f.engineMotor == FlowArrow::Dir::Forward) {
            f.motorBattery = FlowArrow::Dir::Forward; // MOTOR -> BATTERY (engine charge)
        } else if (state.ev_drive || state.gear == Gear::R || powering) {
            f.motorBattery = FlowArrow::Dir::Reverse; // BATTERY -> MOTOR (discharge/assist)
        }
        if (state.ev_drive || state.gear == Gear::R || powering) {
            f.motorWheels = FlowArrow::Dir::Forward; // MOTOR -> WHEELS (EV/reverse/assist)
        }
    }

    return f;
}

void createIconLabel(lv_obj_t *parent, int16_t cx, int16_t labelY, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, Colors::kText, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, kLabelW);
    lv_obj_set_pos(l, static_cast<int16_t>(cx - kLabelW / 2), labelY);
    lv_label_set_text(l, text);
}

lv_obj_t *createCircle(lv_obj_t *parent, int16_t cx, int16_t cy, int16_t radius, lv_color_t color,
                        bool filled) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    int16_t d = static_cast<int16_t>(radius * 2);
    lv_obj_set_size(c, d, d);
    lv_obj_set_pos(c, static_cast<int16_t>(cx - radius), static_cast<int16_t>(cy - radius));
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    if (filled) {
        lv_obj_set_style_bg_color(c, color, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(c, color, 0);
        lv_obj_set_style_border_width(c, 2, 0);
    }
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

void createEngineIcon(lv_obj_t *parent) {
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, kEngineHalfW * 2, kEngineHalfH * 2);
    lv_obj_set_pos(body, static_cast<int16_t>(kEngineCx - kEngineHalfW),
                    static_cast<int16_t>(kEngineCy - kEngineHalfH));
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, Colors::kEngineRed, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 4, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Two small "cylinder head" nubs on top, same idea as the battery icon's nub.
    for (int i = 0; i < 2; i++) {
        lv_obj_t *nub = lv_obj_create(parent);
        lv_obj_remove_style_all(nub);
        lv_obj_set_size(nub, 6, 6);
        int16_t nx = static_cast<int16_t>(kEngineCx - 15 + i * 24);
        lv_obj_set_pos(nub, nx, static_cast<int16_t>(kEngineCy - kEngineHalfH - 5));
        lv_obj_set_style_bg_color(nub, Colors::kEngineRed, 0);
        lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
        lv_obj_clear_flag(nub, LV_OBJ_FLAG_SCROLLABLE);
    }

    createIconLabel(parent, kEngineCx, kLabelRowY, "ENGINE");
}

void createMotorIcon(lv_obj_t *parent) {
    createCircle(parent, kMotorCx, kMotorCy, kMotorRadius, Colors::kAccentCyan, false);
    createCircle(parent, kMotorCx, kMotorCy, 6, Colors::kAccentCyan, true);
    createIconLabel(parent, kMotorCx, kLabelRowY, "MOTOR");
}

void createBatteryIcon(lv_obj_t *parent) {
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, kBatteryHalfW * 2, kBatteryHalfH * 2);
    lv_obj_set_pos(body, static_cast<int16_t>(kBatteryCx - kBatteryHalfW),
                    static_cast<int16_t>(kBatteryCy - kBatteryHalfH));
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, Colors::kBatteryBlue, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 1, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nub = lv_obj_create(parent);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 2, 6);
    lv_obj_set_pos(nub, static_cast<int16_t>(kBatteryCx - kBatteryHalfW - 2),
                    static_cast<int16_t>(kBatteryCy - 3));
    lv_obj_set_style_bg_color(nub, Colors::kBatteryBlue, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_clear_flag(nub, LV_OBJ_FLAG_SCROLLABLE);

    createIconLabel(parent, kBatteryCx, kLabelRowY, "BATTERY");
}

void createWheelsIcon(lv_obj_t *parent) {
    createCircle(parent, kWheelsCx, kWheelsCy, kWheelsRadius, Colors::kText, false);
    createCircle(parent, kWheelsCx, kWheelsCy, 4, Colors::kMutedText, true);
    createIconLabel(parent, kWheelsCx, kWheelsLabelY, "WHEELS");
}

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

} // namespace

void build(lv_obj_t *parent) {
    // Same reasoning as CockpitUi::build(): build onto its own child object
    // rather than restyling `parent` directly.
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, kScreenW, kScreenH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // Arrows first so the node icons layer on top of their endpoints.
    FlowArrow::create(&g_engineMotorArrow, root, kEngineRightX, kEngineCy, kMotorLeftX, kMotorCy,
                       kArrowThickness, Colors::kEngineRed);
    FlowArrow::create(&g_motorBatteryArrow, root, kMotorRightX, kMotorCy, kBatteryLeftX, kBatteryCy,
                       kArrowThickness, Colors::kChgGreen);
    FlowArrow::create(&g_motorWheelsArrow, root, kMotorCx, kMotorBottomY, kWheelsCx, kWheelsTopY,
                       kArrowThickness, Colors::kAccentCyan);
    // Bent 90 degrees at (kEngineCx, kWheelsCy): straight down from ENGINE,
    // then straight across into WHEELS' left edge -- a right-angle corner
    // instead of a diagonal line.
    FlowArrow::createBent(&g_engineWheelsArrow, root, kEngineCx, kEngineBottomY, kEngineCx, kWheelsCy,
                          kWheelsLeftX, kWheelsCy, kArrowThickness, Colors::kEngineRed);

    createEngineIcon(root);
    createMotorIcon(root);
    createBatteryIcon(root);
    createWheelsIcon(root);
}

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    float dtS = (g_lastUpdateMs == 0) ? 0.0f : (nowMs - g_lastUpdateMs) / 1000.0f;
    g_lastUpdateMs = nowMs;
    if (dtS > 0.25f) dtS = 0.25f; // clamp a long gap (first call, tile switch) to avoid an animation jump

    FlowState f = deriveFlow(state);

    FlowArrow::setState(&g_engineMotorArrow, f.engineMotor, Colors::kEngineRed);
    FlowArrow::setState(&g_engineWheelsArrow, f.engineWheels, Colors::kEngineRed);
    FlowArrow::setState(&g_motorBatteryArrow, f.motorBattery,
                         f.motorBattery == FlowArrow::Dir::Forward ? Colors::kChgGreen : Colors::kBatteryBlue);
    FlowArrow::setState(&g_motorWheelsArrow, f.motorWheels,
                         f.motorWheels == FlowArrow::Dir::Forward ? Colors::kAccentCyan : Colors::kChgGreen);

    g_animAccumS += dtS;
    if (g_animAccumS >= kAnimTickIntervalS) {
        FlowArrow::tick(&g_engineMotorArrow, g_animAccumS);
        FlowArrow::tick(&g_motorBatteryArrow, g_animAccumS);
        FlowArrow::tick(&g_motorWheelsArrow, g_animAccumS);
        FlowArrow::tick(&g_engineWheelsArrow, g_animAccumS);
        g_animAccumS = 0.0f;
    }
}

} // namespace EnergyFlowUi
