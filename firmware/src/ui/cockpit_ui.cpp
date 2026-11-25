#include "cockpit_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "accel_timer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <lvgl.h>
#include <cstring>

namespace CockpitUi {

namespace {

// --- Layout constants, matching Cockpit.dc.html's 640x172 canvas ---
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;
// Root screen is 640x172 (center y=86), so an absolute target_y maps to an
// LV_ALIGN_CENTER y-offset of (target_y - 86). Speed + the RPM/EV row below
// it are treated as one vertically-centered block. The horizontal CHG/PWR
// bar that used to occupy y=0..16 is gone (power now lives in the right-hand
// vertical column, see below), so the block is centered on the screen's full
// height rather than on the band left under that bar -- every offset in
// createCenterGroup() is 8px higher than it was when the bar existed. Gear
// and units keep their own fixed anchor relative to the speed number.

// Side columns. Both are inset by the same margin from their own screen edge,
// and both gauges are the same size, so the cluster reads as a symmetric pair:
// HV battery charge on the left, instantaneous CHG/PWR power flow on the right.
constexpr int16_t kSideMargin = 18;

// Vertical tick gauge geometry, shared by both columns. Equal top/bottom
// margin, with the gauge flush to its column's own outer screen edge; the
// icon/caption and the numeric readout sit on the inner side, toward screen
// center.
constexpr int16_t kGaugeW = 14;
constexpr int16_t kGaugeMargin = 16;
constexpr int16_t kGaugeY = kGaugeMargin;                        // 16
constexpr int16_t kGaugeBottom = kScreenH - kGaugeMargin;        // 156
constexpr int16_t kGaugeH = kGaugeBottom - kGaugeY;              // 140

// Shared "bottom row" Y for both columns' numeric value, bottom-aligned with
// the gauges themselves.
constexpr int16_t kSideValueY = kGaugeBottom - 18; // 18 = spacegrotesk_25_battery's line_height
// Each column's "top row" sits just above that: the battery icon on the left
// (12px tall), the CHG/PWR caption on the right (12px line height).
constexpr int16_t kSideIconY = kSideValueY - 3 - 12;    // 3px gap above the value row
constexpr int16_t kSideCaptionY = kSideValueY - 3 - 12; // same gap, spacegrotesk_14_chgpwr's line_height

// Left column: HV battery gauge, flush to the left screen edge.
constexpr int16_t kBattTickX = kSideMargin; // 18
// Gap from the gauge's right edge to the text's left edge equals the screen's
// own side margin, for a consistent visual rhythm.
constexpr int16_t kBattInnerX = kBattTickX + kGaugeW + kSideMargin; // 50; nub/value sit 2px further left (toward the ticks)

// Right column: CHG/PWR power-flow gauge, mirroring the battery column --
// flush to the right screen edge, with its readout on the inner side.
constexpr int16_t kPwrTickX = kScreenW - kSideMargin - kGaugeW; // 608
// Right edge of the readout, one side margin in from the gauge. The text is
// right-aligned to it, so it grows away from the gauge -- mirroring the
// battery's left-aligned readout, which grows away from its own gauge.
constexpr int16_t kPwrTextRight = kPwrTickX - kSideMargin;      // 590
// Fixed-width right-aligned boxes: wide enough for "100" at 26px / "PWR" at
// 14px, and transparent, so the surplus width on their left costs nothing.
constexpr int16_t kPwrTextBoxW = 64;

// Past this much fill the PWR gauge goes red instead of white -- the "you are
// flat out" cue. kPwrRedlineHyst is how far the bar has to fall back below the
// threshold before it returns to white: without it, a pedal held right at the
// boundary would strobe the gauge between the two colors every sync tick.
constexpr int kPwrRedlinePct = 75;
constexpr int kPwrRedlineHyst = 3;

lv_obj_t *g_battBar = nullptr;
lv_obj_t *g_battValueLabel = nullptr;
lv_obj_t *g_battPctLabel = nullptr;

lv_obj_t *g_pwrBar = nullptr;
lv_obj_t *g_pwrValueLabel = nullptr;
lv_obj_t *g_pwrPctLabel = nullptr;
lv_obj_t *g_pwrCaptionLabel = nullptr;

lv_obj_t *g_battIcon = nullptr;
lv_obj_t *g_battNub = nullptr;
constexpr int kTickCount = 11;
lv_obj_t *g_battTicks[kTickCount] = {};
lv_obj_t *g_pwrTicks[kTickCount] = {};
// What the power gauge is currently painted as. Tracked so update() only
// restyles on an actual zone change -- a style change invalidates the object,
// and a zone holds for many frames at a time.
enum class PowerZone { Chg, Pwr, PwrRedline };
PowerZone g_pwrZone = PowerZone::Pwr;

// Start-up sweep (see startSweep()): up, down, then a short ease onto the
// live reading instead of a jump from 0 to it.
constexpr uint32_t kSweepUpMs = 650;
constexpr uint32_t kSweepDownMs = 650;
constexpr uint32_t kSweepSettleMs = 350;
bool g_sweeping = false;
uint32_t g_sweepStartMs = 0;

float easeInOut(float p) {
    return p < 0.5f ? 2.0f * p * p : 1.0f - 2.0f * (1.0f - p) * (1.0f - p);
}

// What a gauge shows during the sweep, given its live value. Returns false
// once the sweep is over (and clears it), leaving `pct` untouched.
bool sweepValue(uint32_t t, int live, int &pct) {
    float v;
    if (t < kSweepUpMs) {
        v = 100.0f * easeInOut(static_cast<float>(t) / kSweepUpMs);
    } else if ((t -= kSweepUpMs) < kSweepDownMs) {
        v = 100.0f * (1.0f - easeInOut(static_cast<float>(t) / kSweepDownMs));
    } else if ((t -= kSweepDownMs) < kSweepSettleMs) {
        v = live * easeInOut(static_cast<float>(t) / kSweepSettleMs);
    } else {
        return false;
    }
    pct = static_cast<int>(v + 0.5f);
    return true;
}

lv_color_t zoneColor(PowerZone z) {
    switch (z) {
        case PowerZone::Chg: return Colors::kChgGreen;
        case PowerZone::Pwr: return Colors::kPwrWhite;
        case PowerZone::PwrRedline: return Colors::kPwrRed;
    }
    return Colors::kPwrWhite;
}

lv_obj_t *g_speedLabel = nullptr;
lv_obj_t *g_gearLabel = nullptr;
lv_obj_t *g_unitsLabel = nullptr;
lv_obj_t *g_rpmLabel = nullptr;
lv_obj_t *g_evLabel = nullptr;
lv_obj_t *g_dumpLabel = nullptr; // replaces the RPM/EV row while a USB CAN dump streams

// 0-50/0-100 timer readout: shares the gear letter's slot (same anchor,
// exactly one of the two is visible at a time), see AccelTimer.
lv_obj_t *g_accelTimeLabel = nullptr;  // "6.55" -- elapsed seconds
lv_obj_t *g_accelThresholdLabel = nullptr; // "0-50" / "0-100" -- which threshold

char gearChar(Gear g) {
    switch (g) {
        case Gear::P: return 'P';
        case Gear::R: return 'R';
        case Gear::N: return 'N';
        case Gear::D: return 'D';
        case Gear::B: return 'B';
    }
    return 'P';
}

// Vertical tick gauge shared by both side columns: an lv_bar (auto-oriented
// vertical since h > w, filling bottom-up by default -- exactly the spec's
// behavior, no extra code needed for the fill direction) with 11 static tick
// marks layered on top, evenly spaced, drawn once.
lv_obj_t *createTickGauge(lv_obj_t *parent, int16_t x, lv_color_t color, lv_obj_t **ticks) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, kGaugeW, kGaugeH);
    lv_obj_set_pos(bar, x, kGaugeY);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, Colors::kBarTrack, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(bar, 10, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_spread(bar, 1, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_60, LV_PART_INDICATOR);

    for (int i = 0; i < kTickCount; i++) {
        lv_obj_t *tick = lv_obj_create(parent);
        ticks[i] = tick;
        lv_obj_remove_style_all(tick);
        lv_obj_set_size(tick, 9, 1);
        int16_t ty = kGaugeY + static_cast<int16_t>(i * (kGaugeH - 1) / 10);
        lv_obj_set_pos(tick, x + (kGaugeW - 9) / 2, ty);
        lv_obj_set_style_bg_color(tick, Colors::kTickDim, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
    }
    return bar;
}

void createBatteryGauge(lv_obj_t *parent) {
    // Icon + numeric readout sit at the BOTTOM of the column, aligned with
    // the gauge's own bottom edge (kSideValueY/kSideIconY). The tick gauge is
    // flush to the column's outer/left edge (kBattTickX); icon/nub/value sit
    // on the inner side (kBattInnerX), toward screen center, with the nub
    // pointing back toward the ticks.
    lv_obj_t *icon = lv_obj_create(parent);
    g_battIcon = icon;
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 16, 12);
    lv_obj_set_pos(icon, kBattInnerX, kSideIconY);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(icon, Colors::kBatteryBlue, 0);
    lv_obj_set_style_border_width(icon, 2, 0);
    lv_obj_set_style_radius(icon, 1, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nub = lv_obj_create(parent);
    g_battNub = nub;
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 2, 4);
    lv_obj_set_pos(nub, kBattInnerX - 2, kSideIconY + 5);
    lv_obj_set_style_bg_color(nub, Colors::kBatteryBlue, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_clear_flag(nub, LV_OBJ_FLAG_SCROLLABLE);

    // Numeric readout: value (26px bold) + "%" (13px semi-bold, muted).
    g_battValueLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_battValueLabel, &spacegrotesk_25_battery, 0);
    lv_obj_set_style_text_color(g_battValueLabel, Colors::kText, 0);
    lv_label_set_text(g_battValueLabel, "--");
    lv_obj_set_pos(g_battValueLabel, kBattInnerX - 2, kSideValueY);

    g_battPctLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_battPctLabel, &spacegrotesk_13_pct, 0);
    lv_obj_set_style_text_color(g_battPctLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_battPctLabel, "%");
    lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    g_battBar = createTickGauge(parent, kBattTickX, Colors::kBatteryBlue, g_battTicks);
}

