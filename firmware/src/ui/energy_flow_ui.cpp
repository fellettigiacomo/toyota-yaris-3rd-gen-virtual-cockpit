#include "energy_flow_ui.h"
#include "colors.h"
#include "fonts/fonts.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// The engine on the left of the cockpit's centre column, the electric side on
// the right. Energy travels as particles through a funnel: three lanes that
// start spread out at the source and meet at the speed, which stands in for
// the wheels. When the pack is being charged the electric side runs the other
// way, out of the speed, in the charge colour.
//
// The particles are drawn by one draw callback per side rather than as LVGL
// objects: eighteen moving dots would cost eighteen objects' worth of the
// 48 KB LVGL heap, and with full_refresh every frame is redrawn anyway.
namespace EnergyFlowUi {

namespace {

// --- flow derivation (unchanged) -----------------------------------------
constexpr float kStopEnterKph = 2.0f;
constexpr float kStopExitKph = 4.0f;
bool g_stoppedLatched = true;

constexpr float kDecelHsiThreshold = 5.0f;
constexpr float kPwrHsiThreshold = 5.0f;
constexpr float kPwrHsiConfident = 25.0f;
constexpr int8_t kAccelDemandDecelThreshold = -8;
constexpr float kSocTrendEps = 0.05f;

enum class Dir : uint8_t { Off, Forward, Reverse };

struct FlowState {
    Dir engineMotor = Dir::Off;
    Dir motorBattery = Dir::Off;
    Dir motorWheels = Dir::Off;
    Dir engineWheels = Dir::Off;
};

// The qualitative "brain": maps VehicleState onto the 4 link directions.
// See re/docs/signal_findings.md for the underlying signal semantics.
// How long the car's own flags stay trusted after the last 0x3B6 frame. The
// message comes once a second; three missed frames means it is gone.
constexpr uint32_t kEnergyFlagsMaxAgeMs = 3500;

// The car broadcasts the flow directions its own energy monitor draws
// (0x3B6). When those are fresh they are used as they are; the inference
// below remains as the fallback for a bus where the message never shows up,
// and for the one flow the flags do not carry: engine braking in B.
bool flowFromCar(const VehicleState &state, bool engineBraking, FlowState &f) {
    if (!state.energy_flags_seen) return false;
    if (millis() - state.energy_flags_ms > kEnergyFlagsMaxAgeMs) return false;

    if (engineBraking) {
        f.engineWheels = Dir::Reverse; // WHEELS -> ENGINE
    } else if (state.ef_engine_to_wheels) {
        f.engineWheels = Dir::Forward; // ENGINE -> WHEELS
    }

    if (state.ef_wheels_to_motor) {
        f.motorWheels = Dir::Reverse; // WHEELS -> MOTOR (regen)
    } else if (state.ef_motor_to_wheels) {
        f.motorWheels = Dir::Forward; // MOTOR -> WHEELS
    }

    if (state.ef_batt_charging) {
        f.motorBattery = Dir::Forward; // MOTOR -> BATTERY
    } else if (state.ef_batt_discharging) {
        f.motorBattery = Dir::Reverse; // BATTERY -> MOTOR
    }

    // The engine feeds the motor-generators whenever it runs and the battery
    // is being charged by something other than the wheels.
    if (state.ef_engine_running && state.ef_batt_charging && !state.ef_wheels_to_motor) {
        f.engineMotor = Dir::Forward; // ENGINE -> MOTOR
    }
    return true;
}

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
    bool powering =
        state.ice_running &&
        (state.hsi_power >= kPwrHsiConfident || (state.hsi_power >= kPwrHsiThreshold && socFalling));

    FlowState f;

    if (flowFromCar(state, state.gear == Gear::B && decelerating && !isStopped, f)) return f;

