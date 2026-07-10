#include "energy_flow_ui.h"
#include "colors.h"
#include "flow_arrow.h"
#include "fonts/fonts.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace EnergyFlowUi {

namespace {

constexpr float kPi = 3.14159265358979f;

// --- Node layout, 640x172 screen ---
// Top row: ENGINE (left) - MOTOR (center) - BATTERY (right), all icon-centered
// at y=54. Bottom: WHEELS, icon-centered at (320,130). Labels sit BESIDE their
// icons (ENGINE label left, BATTERY label right + live SOC% below it, WHEELS
// label upper-left) rather than above/below, which frees vertical space and
// keeps the top/bottom margins comfortable (~12px top, ~20px bottom). MOTOR is
// the one exception -- centered node, so its label rides above it.
constexpr int16_t kEngineCx = 155, kEngineCy = 54;
constexpr int16_t kMotorCx = 320, kMotorCy = 54;
constexpr int16_t kBatteryCx = 485, kBatteryCy = 54;
constexpr int16_t kWheelsCx = 320, kWheelsCy = 130;

// All four icons are drawn into a square canvas of this size (see the icon
// helpers below), so a single half-extent governs every node's edge.
constexpr int16_t kIconSize = 44;
constexpr int16_t kIconHalf = kIconSize / 2; // 22

// Small gap between an arrow endpoint and the icon it touches, so the opaque
// icon canvas never clips the arrow's rendered shaft.
constexpr int16_t kEndpointGap = 3;
constexpr int16_t kNodeReach = kIconHalf + kEndpointGap; // 25, center -> arrow endpoint

constexpr int16_t kArrowThickness = 10;

// Arrow endpoints, derived from the node geometry above.
constexpr int16_t kEngineRightX = kEngineCx + kNodeReach;
constexpr int16_t kEngineBottomY = kEngineCy + kNodeReach;
constexpr int16_t kMotorLeftX = kMotorCx - kNodeReach;
constexpr int16_t kMotorRightX = kMotorCx + kNodeReach;
constexpr int16_t kMotorBottomY = kMotorCy + kNodeReach;
constexpr int16_t kBatteryLeftX = kBatteryCx - kNodeReach;
constexpr int16_t kWheelsTopY = kWheelsCy - kNodeReach;
constexpr int16_t kWheelsLeftX = kWheelsCx - kNodeReach;
// The bent ENGINE->WHEELS arrow enters the wheel's LOWER-left so it clears the
// WHEELS label, which sits directly to the left of the wheel (vertically
// centred on it).
constexpr int16_t kEngineWheelsEnterY = kWheelsCy + 15;

// Label boxes. Node labels use the 24px DIN font (dinnext_24_label, full
// uppercase alphabet); white, to match the requested design.
constexpr int16_t kLabelW = 96;
constexpr int16_t kLabelLineH = 18; // dinnext_24_label line height

FlowArrow::Handle g_engineMotorArrow;
FlowArrow::Handle g_motorBatteryArrow;
FlowArrow::Handle g_motorWheelsArrow;
FlowArrow::Handle g_engineWheelsArrow;

lv_obj_t *g_battValueLabel = nullptr;
lv_obj_t *g_battPctLabel = nullptr;
int g_lastBattPct = -1;

uint32_t g_lastUpdateMs = 0;

// Each FlowArrow::tick() re-rasterizes its canvas pixel-by-pixel; running all
// four at the full ~30Hz update rate was laggy on real hardware, so the
// flowing animation is redrawn much slower. Direction/colour changes
// (setState) stay responsive every call regardless.
constexpr float kAnimTickIntervalS = 0.5f; // ~2Hz
float g_animAccumS = 0.0f;

// --- Hysteresis for "is the vehicle stopped" (avoids flicker at standstill) ---
constexpr float kStopEnterKph = 2.0f;
constexpr float kStopExitKph = 4.0f;
bool g_stoppedLatched = true;

constexpr float kDecelHsiThreshold = 5.0f;
constexpr float kPwrHsiThreshold = 5.0f;
constexpr float kPwrHsiConfident = 25.0f;
constexpr int8_t kAccelDemandDecelThreshold = -8;
constexpr float kSocTrendEps = 0.05f;

struct FlowState {
    FlowArrow::Dir engineMotor = FlowArrow::Dir::Off;
    FlowArrow::Dir motorBattery = FlowArrow::Dir::Off;
    FlowArrow::Dir motorWheels = FlowArrow::Dir::Off;
    FlowArrow::Dir engineWheels = FlowArrow::Dir::Off;
};

// The qualitative "brain": maps VehicleState onto the 4 segment directions.
// Unchanged from the previous design pass; see docs/signal_findings.md and the
// plan for the 10 physical cases each branch covers.
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
    bool powering = state.ice_running &&
                    (state.hsi_power >= kPwrHsiConfident || (state.hsi_power >= kPwrHsiThreshold && socFalling));