void createPowerGauge(lv_obj_t *parent) {
    // Mirror image of the battery column: gauge flush to the right screen
    // edge, caption + numeric readout on the inner side, both right-aligned
    // so their right edge lands on kPwrTextRight regardless of digit count
    // (the battery's are left-aligned off a fixed left edge, same idea
    // mirrored). The gauge fills white under power and green under charge --
    // update() swaps the fill/caption color when hsi_power changes sign.
    //
    // The value box's right edge is pulled in by the "%" suffix's own width
    // plus its 2px gap, so that "<value> %" as a group ends on kPwrTextRight.
    // The suffix's width is measured from the font rather than hardcoded, and
    // the whole arrangement is static -- update() only ever sets the text.
    lv_point_t pctSize;
    lv_txt_get_size(&pctSize, "%", &spacegrotesk_13_pct, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int16_t valueBoxX = kPwrTextRight - 2 - static_cast<int16_t>(pctSize.x) - kPwrTextBoxW;

    g_pwrCaptionLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pwrCaptionLabel, &spacegrotesk_14_chgpwr, 0);
    lv_obj_set_style_text_letter_space(g_pwrCaptionLabel, 1, 0);
    lv_obj_set_style_text_color(g_pwrCaptionLabel, Colors::kPwrWhite, 0);
    lv_obj_set_width(g_pwrCaptionLabel, kPwrTextBoxW);
    lv_obj_set_style_text_align(g_pwrCaptionLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_pwrCaptionLabel, "PWR");
    lv_obj_set_pos(g_pwrCaptionLabel, kPwrTextRight - kPwrTextBoxW, kSideCaptionY);

    g_pwrValueLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pwrValueLabel, &spacegrotesk_25_battery, 0);
    lv_obj_set_style_text_color(g_pwrValueLabel, Colors::kText, 0);
    lv_obj_set_width(g_pwrValueLabel, kPwrTextBoxW);
    lv_obj_set_style_text_align(g_pwrValueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_pwrValueLabel, "--");
    lv_obj_set_pos(g_pwrValueLabel, valueBoxX, kSideValueY);

    g_pwrPctLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pwrPctLabel, &spacegrotesk_13_pct, 0);
    lv_obj_set_style_text_color(g_pwrPctLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_pwrPctLabel, "%");
    lv_obj_align_to(g_pwrPctLabel, g_pwrValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    g_pwrBar = createTickGauge(parent, kPwrTickX, Colors::kPwrWhite, g_pwrTicks);
    g_pwrZone = PowerZone::Pwr;
}

