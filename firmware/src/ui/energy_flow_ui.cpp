#include "energy_flow_ui.h"
#include "colors.h"
#include "fonts/fonts.h"

#include <Arduino.h>
#include <cmath>

// Four nodes, four links, same topology the Prius-style diagram has always
// had -- but drawn with the widgets the rest of this cluster is drawn with
// instead of with a pixel plotter.
//
// What changed, and why each one mattered:
//
//   Nodes. They used to be 44x44 lv_canvas buffers with an engine block, a
//   battery and a spoked wheel plotted pixel by pixel. No anti-aliasing, in a
//   UI whose every other mark is a 4bpp DIN glyph or a clean rectangle -- and
//   each canvas was opaque, filled with the background colour, so it punched
//   a hole nothing could pass behind. They are now typographic: the node's
//   name in a hairline-bordered pill, which anti-aliases because LVGL draws
//   it, and which says what the node is without anyone having to recognise a
//   32px drawing of an engine.
//
//   Links. They used to be flow_arrow.cpp canvases, re-rasterised pixel by
//   pixel on every animation tick -- so expensive that the chevrons had to be
//   stepped at 2Hz to stay smooth on the board, which is exactly what made
//   them look like a 2005 MFD. A link is now a dim shaft with a bright
//   segment riding along it: the same shaft/highlight split flow_arrow drew,
//   except moving it is one lv_obj_set_pos, so it runs at the full UI rate
//   and costs less than the old 2Hz redraw did.
//
// The colour rules and deriveFlow() below are carried over unchanged -- the
// question of which way energy is moving was already answered correctly, and
// this is a change of drawing, not of meaning.
namespace EnergyFlowUi {

namespace {

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

// --- node geometry --------------------------------------------------------
// The top row keeps its ENGINE - MOTOR - BATTERY order and WHEELS stays
// centred below, so anyone used to the old screen reads this one the same
// way. Nodes are spread wider than before because a pill is wider than a
// 44px icon, and the gaps between them are the links.
constexpr int16_t kRowY = 48;
constexpr int16_t kWheelY = 134;
constexpr int16_t kEngineCx = 108;
constexpr int16_t kMotorCx = 320;
constexpr int16_t kBatteryCx = 532;
constexpr int16_t kPillH = 34;
constexpr int16_t kPillPadX = 30; // total horizontal padding around a node's text
constexpr int16_t kPillHalfH = kPillH / 2;

// --- link geometry --------------------------------------------------------
constexpr int16_t kActiveT = 10;   // matches the weight the old arrows had
constexpr int16_t kIdleT = 4;      // an idle link keeps the topology, not the emphasis
constexpr int16_t kHighlightLen = 30;
constexpr float kFlowSpeedPxPerS = 90.0f;

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

// ---------------------------------------------------------------------------
// A link is one or two axis-aligned legs walked in path order. Two legs is
// only ever the engine's mechanical route to the wheels, which goes down the
// left of the screen and then across. Splitting the highlight per leg is what
// lets it round that corner without jumping: the travelling segment is
// clipped against each leg's span, so it shortens into the corner on one leg
// and grows out of it on the other.
// ---------------------------------------------------------------------------
struct Leg {
    bool vertical;
    int16_t x;   // centre line for a vertical leg, start x for a horizontal one
    int16_t y;   // start y for a vertical leg, centre line for a horizontal one
    int16_t len; // always positive, measured in path order
    lv_obj_t *shaft = nullptr;
    lv_obj_t *highlight = nullptr;
};

struct Link {
    Leg legs[2];
    int legCount = 0;
    int16_t totalLen = 0;
    float phasePx = 0.0f; // distance the highlight's leading edge has travelled
    Dir shownDir = Dir::Off;
    lv_color_t shownColor = Colors::kFlowOff;
    bool styled = false;
};

Link g_engineMotor, g_motorBattery, g_motorWheels, g_engineWheels;

// A node is lit when at least one of its own links is carrying something.
// Without this the ENGINE pill sat there in full red with two dead grey
// stubs hanging off it, which says the opposite of what the links say.
struct Node {
    lv_obj_t *pill = nullptr;
    lv_obj_t *label = nullptr;
    lv_color_t color = Colors::kText;
    bool lit = true;
    bool styled = false;
};
Node g_engineNode, g_motorNode, g_batteryNode, g_wheelsNode;

uint32_t g_lastUpdateMs = 0;

int16_t textW(const char *txt, const lv_font_t *font) {
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return static_cast<int16_t>(sz.x);
}

int16_t pillHalfW(const char *txt) {
    return static_cast<int16_t>((textW(txt, &dinnext_24_label) + kPillPadX) / 2);
}

lv_obj_t *makeRect(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, lv_color_t color,
                   int16_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Node: its name inside a hairline pill. The fill is a wash of the node's own
// colour rather than a solid, for the same reason the efficiency screen's
// halves are washes -- a saturated block this size is too loud on a dash.
void makePill(lv_obj_t *parent, Node &node, int16_t cx, int16_t cy, const char *txt,
              lv_color_t color) {
    node.color = color;
    int16_t halfW = pillHalfW(txt);
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, static_cast<int16_t>(halfW * 2), kPillH);
    lv_obj_set_pos(o, static_cast<int16_t>(cx - halfW), static_cast<int16_t>(cy - kPillHalfH));
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_20, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, kPillHalfH, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &dinnext_24_label, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, static_cast<int16_t>(cx - textW(txt, &dinnext_24_label) / 2),
                   static_cast<int16_t>(cy - 9));