    FlowState f;

    if (state.gear == Gear::B && decelerating && !isStopped) {
        f.engineWheels = FlowArrow::Dir::Reverse; // WHEELS -> ENGINE (engine braking)
    } else if (state.ice_running && !state.ev_drive && state.gear != Gear::R && !isStopped &&
               !decelerating) {
        f.engineWheels = FlowArrow::Dir::Forward; // ENGINE -> WHEELS
    }

    if (state.ice_running && socRising && !decelerating) {
        f.engineMotor = FlowArrow::Dir::Forward; // ENGINE -> MOTOR (charge)
    }

    if (decelerating && !isStopped) {
        f.motorBattery = FlowArrow::Dir::Forward; // MOTOR -> BATTERY (regen)
        f.motorWheels = FlowArrow::Dir::Reverse;  // WHEELS -> MOTOR (regen)
    } else {
        if (f.engineMotor == FlowArrow::Dir::Forward) {
            f.motorBattery = FlowArrow::Dir::Forward; // MOTOR -> BATTERY (engine charge)
        } else if (state.ev_drive || state.gear == Gear::R || powering) {
            f.motorBattery = FlowArrow::Dir::Reverse; // BATTERY -> MOTOR (assist)
        }
        if (state.ev_drive || state.gear == Gear::R || powering) {
            f.motorWheels = FlowArrow::Dir::Forward; // MOTOR -> WHEELS
        }
    }

    return f;
}

// ---------------------------------------------------------------------------
// Icons: each is drawn once, at build time, into its own opaque lv_canvas
// (filled with the screen background so it's invisible except where a shape is
// painted -- same trick the flow arrows use). Drawn once, so the per-pixel
// rasterization cost here is irrelevant (unlike the animated arrows).
// ---------------------------------------------------------------------------
struct IconCanvas {
    lv_obj_t *canvas;
    int w, h;
};

IconCanvas makeIconCanvas(lv_obj_t *parent, int16_t cx, int16_t cy) {
    IconCanvas ic;
    ic.w = kIconSize;
    ic.h = kIconSize;
    lv_color_t *buf = static_cast<lv_color_t *>(malloc(static_cast<size_t>(kIconSize) * kIconSize * sizeof(lv_color_t)));
    ic.canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(ic.canvas, buf, kIconSize, kIconSize, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(ic.canvas, cx - kIconHalf, cy - kIconHalf);
    lv_canvas_fill_bg(ic.canvas, Colors::kBg, LV_OPA_COVER);
    return ic;
}

inline void px(IconCanvas &ic, int x, int y, lv_color_t c) {
    if (x >= 0 && x < ic.w && y >= 0 && y < ic.h) lv_canvas_set_px_color(ic.canvas, x, y, c);
}

void fillRect(IconCanvas &ic, int x0, int y0, int x1, int y1, lv_color_t c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) px(ic, x, y, c);
}

// Border-only rectangle, `t` px thick.
void strokeRect(IconCanvas &ic, int x0, int y0, int x1, int y1, int t, lv_color_t c) {
    fillRect(ic, x0, y0, x1, y0 + t - 1, c);
    fillRect(ic, x0, y1 - t + 1, x1, y1, c);
    fillRect(ic, x0, y0, x0 + t - 1, y1, c);
    fillRect(ic, x1 - t + 1, y0, x1, y1, c);
}