void createCenterGroup(lv_obj_t *parent) {
    // Speed + the RPM/EV row form one vertically-centered block (see the
    // layout comment above): speed dy=-19, RPM/EV dy=+50.
    g_speedLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_speedLabel, &spacegrotesk_106_speed, 0);
    lv_obj_set_style_text_color(g_speedLabel, Colors::kText, 0);
    lv_obj_set_style_text_letter_space(g_speedLabel, -3, 0);
    lv_label_set_text(g_speedLabel, "0");
    lv_obj_align(g_speedLabel, LV_ALIGN_CENTER, 0, -19);

    // Gear keeps its own fixed anchor (dy=-4), holding the same offset
    // relative to the speed number it flanks.
    g_gearLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_gearLabel, &spacegrotesk_39_gear, 0);
    lv_obj_set_style_text_color(g_gearLabel, Colors::kAccentCyan, 0);
    lv_label_set_text(g_gearLabel, "P");
    lv_obj_align(g_gearLabel, LV_ALIGN_CENTER, -158, -4);

    // 0-50/0-100 timer readout, sharing the gear letter's anchor (dx=-158) --
    // hidden by default, swapped in for the gear letter by update() while
    // AccelTimer has a result to show. Two stacked labels, same centering
    // trick the RPM/EV row already uses one anchor over. Both lines use the
    // same 39px size as the gear letter itself (spacegrotesk_39_gear's
    // line_height is 28px) for readability at a glance. Centered on the gear
    // letter's own anchor (dy=-4): seconds on top (dy=-22), threshold caption
    // below (dy=14), 36px apart so the two 28px-tall lines don't touch.
    g_accelTimeLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_accelTimeLabel, &spacegrotesk_39_accel_time, 0);
    lv_obj_set_style_text_color(g_accelTimeLabel, Colors::kAccentCyan, 0);
    lv_label_set_text(g_accelTimeLabel, "0.00");
    lv_obj_align(g_accelTimeLabel, LV_ALIGN_CENTER, -158, -22);
    lv_obj_add_flag(g_accelTimeLabel, LV_OBJ_FLAG_HIDDEN);

    g_accelThresholdLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_accelThresholdLabel, &spacegrotesk_39_accel_label, 0);
    lv_obj_set_style_text_color(g_accelThresholdLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_accelThresholdLabel, "0-50");
    lv_obj_align(g_accelThresholdLabel, LV_ALIGN_CENTER, -158, 14);
    lv_obj_add_flag(g_accelThresholdLabel, LV_OBJ_FLAG_HIDDEN);

    g_unitsLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_unitsLabel, &spacegrotesk_29_units, 0);
    lv_obj_set_style_text_color(g_unitsLabel, Colors::kAccentCyan, 0);
    lv_obj_set_style_text_letter_space(g_unitsLabel, 2, 0);
    lv_label_set_text(g_unitsLabel, "KM/H");
    lv_obj_align(g_unitsLabel, LV_ALIGN_CENTER, 168, -4);

    // RPM row and EV row share the same anchor; exactly one of the two is
    // visible at a time based on ev_drive.
    g_rpmLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_rpmLabel, &spacegrotesk_25_rpm, 0);
    lv_obj_set_style_text_color(g_rpmLabel, Colors::kTextBright, 0);
    lv_obj_set_style_text_letter_space(g_rpmLabel, 1, 0);
    lv_label_set_text(g_rpmLabel, "RPM 0");
    lv_obj_align(g_rpmLabel, LV_ALIGN_CENTER, 0, 50);

    g_evLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_evLabel, &spacegrotesk_27_ev, 0);
    lv_obj_set_style_text_color(g_evLabel, Colors::kEvGreen, 0);
    lv_obj_set_style_text_letter_space(g_evLabel, 2, 0);
    lv_label_set_text(g_evLabel, "EV");
    lv_obj_align(g_evLabel, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_flag(g_evLabel, LV_OBJ_FLAG_HIDDEN);

    // Same anchor again: "CAN DUMP USB · EV" / "CAN DUMP USB · 2300 RPM"
    // while usb_dump.cpp is streaming -- the row keeps its job, it just says
    // the bus is being captured too. Recolor markup for the two halves.
    g_dumpLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_dumpLabel, &spacegrotesk_25_rpm, 0);
    lv_obj_set_style_text_color(g_dumpLabel, Colors::kTextBright, 0);
    lv_obj_set_style_text_letter_space(g_dumpLabel, 1, 0);
    lv_label_set_recolor(g_dumpLabel, true);
    lv_label_set_text(g_dumpLabel, "");
    lv_obj_align(g_dumpLabel, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_flag(g_dumpLabel, LV_OBJ_FLAG_HIDDEN);
}