    node.pill = o;
    node.label = l;
}

void setNodeLit(Node &node, bool lit) {
    if (node.styled && lit == node.lit) return;
    node.styled = true;
    node.lit = lit;
    lv_color_t c = lit ? node.color : Colors::kFlowOff;
    lv_obj_set_style_border_color(node.pill, c, 0);
    lv_obj_set_style_bg_opa(node.pill, lit ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(node.label, c, 0);
}

void addLeg(lv_obj_t *parent, Link &link, bool vertical, int16_t x, int16_t y, int16_t len) {
    Leg &leg = link.legs[link.legCount];
    leg.vertical = vertical;
    leg.x = x;
    leg.y = y;
    leg.len = len;
    leg.shaft = makeRect(parent, 0, 0, 1, 1, Colors::kFlowOff, kIdleT / 2);
    leg.highlight = makeRect(parent, 0, 0, 1, 1, Colors::kFlowOff, kActiveT / 2);
    lv_obj_add_flag(leg.highlight, LV_OBJ_FLAG_HIDDEN);
    link.totalLen = static_cast<int16_t>(link.totalLen + len);
    link.legCount++;
}

// Lays a leg's shaft out at the given thickness. A bend is filled by having
// BOTH legs overrun the corner by half the thickness -- the leg arriving
// extends its end, the leg leaving extends its start -- so they overlap in a
// t-by-t square and the outer corner has no notch. Extending only one of them
// leaves a bite out of the bend, which at 10px is plainly visible.
void layoutShaft(const Leg &leg, int16_t t, bool extendEnd, bool extendStart) {
    int16_t head = extendStart ? static_cast<int16_t>(t / 2) : 0;
    int16_t tail = extendEnd ? static_cast<int16_t>(t / 2) : 0;
    if (leg.vertical) {
        lv_obj_set_size(leg.shaft, t, static_cast<int16_t>(leg.len + head + tail));
        lv_obj_set_pos(leg.shaft, static_cast<int16_t>(leg.x - t / 2),
                       static_cast<int16_t>(leg.y - head));
    } else {
        lv_obj_set_size(leg.shaft, static_cast<int16_t>(leg.len + head + tail), t);
        lv_obj_set_pos(leg.shaft, static_cast<int16_t>(leg.x - head),
                       static_cast<int16_t>(leg.y - t / 2));
    }
    lv_obj_set_style_radius(leg.shaft, static_cast<int16_t>(t / 2), 0);
}

// Places the travelling segment. lo/hi are the segment's span in the LINK's
// path coordinates; this clips them to one leg and hides the object when
// nothing of the segment falls on it.
void layoutHighlight(const Leg &leg, int16_t legStart, float lo, float hi, lv_color_t color) {
    float legLo = std::fmax(lo, static_cast<float>(legStart));
    float legHi = std::fmin(hi, static_cast<float>(legStart + leg.len));
    if (legHi - legLo < 1.0f) {
        lv_obj_add_flag(leg.highlight, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(leg.highlight, LV_OBJ_FLAG_HIDDEN);
    int16_t off = static_cast<int16_t>(legLo - legStart);
    int16_t len = static_cast<int16_t>(legHi - legLo);
    if (leg.vertical) {
        lv_obj_set_size(leg.highlight, kActiveT, len);
        lv_obj_set_pos(leg.highlight, static_cast<int16_t>(leg.x - kActiveT / 2),
                       static_cast<int16_t>(leg.y + off));
    } else {
        lv_obj_set_size(leg.highlight, len, kActiveT);
        lv_obj_set_pos(leg.highlight, static_cast<int16_t>(leg.x + off),
                       static_cast<int16_t>(leg.y - kActiveT / 2));
    }
    lv_obj_set_style_bg_color(leg.highlight, color, 0);
    lv_obj_set_style_shadow_color(leg.highlight, color, 0);
    lv_obj_set_style_shadow_width(leg.highlight, 14, 0);
    lv_obj_set_style_shadow_spread(leg.highlight, 1, 0);
    lv_obj_set_style_shadow_opa(leg.highlight, LV_OPA_50, 0);
}

void updateLink(Link &link, Dir dir, lv_color_t color, float dtS) {
    bool active = (dir != Dir::Off);

    // Shaft geometry and colour only change when the link's state does --
    // the per-frame work is moving the highlight, nothing else.
    if (!link.styled || dir != link.shownDir || color.full != link.shownColor.full) {
        link.styled = true;
        link.shownDir = dir;
        link.shownColor = color;
        int16_t t = active ? kActiveT : kIdleT;
        lv_color_t shaft = active ? lv_color_mix(color, Colors::kBg, 45) : Colors::kFlowOff;
        for (int i = 0; i < link.legCount; i++) {
            bool bendAhead = (link.legCount == 2 && i == 0);
            bool bendBehind = (link.legCount == 2 && i == 1);
            layoutShaft(link.legs[i], t, bendAhead, bendBehind);
            lv_obj_set_style_bg_color(link.legs[i].shaft, shaft, 0);
        }
        // Restart the run on any state change, reversals included: carrying
        // the phase across a flip makes the segment appear to bounce back off
        // the node it was heading for, where restarting reads as "this is now
        // going the other way, from the source".
        link.phasePx = 0.0f;
        if (!active) {
            for (int i = 0; i < link.legCount; i++)
                lv_obj_add_flag(link.legs[i].highlight, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!active) return;

    // One cycle carries the segment from fully off one end to fully off the
    // other, so it enters and leaves instead of popping into existence.
    float cycle = static_cast<float>(link.totalLen) + kHighlightLen;
    link.phasePx += kFlowSpeedPxPerS * dtS;
    if (link.phasePx >= cycle) link.phasePx = std::fmod(link.phasePx, cycle);

    float lead = (dir == Dir::Forward) ? link.phasePx : cycle - link.phasePx;
    float lo = lead - kHighlightLen;
    float hi = lead;

    int16_t legStart = 0;
    for (int i = 0; i < link.legCount; i++) {
        layoutHighlight(link.legs[i], legStart, lo, hi, color);
        legStart = static_cast<int16_t>(legStart + link.legs[i].len);
    }
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

    int16_t engHalf = pillHalfW("ENGINE");
    int16_t motHalf = pillHalfW("MOTOR");
    int16_t batHalf = pillHalfW("BATTERY");
    int16_t whlHalf = pillHalfW("WHEELS");

    // Links first so the pills draw over the ends of their shafts.
    addLeg(root, g_engineMotor, false, static_cast<int16_t>(kEngineCx + engHalf), kRowY,
             static_cast<int16_t>((kMotorCx - motHalf) - (kEngineCx + engHalf)));
    addLeg(root, g_motorBattery, false, static_cast<int16_t>(kMotorCx + motHalf), kRowY,
             static_cast<int16_t>((kBatteryCx - batHalf) - (kMotorCx + motHalf)));
    addLeg(root, g_motorWheels, true, kMotorCx, static_cast<int16_t>(kRowY + kPillHalfH),
             static_cast<int16_t>((kWheelY - kPillHalfH) - (kRowY + kPillHalfH)));

    // ENGINE -> WHEELS is the one bent route: down the left edge, then across
    // into the wheels' left side.
    addLeg(root, g_engineWheels, true, kEngineCx, static_cast<int16_t>(kRowY + kPillHalfH),
             static_cast<int16_t>(kWheelY - (kRowY + kPillHalfH)));
    addLeg(root, g_engineWheels, false, kEngineCx, kWheelY,
                 static_cast<int16_t>((kMotorCx - whlHalf) - kEngineCx));

    makePill(root, g_engineNode, kEngineCx, kRowY, "ENGINE", Colors::kEngineRed);
    makePill(root, g_motorNode, kMotorCx, kRowY, "MOTOR", Colors::kAccentCyan);
    makePill(root, g_batteryNode, kBatteryCx, kRowY, "BATTERY", Colors::kBatteryBlue);
    makePill(root, g_wheelsNode, kMotorCx, kWheelY, "WHEELS", Colors::kText);
}

void update(const VehicleState &state) {
    uint32_t nowMs = millis();
    float dtS = (g_lastUpdateMs == 0) ? 0.0f : (nowMs - g_lastUpdateMs) / 1000.0f;
    g_lastUpdateMs = nowMs;
    if (dtS > 0.25f) dtS = 0.25f; // a pause must not teleport the highlights

    FlowState f = deriveFlow(state);

    // Colour rules carried over from the previous screen: red is always the
    // engine's mechanical energy, green is always energy being recovered, and
    // the battery link takes the pack's own blue when it is the one giving.
    updateLink(g_engineMotor, f.engineMotor, Colors::kEngineRed, dtS);
    updateLink(g_engineWheels, f.engineWheels, Colors::kEngineRed, dtS);
    updateLink(g_motorBattery, f.motorBattery,
               f.motorBattery == Dir::Forward ? Colors::kChgGreen : Colors::kBatteryBlue, dtS);
    updateLink(g_motorWheels, f.motorWheels,
               f.motorWheels == Dir::Forward ? Colors::kAccentCyan : Colors::kChgGreen, dtS);

    bool engMot = f.engineMotor != Dir::Off;
    bool engWhl = f.engineWheels != Dir::Off;
    bool motBat = f.motorBattery != Dir::Off;
    bool motWhl = f.motorWheels != Dir::Off;
    setNodeLit(g_engineNode, engMot || engWhl);
    setNodeLit(g_motorNode, engMot || motBat || motWhl);
    setNodeLit(g_batteryNode, motBat);
    setNodeLit(g_wheelsNode, motWhl || engWhl);
}

} // namespace EnergyFlowUi