void fillCircle(IconCanvas &ic, int cx, int cy, int r, lv_color_t c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) px(ic, cx + dx, cy + dy, c);
}

// Filled annulus (ring) between rIn and rOut.
void ring(IconCanvas &ic, int cx, int cy, int rOut, int rIn, lv_color_t c) {
    for (int dy = -rOut; dy <= rOut; dy++)
        for (int dx = -rOut; dx <= rOut; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= rOut * rOut && d2 >= rIn * rIn) px(ic, cx + dx, cy + dy, c);
        }
}

// Thick line by stamping small discs along the segment.
void thickLine(IconCanvas &ic, float x0, float y0, float x1, float y1, int r, lv_color_t c) {
    float dx = x1 - x0, dy = y1 - y0;
    int steps = static_cast<int>(std::max(std::fabs(dx), std::fabs(dy))) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = static_cast<float>(i) / steps;
        fillCircle(ic, static_cast<int>(x0 + dx * t + 0.5f), static_cast<int>(y0 + dy * t + 0.5f), r, c);
    }
}

// Engine block: main body + raised valve cover + two cylinder stubs + a
// belt pulley on the right, with a couple of cooling-fin cut lines.
void createEngineIcon(lv_obj_t *parent) {
    IconCanvas ic = makeIconCanvas(parent, kEngineCx, kEngineCy);
    lv_color_t c = Colors::kEngineRed;
    fillRect(ic, 4, 20, 37, 38, c);   // lower body
    fillRect(ic, 9, 12, 31, 20, c);   // valve cover
    fillRect(ic, 12, 7, 17, 12, c);   // cylinder stub 1
    fillRect(ic, 22, 7, 27, 12, c);   // cylinder stub 2
    fillCircle(ic, 39, 30, 5, c);     // belt pulley (right)
    // Cooling fins: thin background cut lines across the lower body.
    fillRect(ic, 7, 26, 29, 26, Colors::kBg);
    fillRect(ic, 7, 30, 29, 30, Colors::kBg);
    fillRect(ic, 7, 34, 29, 34, Colors::kBg);
}

// Electric motor drawn as a cog/gear -- reads unambiguously as a machine and
// stays visually distinct from the WHEELS tyre (which is a spoked ring). Body
// disc + gear teeth + a punched-out centre hole + a shaft stub.
void createMotorIcon(lv_obj_t *parent) {
    IconCanvas ic = makeIconCanvas(parent, kMotorCx, kMotorCy);
    lv_color_t c = Colors::kAccentCyan;
    const int cx = kIconHalf, cy = kIconHalf;
    for (int k = 0; k < 8; k++) { // gear teeth
        float a = k * (kPi / 4.0f);
        thickLine(ic, cx + 12 * std::cos(a), cy + 12 * std::sin(a),
                  cx + 19 * std::cos(a), cy + 19 * std::sin(a), 2, c);
    }
    fillCircle(ic, cx, cy, 14, c);       // body
    fillCircle(ic, cx, cy, 6, Colors::kBg); // centre hole -> unmistakably a cog
    fillCircle(ic, cx, cy, 2, c);        // axle
}

// HV battery: outlined body + two terminals + internal cell dividers.
void createBatteryIcon(lv_obj_t *parent) {
    IconCanvas ic = makeIconCanvas(parent, kBatteryCx, kBatteryCy);
    lv_color_t c = Colors::kBatteryBlue;
    strokeRect(ic, 5, 14, 38, 38, 2, c);
    fillRect(ic, 12, 9, 17, 14, c);  // terminal 1
    fillRect(ic, 26, 9, 31, 14, c);  // terminal 2
    fillRect(ic, 16, 16, 17, 36, c); // cell divider 1
    fillRect(ic, 26, 16, 27, 36, c); // cell divider 2
}