    if (state.gear == Gear::B && decelerating && !isStopped) {
        f.engineWheels = Dir::Reverse; // WHEELS -> ENGINE (engine braking)
    } else if (state.ice_running && !state.ev_drive && state.gear != Gear::R && !isStopped &&
               !decelerating) {
        f.engineWheels = Dir::Forward; // ENGINE -> WHEELS
    }

    if (state.ice_running && socRising && !decelerating) {
        f.engineMotor = Dir::Forward; // ENGINE -> MOTOR (charge)
    }

    if (decelerating && !isStopped) {
        f.motorBattery = Dir::Forward; // MOTOR -> BATTERY (regen)
        f.motorWheels = Dir::Reverse;  // WHEELS -> MOTOR (regen)
    } else {
        if (f.engineMotor == Dir::Forward) {
            f.motorBattery = Dir::Forward; // MOTOR -> BATTERY (engine charge)
        } else if (state.ev_drive || state.gear == Gear::R || powering) {
            f.motorBattery = Dir::Reverse; // BATTERY -> MOTOR (assist)
        }
        if (state.ev_drive || state.gear == Gear::R || powering) {
            f.motorWheels = Dir::Forward; // MOTOR -> WHEELS
        }
    }

    return f;
}

// --- readouts ---------------------------------------------------------------
constexpr int16_t kLeftX = 24;
constexpr int16_t kRightX = 616;
constexpr int16_t kTagBaseline = 61;
constexpr int16_t kValueBaseline = 89;
constexpr int16_t kUnitGap = 4;
constexpr lv_opa_t kDimmedOpa = 102;
constexpr float kIdleAmps = 1.0f;

lv_obj_t *g_engineTag = nullptr;
lv_obj_t *g_engineValue = nullptr;
lv_obj_t *g_engineUnit = nullptr;
lv_obj_t *g_electricTag = nullptr;
lv_obj_t *g_ampsValue = nullptr;
lv_obj_t *g_ampsUnit = nullptr;

// --- funnels ----------------------------------------------------------------
enum class Flow : uint8_t { Off, In, Out };

constexpr float kRunPx = 124.0f;   // mouth to speed
constexpr float kSpreadPx = 24.0f; // outer lanes' offset at the mouth
constexpr float kFunnelY = 72.0f;  // the speed's vertical centre, where the lanes meet
constexpr int16_t kFunnelW = 150;
constexpr int16_t kFunnelH = 64;
constexpr int16_t kFunnelTop = 40;

struct Lane {
    float spread;
    lv_coord_t radius;
    float opa;
    float offset[3]; // phase of each of the lane's three particles
};

const Lane kLanes[] = {
    {0.0f, 3, 1.0f, {0.0f, 1.0f / 3.0f, 2.0f / 3.0f}},
    {-kSpreadPx, 2, 0.75f, {1.0f / 6.0f, 0.5f, 5.0f / 6.0f}},
    {kSpreadPx, 2, 0.75f, {1.0f / 15.0f, 0.4f, 11.0f / 15.0f}},
};

struct Funnel {
    lv_obj_t *obj;
    int16_t objX;
    float mouthX;  // particle centre at the mouth, screen x
    float towards; // +1 if the speed is to the right of the mouth, -1 if to the left
    Flow flow;
    lv_color_t color;
    float periodS;
    float phase;   // 0..1
};

Funnel g_engineFunnel = {nullptr, 100, 111.0f, 1.0f, Flow::Off, Colors::kEngineText, 1.5f, 0.0f};
Funnel g_electricFunnel = {nullptr, 390, 533.0f, -1.0f, Flow::Off, Colors::kAccentCyan, 1.3f, 0.0f};

uint32_t g_lastUpdateMs = 0;

// Slow out of the mouth, fast into the speed: a stand-in for the design's
// cubic-bezier(.4, 0, .9, 1).
float ease(float k) {
    return k * k * (1.6f - 0.6f * k);
}

void drawDot(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t &dsc, float cx, float cy, lv_coord_t r, lv_opa_t opa) {
    if (opa <= LV_OPA_MIN) return;
    lv_coord_t x1 = static_cast<lv_coord_t>(lroundf(cx)) - r;
    lv_coord_t y1 = static_cast<lv_coord_t>(lroundf(cy)) - r;
    lv_area_t a = {x1, y1, static_cast<lv_coord_t>(x1 + 2 * r - 1), static_cast<lv_coord_t>(y1 + 2 * r - 1)};
    dsc.bg_opa = opa;
    lv_draw_rect(ctx, &dsc, &a);
}

void drawFunnel(lv_event_t *e) {
    const Funnel *f = static_cast<const Funnel *>(lv_event_get_user_data(e));
    if (f->flow == Flow::Off) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_opa_t opa = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    if (opa <= LV_OPA_MIN) return;

    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    // The container fades in with a small translate_y, so place against the
    // object's live coordinates rather than its nominal position.
    float dx = static_cast<float>(coords.x1 - f->objX);
    float dy = static_cast<float>(coords.y1 - kFunnelTop);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = f->color;

    for (const Lane &lane : kLanes) {
        for (float offset : lane.offset) {
            float t = f->phase + offset;
            t -= floorf(t);
            float k = f->flow == Flow::In ? t : 1.0f - t;
            float p = ease(k);
            float envelope = k < 0.15f ? k / 0.15f : k > 0.85f ? (1.0f - k) / 0.15f : 1.0f;
            float cx = f->mouthX + f->towards * kRunPx * p + dx;
            float cy = kFunnelY + lane.spread * (1.0f - p) + dy;
            float a = opa * lane.opa * envelope;
            if (lane.spread == 0.0f) {
                drawDot(ctx, dsc, cx, cy, lane.radius + 3, static_cast<lv_opa_t>(a * 0.25f));
            }
            drawDot(ctx, dsc, cx, cy, lane.radius, static_cast<lv_opa_t>(a));
        }
    }
}

void buildFunnel(lv_obj_t *parent, Funnel &f) {
    f.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(f.obj);
    lv_obj_set_size(f.obj, kFunnelW, kFunnelH);
    lv_obj_set_pos(f.obj, f.objX, kFunnelTop);
    lv_obj_clear_flag(f.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(f.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(f.obj, drawFunnel, LV_EVENT_DRAW_MAIN, &f);
}

void setFlow(Funnel &f, Flow flow, lv_color_t color, float periodS, float dtS) {
    if (flow != f.flow || color.full != f.color.full) {
        f.flow = flow;
        f.color = color;
        lv_obj_invalidate(f.obj);
    }
    f.periodS = periodS;
    if (flow == Flow::Off) return;
    f.phase += dtS / f.periodS;
    f.phase -= floorf(f.phase);
    lv_obj_invalidate(f.obj);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int16_t letterSpace,
                    const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, letterSpace, 0);
    lv_label_set_text(l, txt);
    return l;
}

int16_t textW(lv_obj_t *label) {
    lv_point_t sz;
    lv_txt_get_size(&sz, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                    lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    return static_cast<int16_t>(sz.x);
}

void placeOnBaseline(lv_obj_t *label, int16_t x, int16_t baseline) {
    const lv_font_t *f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_obj_set_pos(label, x, static_cast<int16_t>(baseline - (f->line_height - f->base_line)));
}

void setText(lv_obj_t *label, const char *txt) {
    if (strcmp(lv_label_get_text(label), txt) != 0) lv_label_set_text(label, txt);
}

void setDimmed(std::initializer_list<lv_obj_t *> labels, bool dimmed) {
    for (lv_obj_t *l : labels) lv_obj_set_style_opa(l, dimmed ? kDimmedOpa : LV_OPA_COVER, 0);
}

} // namespace

void build(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 640, 172);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    buildFunnel(root, g_engineFunnel);
    buildFunnel(root, g_electricFunnel);

    g_engineTag = makeLabel(root, &spacegrotesk_13_tag, Colors::kEngineText, 1, "ENGINE");
    placeOnBaseline(g_engineTag, kLeftX, kTagBaseline);
    g_engineValue = makeLabel(root, &spacegrotesk_25_value, Colors::kText, 0, "0.0");
    placeOnBaseline(g_engineValue, kLeftX, kValueBaseline);
    g_engineUnit = makeLabel(root, &spacegrotesk_12_label, Colors::kMutedText, 1, "L/100");

    g_electricTag = makeLabel(root, &spacegrotesk_13_tag, Colors::kAccentCyan, 1, "MOTOR");
    g_ampsUnit = makeLabel(root, &spacegrotesk_12_label, Colors::kMutedText, 1, "A");
    placeOnBaseline(g_ampsUnit, static_cast<int16_t>(kRightX - textW(g_ampsUnit)), kValueBaseline);
    g_ampsValue = makeLabel(root, &spacegrotesk_25_value, Colors::kText, 0, "0");
}

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    float dtS = (g_lastUpdateMs == 0) ? 0.0f : (nowMs - g_lastUpdateMs) / 1000.0f;
    g_lastUpdateMs = nowMs;
    if (dtS > 0.25f) dtS = 0.25f; // a pause must not make the particles jump