// --- Compact layout ---------------------------------------------------------
// Every screen but the cockpit shows the centre group as a narrow column in
// the middle and the side gauges as thin rails on the screen edges. The
// compact glyphs are separate labels in smaller fonts; each pair travels the
// same path and cross-fades, which reads as the number shrinking without the
// transform layer (a full object-sized buffer) LVGL would need to scale it.
constexpr int16_t kRailW = 8;
constexpr uint32_t kMorphMs = 450;

lv_obj_t *g_speedSmall = nullptr;
lv_obj_t *g_gearSmall = nullptr;
lv_obj_t *g_unitsSmall = nullptr;
lv_obj_t *g_rpmSmall = nullptr;

float g_layoutK = 0.0f; // 0 = cockpit layout, 1 = compact

struct Morph {
    lv_obj_t **full;
    lv_obj_t **compact;    // nullptr: only fades out
    lv_point_t fullOffset; // LV_ALIGN_CENTER offset in the cockpit layout
    lv_point_t compactInk; // screen point the compact glyphs' ink is centred on
    uint32_t glyph;        // whose ink box defines the vertical centre
};

const Morph kMorphs[] = {
    {&g_speedLabel, &g_speedSmall, {0, -19}, {320, 72}, '0'},
    {&g_gearLabel, &g_gearSmall, {-158, -4}, {320, 22}, 'D'},
    {&g_accelTimeLabel, nullptr, {-158, -22}, {320, 22}, '0'},
    {&g_accelThresholdLabel, nullptr, {-158, 14}, {320, 22}, '0'},
    {&g_unitsLabel, &g_unitsSmall, {168, -4}, {320, 113}, 'K'},
    {&g_rpmLabel, &g_rpmSmall, {0, 50}, {320, 145}, 'R'},
    {&g_evLabel, nullptr, {0, 50}, {320, 145}, 'E'},
    {&g_dumpLabel, nullptr, {0, 50}, {320, 145}, 'C'},
};