// Driven wheel: thick tyre ring + hub + spokes.
void createWheelsIcon(lv_obj_t *parent) {
    IconCanvas ic = makeIconCanvas(parent, kWheelsCx, kWheelsCy);
    lv_color_t c = Colors::kText;
    const int cx = kIconHalf, cy = kIconHalf;
    ring(ic, cx, cy, 19, 12, c); // tyre
    fillCircle(ic, cx, cy, 5, c); // hub
    for (int k = 0; k < 5; k++) {
        float a = k * (2.0f * kPi / 5.0f) - kPi / 2.0f;
        thickLine(ic, cx + 5 * std::cos(a), cy + 5 * std::sin(a),
                  cx + 12 * std::cos(a), cy + 12 * std::sin(a), 1, c);
    }
}

// ---------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------
lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                    int16_t x, int16_t y, int16_t w, lv_text_align_t align) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

void createLabels(lv_obj_t *parent) {
    const int16_t vmid = kEngineCy - kLabelLineH / 2; // vertical-centre a label on the top row

    // ENGINE: to the LEFT of its icon, right-aligned so it hugs the icon.
    makeLabel(parent, "ENGINE", &dinnext_24_label, Colors::kText,
              static_cast<int16_t>(kEngineCx - kIconHalf - kEndpointGap - kLabelW), vmid, kLabelW,
              LV_TEXT_ALIGN_RIGHT);

    // MOTOR: centred ABOVE its icon (centred node, no room to a side).
    makeLabel(parent, "MOTOR", &dinnext_24_label, Colors::kText,
              static_cast<int16_t>(kMotorCx - kLabelW / 2),
              static_cast<int16_t>(kMotorCy - kIconHalf - 2 - kLabelLineH), kLabelW,
              LV_TEXT_ALIGN_CENTER);

    // BATTERY: to the RIGHT of its icon, left-aligned, with the live SOC %
    // number on a second line just below it.
    const int16_t battLabelX = kBatteryCx + kIconHalf + kEndpointGap;
    makeLabel(parent, "BATTERY", &dinnext_24_label, Colors::kText, battLabelX,
              static_cast<int16_t>(kBatteryCy - kIconHalf), kLabelW, LV_TEXT_ALIGN_LEFT);

    g_battValueLabel = makeLabel(parent, "--", &dinnext_26_battery, Colors::kText, battLabelX,
                                 static_cast<int16_t>(kBatteryCy + 2), 60, LV_TEXT_ALIGN_LEFT);
    g_battPctLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_battPctLabel, &dinnext_13_pct, 0);
    lv_obj_set_style_text_color(g_battPctLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_battPctLabel, "%");
    lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    // WHEELS: to the LEFT of the wheel, right-aligned, vertically centred on
    // it. The ENGINE->WHEELS arrow enters the wheel's lower-left
    // (kEngineWheelsEnterY), just below this label, so they don't collide.
    makeLabel(parent, "WHEELS", &dinnext_24_label, Colors::kText,
              static_cast<int16_t>(kWheelsCx - kIconHalf - kEndpointGap - kLabelW),
              static_cast<int16_t>(kWheelsCy - kLabelLineH / 2), kLabelW, LV_TEXT_ALIGN_RIGHT);
}

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

} // namespace

void build(lv_obj_t *parent) {
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
    // Bent 90 degrees: down from ENGINE, then across into the wheel's lower-left.
    FlowArrow::createBent(&g_engineWheelsArrow, root, kEngineCx, kEngineBottomY, kEngineCx,
                          kEngineWheelsEnterY, kWheelsLeftX, kEngineWheelsEnterY, kArrowThickness,
                          Colors::kEngineRed);

    createEngineIcon(root);
    createMotorIcon(root);
    createBatteryIcon(root);
    createWheelsIcon(root);
    createLabels(root);
}

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    float dtS = (g_lastUpdateMs == 0) ? 0.0f : (nowMs - g_lastUpdateMs) / 1000.0f;
    g_lastUpdateMs = nowMs;
    if (dtS > 0.25f) dtS = 0.25f;

    // Live battery %, matching the main view's rendering (same fonts).
    int battPct = static_cast<int>(state.battery_soc_pct + 0.5f);
    if (battPct < 0) battPct = 0;
    if (battPct > 100) battPct = 100;
    if (battPct != g_lastBattPct) {
        g_lastBattPct = battPct;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", battPct);
        lv_label_set_text(g_battValueLabel, buf);
        lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);
    }

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