    FlowState f = deriveFlow(state);

    // Left: the engine pushes into the wheels (or into the generator, which
    // the funnel draws the same way); in B on a lift-off the wheels drive it.
    Flow engine = Flow::Off;
    if (f.engineWheels == Dir::Reverse) {
        engine = Flow::Out;
    } else if (f.engineWheels == Dir::Forward || f.engineMotor == Dir::Forward) {
        engine = Flow::In;
    }

    // Right: the motor driving the wheels, or the pack being charged -- by
    // regen, or by the engine through the generator.
    Flow electric = Flow::Off;
    if (f.motorWheels == Dir::Forward) {
        electric = Flow::In;
    } else if (f.motorWheels == Dir::Reverse || f.motorBattery == Dir::Forward) {
        electric = Flow::Out;
    } else if (f.motorBattery == Dir::Reverse) {
        electric = Flow::In;
    }
    bool charging = electric == Flow::Out;

    setFlow(g_engineFunnel, engine, Colors::kEngineText, 1.5f, dtS);
    setFlow(g_electricFunnel, electric, charging ? Colors::kChgGreen : Colors::kAccentCyan,
            charging ? 1.4f : 1.3f, dtS);

    char buf[16];
    bool burning = state.fuel_flow_mm3s > 0.0f;
    if (!burning) {
        snprintf(buf, sizeof(buf), "0.0");
    } else if (state.inst_consumption_l100 < 0.0f) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        float v = state.inst_consumption_l100 > 99.9f ? 99.9f : state.inst_consumption_l100;
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    }
    setText(g_engineValue, buf);
    placeOnBaseline(g_engineUnit, static_cast<int16_t>(kLeftX + textW(g_engineValue) + kUnitGap),
                    kValueBaseline);
    setDimmed({g_engineTag, g_engineValue, g_engineUnit}, engine == Flow::Off && !burning);

    setText(g_electricTag, charging ? "CHARGE" : "MOTOR");
    lv_obj_set_style_text_color(g_electricTag, charging ? Colors::kChgGreen : Colors::kAccentCyan, 0);
    placeOnBaseline(g_electricTag, static_cast<int16_t>(kRightX - textW(g_electricTag)), kTagBaseline);

    float amps = fabsf(state.battery_current_a);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(amps + 0.5f));
    setText(g_ampsValue, buf);
    placeOnBaseline(g_ampsValue,
                    static_cast<int16_t>(kRightX - textW(g_ampsUnit) - kUnitGap - textW(g_ampsValue)),
                    kValueBaseline);
    setDimmed({g_electricTag, g_ampsValue, g_ampsUnit}, electric == Flow::Off && amps < kIdleAmps);
}

} // namespace EnergyFlowUi