lv_obj_t *makeCompactLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                           int16_t letterSpace, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, letterSpace, 0);
    lv_label_set_text(l, txt);
    return l;
}

void createCompactGroup(lv_obj_t *parent) {
    g_speedSmall = makeCompactLabel(parent, &spacegrotesk_64_speed, Colors::kText, -2, "0");
    g_gearSmall = makeCompactLabel(parent, &spacegrotesk_20_small, Colors::kAccentCyan, 0, "P");
    g_unitsSmall = makeCompactLabel(parent, &spacegrotesk_20_small, Colors::kAccentCyan, 1, "KM/H");
    g_rpmSmall = makeCompactLabel(parent, &spacegrotesk_16_rpm, Colors::kEvGreen, 1, "EV");
}

// Vertical distance from a label's box centre to the centre of `glyph`'s ink.
int16_t inkDy(lv_obj_t *label, uint32_t glyph) {
    const lv_font_t *f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_font_glyph_dsc_t g;
    lv_font_get_glyph_dsc(f, &g, glyph, 0);
    return static_cast<int16_t>((f->line_height - f->base_line) - g.ofs_y - g.box_h / 2 -
                                f->line_height / 2);
}

lv_coord_t rounded(float v) {
    return static_cast<lv_coord_t>(lroundf(v));
}

lv_coord_t lerp(float a, float b, float k) {
    return rounded(a + (b - a) * k);
}

lv_opa_t toOpa(float v) {
    if (v <= 0.0f) return LV_OPA_TRANSP;
    if (v >= 1.0f) return LV_OPA_COVER;
    return static_cast<lv_opa_t>(v * 255.0f + 0.5f);
}

void applyLayout(float k) {
    g_layoutK = k;
    // A short, complementary cross-fade around the midpoint: a long one leaves
    // two differently sized copies of the speed on screen at once, a fade out
    // then in leaves none.
    float swap = (k - 0.4f) / 0.2f;
    lv_opa_t fullOpa = toOpa(1.0f - swap);
    lv_opa_t compactOpa = toOpa(swap);

    for (const Morph &m : kMorphs) {
        lv_obj_t *full = *m.full;
        int16_t fullDy = inkDy(full, m.glyph);
        float fromX = kScreenW / 2 + m.fullOffset.x;
        float fromY = kScreenH / 2 + m.fullOffset.y + fullDy;
        float x = fromX + (m.compactInk.x - fromX) * k - kScreenW / 2;
        float y = fromY + (m.compactInk.y - fromY) * k - kScreenH / 2;
        lv_obj_align(full, LV_ALIGN_CENTER, rounded(x), rounded(y - fullDy));
        lv_obj_set_style_opa(full, fullOpa, 0);
        if (m.compact) {
            lv_obj_t *compact = *m.compact;
            int16_t compactDy = inkDy(compact, m.glyph);
            lv_obj_align(compact, LV_ALIGN_CENTER, rounded(x), rounded(y - compactDy));
            lv_obj_set_style_opa(compact, compactOpa, 0);
        }
    }

    lv_obj_set_pos(g_battBar, lerp(kBattTickX, 0, k), lerp(kGaugeY, 0, k));
    lv_obj_set_size(g_battBar, lerp(kGaugeW, kRailW, k), lerp(kGaugeH, kScreenH, k));
    lv_obj_set_pos(g_pwrBar, lerp(kPwrTickX, kScreenW - kRailW, k), lerp(kGaugeY, 0, k));
    lv_obj_set_size(g_pwrBar, lerp(kGaugeW, kRailW, k), lerp(kGaugeH, kScreenH, k));

    lv_opa_t sideOpa = toOpa(1.0f - k * 2.5f);
    for (int i = 0; i < kTickCount; i++) {
        lv_obj_set_style_opa(g_battTicks[i], sideOpa, 0);
        lv_obj_set_style_opa(g_pwrTicks[i], sideOpa, 0);
    }
    for (lv_obj_t *o : {g_battIcon, g_battNub, g_battValueLabel, g_battPctLabel, g_pwrCaptionLabel,
                        g_pwrValueLabel, g_pwrPctLabel}) {
        lv_obj_set_style_opa(o, sideOpa, 0);
    }
}

void morphExec(void *, int32_t v) {
    applyLayout(static_cast<float>(v) / 1000.0f);
}

} // namespace

void build(lv_obj_t *parent) {
    // Transparent: the glow and the other screens' content show through.
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, kScreenW, kScreenH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    createBatteryGauge(root);
    createPowerGauge(root);
    createCenterGroup(root);
    createCompactGroup(root);
    applyLayout(0.0f);
}

void update(const VehicleState &state) {
    // CHG/PWR gauge: hsi_power is -100..+255, negative=CHG, positive=PWR.
    // One vertical bar carries both, since the car is never charging and
    // powering at the same instant -- the sign picks the color (white=PWR,
    // green=CHG, red past kPwrRedlinePct) and the magnitude the fill.
    //
    // CHG is shown 1:1 (floor confirmed at exactly -100). PWR is halved for
    // display: on-car observation showed the ECO/PWR boundary (raw~100) is
    // actually the midpoint of the real gauge's PWR sweep, not its top -- so
    // raw~100 reads ~50% and full PWR reads 100% at raw~200. Now that the
    // sign comes from HSI_ZONE the positive side is no longer capped at 155,
    // so full PWR is genuinely reachable; the clamp below caps the top anyway
    // if the raw sweep turns out to run past 200.
    bool isChg = state.hsi_power < 0;
    float rawPct = isChg ? static_cast<float>(-state.hsi_power)
                         : static_cast<float>(state.hsi_power) * 0.5f;
    int pwrPct = static_cast<int>(rawPct + 0.5f);
    if (pwrPct < 0) pwrPct = 0;
    if (pwrPct > 100) pwrPct = 100;

    int battPct = static_cast<int>(state.battery_soc_pct + 0.5f);
    if (battPct < 0) battPct = 0;
    if (battPct > 100) battPct = 100;

    // During the start-up sweep both gauges (and their readouts) show the
    // sweep instead. The power gauge sweeps on the PWR side, so it runs
    // through its redline colour at the top just like under real load, and
    // settles onto the live reading's side only for the last stretch.
    if (g_sweeping) {
        uint32_t t = lv_tick_elaps(g_sweepStartMs);
        int sweptPwr = pwrPct, sweptBatt = battPct;
        if (sweepValue(t, pwrPct, sweptPwr) && sweepValue(t, battPct, sweptBatt)) {
            if (t < kSweepUpMs + kSweepDownMs) isChg = false;
            pwrPct = sweptPwr;
            battPct = sweptBatt;
        } else {
            g_sweeping = false;
        }
    }

    // Pick the zone off the fill the gauge is actually showing, so the red
    // threshold means "three quarters up the bar" no matter how the raw
    // signal is scaled onto it. Between the redline and its hysteresis band
    // the previous zone stands (unless that was CHG, which the sign has just
    // ruled out).
    PowerZone zone;
    if (isChg) {
        zone = PowerZone::Chg;
    } else if (pwrPct >= kPwrRedlinePct) {
        zone = PowerZone::PwrRedline;
    } else if (pwrPct <= kPwrRedlinePct - kPwrRedlineHyst || g_pwrZone == PowerZone::Chg) {
        zone = PowerZone::Pwr;
    } else {
        zone = g_pwrZone;
    }

    if (zone != g_pwrZone) {
        g_pwrZone = zone;
        lv_color_t color = zoneColor(zone);
        lv_obj_set_style_bg_color(g_pwrBar, color, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(g_pwrBar, color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(g_pwrCaptionLabel, color, 0);
        // The caption stays "PWR" on the redline -- the color carries it, and
        // the 14px subset has no other word to spell anyway.
        lv_label_set_text(g_pwrCaptionLabel, zone == PowerZone::Chg ? "CHG" : "PWR");
    }
    lv_bar_set_value(g_pwrBar, pwrPct, LV_ANIM_OFF);
    // The readout under the caption: the HSI percentage while the engine
    // runs, the HV pack current in amps while it does not. With the engine
    // off everything the car does goes through the battery, so the current
    // is the real size of what the bar only shows as a share of its scale.
    // Magnitude only -- CHG/PWR above already says which way it flows. The
    // start-up sweep keeps its percentage.
    bool showAmps = !g_sweeping && (state.ev_drive || state.rpm == 0);
    int pwrReadout = showAmps ? static_cast<int>(lroundf(fabsf(state.battery_current_a))) : pwrPct;
    char pwrBuf[8];
    snprintf(pwrBuf, sizeof(pwrBuf), "%d", pwrReadout);
    lv_label_set_text(g_pwrValueLabel, pwrBuf);
    const char *suffix = showAmps ? "A" : "%";
    if (strcmp(lv_label_get_text(g_pwrPctLabel), suffix) != 0) lv_label_set_text(g_pwrPctLabel, suffix);

    // Battery gauge.
    lv_bar_set_value(g_battBar, battPct, LV_ANIM_OFF);
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d", battPct);
    lv_label_set_text(g_battValueLabel, battBuf);
    lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    // Speed / gear / units.
    char speedBuf[8];
    snprintf(speedBuf, sizeof(speedBuf), "%d", static_cast<int>(state.speed_kph + 0.5f));
    lv_label_set_text(g_speedLabel, speedBuf);
    lv_label_set_text(g_speedSmall, speedBuf);

    // The compact column always shows the gear: the 0-50/0-100 readout is a
    // cockpit-only feature, and it fades out with the rest of the full layout.
    char gearBuf[2] = {gearChar(state.gear), '\0'};
    lv_label_set_text(g_gearSmall, gearBuf);

    AccelTimer::Display accel = AccelTimer::getDisplay();
    if (accel.active) {
        char accelTimeBuf[8];
        snprintf(accelTimeBuf, sizeof(accelTimeBuf), "%.2f", accel.seconds);
        lv_label_set_text(g_accelTimeLabel, accelTimeBuf);
        char thresholdBuf[8];
        snprintf(thresholdBuf, sizeof(thresholdBuf), "0-%d", accel.thresholdKph);
        lv_label_set_text(g_accelThresholdLabel, thresholdBuf);
        lv_obj_add_flag(g_gearLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_accelTimeLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_accelThresholdLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_gearLabel, gearBuf);
        lv_obj_clear_flag(g_gearLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_accelTimeLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_accelThresholdLabel, LV_OBJ_FLAG_HIDDEN);
    }

    // RPM-or-EV row. Force "EV" whenever RPM reads 0 (boot before any CAN
    // traffic, or any RPM==0 state) so the cluster never shows a broken
    // "RPM 0". Raw ev_drive/ice_running are unaffected -- selection only.
    bool showEv = state.ev_drive || state.rpm == 0;
    if (showEv) {
        lv_label_set_text(g_rpmSmall, "EV");
        lv_obj_set_style_text_color(g_rpmSmall, Colors::kEvGreen, 0);
    } else {
        char rpmSmallBuf[16];
        snprintf(rpmSmallBuf, sizeof(rpmSmallBuf), "RPM %d", state.rpm < 0 ? 0 : state.rpm);
        lv_label_set_text(g_rpmSmall, rpmSmallBuf);
        lv_obj_set_style_text_color(g_rpmSmall, Colors::kTextBright, 0);
    }
    if (state.usb_dump) {
        lv_obj_add_flag(g_rpmLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_evLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_dumpLabel, LV_OBJ_FLAG_HIDDEN);
        char buf[64];
        if (showEv) {
            snprintf(buf, sizeof(buf), COCKPIT_HEX_ACCENT " CAN DUMP USB#  ·  " COCKPIT_HEX_EV " EV#");
        } else {
            snprintf(buf, sizeof(buf), COCKPIT_HEX_ACCENT " CAN DUMP USB#  ·  %d RPM", state.rpm < 0 ? 0 : state.rpm);
        }
        if (strcmp(lv_label_get_text(g_dumpLabel), buf) != 0) lv_label_set_text(g_dumpLabel, buf);
        return;
    }
    lv_obj_add_flag(g_dumpLabel, LV_OBJ_FLAG_HIDDEN);
    if (showEv) {
        lv_obj_add_flag(g_rpmLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_evLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g_rpmLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_evLabel, LV_OBJ_FLAG_HIDDEN);
        char rpmBuf[16];
        int rpm = state.rpm < 0 ? 0 : state.rpm;
        snprintf(rpmBuf, sizeof(rpmBuf), "RPM %d", rpm);
        lv_label_set_text(g_rpmLabel, rpmBuf);
    }
}

void startSweep() {
    g_sweeping = true;
    g_sweepStartMs = lv_tick_get();
}

bool sweeping() { return g_sweeping; }

void setCompact(bool compact) {
    int32_t from = static_cast<int32_t>(lroundf(g_layoutK * 1000.0f));
    int32_t to = compact ? 1000 : 0;
    lv_anim_del(&g_layoutK, nullptr);
    if (from == to) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &g_layoutK);
    lv_anim_set_exec_cb(&a, morphExec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, static_cast<uint32_t>(kMorphMs * std::abs(to - from) / 1000));
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

} // namespace CockpitUi
