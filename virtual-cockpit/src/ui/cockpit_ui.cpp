#include "cockpit_ui.h"
#include "colors.h"
#include "bar_gauge.h"
#include "fonts/fonts.h"
#include "accel_timer.h"

#include <cstdio>
#include <lvgl.h>

namespace CockpitUi {

namespace {

// --- Layout constants, matching Cockpit.dc.html's 640x172 canvas ---
constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;
// Root screen is 640x172 (center y=86), so an absolute target_y maps to an
// LV_ALIGN_CENTER y-offset of (target_y - 86). Speed + the RPM/EV row below
// it are treated as one vertically-centered block, but the "center" is
// anchored between the CHG/PWR bar's bottom edge (y=16) and the screen's
// bottom edge (y=172), not the screen's raw midpoint -- speed dy=-11,
// RPM/EV dy=+58, a ~17px gap between them (slightly tightened from an
// earlier ~22-23px per owner feedback), with the margin above speed still
// close to the margin below the RPM/EV row (~16-17px each). Gear/units
// keep their own original target_y=90 anchor (dy=+4) independent of this
// block, per the owner's explicit "leave gear/units where they are"
// feedback.

// CHG/PWR bar: x=70..570 (500px), y=0, h=16, 3px black divider at the center.
constexpr int16_t kBarY = 0;
constexpr int16_t kBarH = 16;
constexpr int16_t kBarX0 = 70;
constexpr int16_t kBarX1 = 570;
constexpr int16_t kDividerW = 3;
constexpr int16_t kChgW = (kBarX1 - kBarX0 - kDividerW) / 2;        // 248
constexpr int16_t kPwrW = (kBarX1 - kBarX0 - kDividerW) - kChgW;    // 249
constexpr int16_t kDividerX = kBarX0 + kChgW;                       // 318

// Left/right 74px side columns.
constexpr int16_t kLeftX = 18;
constexpr int16_t kRightW = 74;
constexpr int16_t kRightX = kScreenW - 18 - kRightW; // 548

// Left slot: HV battery gauge (moved here from the right per owner
// feedback). Equal top/bottom margin (16px, matching the CHG/PWR bar's own
// height as a clean visual reference). The tick gauge is flush to the
// column's own outer/left edge; the icon/nub/numeric readout sit on the
// inner side, toward screen center.
constexpr int16_t kBattTickW = 14;
constexpr int16_t kBattTickX = kLeftX; // 18, flush to the column's outer/left edge
// icon/value moved close to the bar: the gap from the bar's right edge to
// the text's left edge now equals the screen's own left margin (kLeftX),
// per owner feedback -- was a much wider ~34px gap before.
constexpr int16_t kBattInnerX = kBattTickX + kBattTickW + kLeftX; // 50; nub/value sit 2px further left (toward the ticks)
constexpr int16_t kBattMargin = 16;
constexpr int16_t kBattTickY = kBattMargin;                        // 16
constexpr int16_t kBattTickBottom = kScreenH - kBattMargin;        // 156
constexpr int16_t kBattTickH = kBattTickBottom - kBattTickY;       // 140

// Shared "bottom row" Y for the two side columns (battery numeric value /
// right-slot clock, bottom-aligned with the bar).
constexpr int16_t kSideValueY = kBattTickBottom - 18; // 18 = dinnext_26_battery/dinnext_26_rpm's line_height
// Battery icon sits in its own "top row" above that, sized for the 12px
// icon specifically (not reused for the right slot -- its temperature
// label is 19px-tall text, not a small icon, and needs its own gap below,
// see kRightTempY).
constexpr int16_t kSideIconY = kSideValueY - 3 - 12;   // 3px gap above the value row, icon is 12px tall

// Right slot (moved here from the left per owner feedback): ambient
// temperature (top row) + clock time (bottom row, mirrors the battery
// numeric value's row, same font size), both right-aligned within the
// column so the two lines up flush on their right edge -- no real
// fuel-level signal exists on this bus (see docs/signal_findings.md), so
// this slot never held a fuel gauge to begin with.
constexpr int16_t kRightTempY = kSideValueY - 4 - 19; // 4px gap above the clock row, dinnext_26_rpm's line_height=19

BarGauge::Handle g_chg;
BarGauge::Handle g_pwr;

lv_obj_t *g_battBar = nullptr;
lv_obj_t *g_battValueLabel = nullptr;
lv_obj_t *g_battPctLabel = nullptr;

lv_obj_t *g_tempLabel = nullptr;
lv_obj_t *g_clockLabel = nullptr;

lv_obj_t *g_speedLabel = nullptr;
lv_obj_t *g_gearLabel = nullptr;
lv_obj_t *g_unitsLabel = nullptr;
lv_obj_t *g_rpmLabel = nullptr;
lv_obj_t *g_evLabel = nullptr;

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

void createDivider(lv_obj_t *parent) {
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, kDividerW, kBarH);
    lv_obj_set_pos(d, kDividerX, kBarY);
    lv_obj_set_style_bg_color(d, Colors::kDivider, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
}

void createBatteryGauge(lv_obj_t *parent) {
    // Icon + numeric readout sit at the BOTTOM of the column, aligned with
    // the bar's own bottom edge (kSideValueY/kSideIconY). The tick gauge is
    // flush to the column's outer/left edge (kBattTickX=kLeftX); icon/nub/
    // value sit on the inner side (kBattInnerX), toward screen center, with
    // the nub pointing back toward the ticks (mirrors the original
    // right-column layout, where the nub pointed toward the ticks on that
    // side's outer/right edge).
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 16, 12);
    lv_obj_set_pos(icon, kBattInnerX, kSideIconY);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(icon, Colors::kBatteryBlue, 0);
    lv_obj_set_style_border_width(icon, 2, 0);
    lv_obj_set_style_radius(icon, 1, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nub = lv_obj_create(parent);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 2, 4);
    lv_obj_set_pos(nub, kBattInnerX - 2, kSideIconY + 5);
    lv_obj_set_style_bg_color(nub, Colors::kBatteryBlue, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_clear_flag(nub, LV_OBJ_FLAG_SCROLLABLE);

    // Numeric readout: value (26px bold) + "%" (13px semi-bold, muted).
    g_battValueLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_battValueLabel, &dinnext_26_battery, 0);
    lv_obj_set_style_text_color(g_battValueLabel, Colors::kText, 0);
    lv_label_set_text(g_battValueLabel, "--");
    lv_obj_set_pos(g_battValueLabel, kBattInnerX - 2, kSideValueY);

    g_battPctLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_battPctLabel, &dinnext_13_pct, 0);
    lv_obj_set_style_text_color(g_battPctLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_battPctLabel, "%");
    lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    // Vertical tick gauge: lv_bar auto-orients vertical since h > w, filling
    // bottom-up by default -- exactly the spec's behavior, no extra code
    // needed for the fill direction.
    g_battBar = lv_bar_create(parent);
    lv_obj_remove_style_all(g_battBar);
    lv_obj_set_size(g_battBar, kBattTickW, kBattTickH);
    lv_obj_set_pos(g_battBar, kBattTickX, kBattTickY);
    lv_bar_set_range(g_battBar, 0, 100);
    lv_bar_set_value(g_battBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_battBar, Colors::kBarTrack, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_battBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_battBar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_battBar, Colors::kBatteryBlue, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_battBar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_battBar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(g_battBar, Colors::kBatteryBlue, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(g_battBar, 10, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_spread(g_battBar, 1, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(g_battBar, LV_OPA_60, LV_PART_INDICATOR);

    // 11 static tick marks layered on top, evenly spaced, drawn once.
    for (int i = 0; i < 11; i++) {
        lv_obj_t *tick = lv_obj_create(parent);
        lv_obj_remove_style_all(tick);
        lv_obj_set_size(tick, 9, 1);
        int16_t ty = kBattTickY + static_cast<int16_t>(i * (kBattTickH - 1) / 10);
        lv_obj_set_pos(tick, kBattTickX + (kBattTickW - 9) / 2, ty);
        lv_obj_set_style_bg_color(tick, Colors::kTickDim, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void createTempClockSlot(lv_obj_t *parent) {
    // Replaces the design's fuel gauge: no real fuel-level signal exists on
    // this bus (0x3A0 is a fuel-consumption counter, not a tank level -- see
    // docs/signal_findings.md), so this column shows ambient temperature and
    // clock time instead, per the owner's decision. Right-aligned (not
    // centered) so the two lines share a common right edge, per owner
    // feedback.
    g_tempLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_tempLabel, &dinnext_26_rpm, 0);
    lv_obj_set_style_text_color(g_tempLabel, Colors::kText, 0);
    lv_obj_set_style_text_align(g_tempLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(g_tempLabel, kRightW);
    lv_label_set_text(g_tempLabel, ""); // populated by the first update() call
    lv_obj_set_pos(g_tempLabel, kRightX, kRightTempY);

    g_clockLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_clockLabel, &dinnext_26_rpm, 0);
    lv_obj_set_style_text_color(g_clockLabel, Colors::kText, 0); // white, not muted -- per owner feedback
    lv_obj_set_style_text_align(g_clockLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(g_clockLabel, kRightW);
    lv_label_set_text(g_clockLabel, "");
    lv_obj_set_pos(g_clockLabel, kRightX, kSideValueY);
}

void createCenterGroup(lv_obj_t *parent) {
    // Speed + the RPM/EV row form one vertically-centered block (see the
    // layout comment above): speed dy=-11, RPM/EV dy=+58.
    g_speedLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_speedLabel, &dinnext_120_speed, 0);
    lv_obj_set_style_text_color(g_speedLabel, Colors::kText, 0);
    lv_obj_set_style_text_letter_space(g_speedLabel, -3, 0);
    lv_label_set_text(g_speedLabel, "0");
    lv_obj_align(g_speedLabel, LV_ALIGN_CENTER, 0, -11);

    // Gear keeps its original position (target y=90 => dy=4) independent of
    // the speed/RPM re-centering above -- only the font got bigger, per the
    // owner's explicit "leave it in the same position" request.
    g_gearLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_gearLabel, &dinnext_40_gear, 0);
    lv_obj_set_style_text_color(g_gearLabel, Colors::kAccentCyan, 0);
    lv_label_set_text(g_gearLabel, "P");
    lv_obj_align(g_gearLabel, LV_ALIGN_CENTER, -158, 4);

    // 0-50/0-100 timer readout, sharing the gear letter's anchor (dx=-158) --
    // hidden by default, swapped in for the gear letter by update() while
    // AccelTimer has a result to show. Two stacked labels, same centering
    // trick the RPM/EV row already uses one anchor over, just split into two
    // rows here: seconds on top (dy=-6), threshold caption below (dy=13).
    g_accelTimeLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_accelTimeLabel, &dinnext_20_accel_time, 0);
    lv_obj_set_style_text_color(g_accelTimeLabel, Colors::kAccentCyan, 0);
    lv_label_set_text(g_accelTimeLabel, "0.00");
    lv_obj_align(g_accelTimeLabel, LV_ALIGN_CENTER, -158, -6);
    lv_obj_add_flag(g_accelTimeLabel, LV_OBJ_FLAG_HIDDEN);

    g_accelThresholdLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_accelThresholdLabel, &dinnext_15_accel_label, 0);
    lv_obj_set_style_text_color(g_accelThresholdLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_accelThresholdLabel, "0-50");
    lv_obj_align(g_accelThresholdLabel, LV_ALIGN_CENTER, -158, 13);
    lv_obj_add_flag(g_accelThresholdLabel, LV_OBJ_FLAG_HIDDEN);

    // Units label: unchanged, per the owner's "leave km/h as is" request.
    g_unitsLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_unitsLabel, &dinnext_30_units, 0);
    lv_obj_set_style_text_color(g_unitsLabel, Colors::kAccentCyan, 0);
    lv_obj_set_style_text_letter_space(g_unitsLabel, 2, 0);
    lv_label_set_text(g_unitsLabel, "KM/H");
    lv_obj_align(g_unitsLabel, LV_ALIGN_CENTER, 168, 4);

    // RPM row and EV row share the same anchor; exactly one of the two is
    // visible at a time based on ev_drive.
    g_rpmLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_rpmLabel, &dinnext_26_rpm, 0);
    lv_obj_set_style_text_color(g_rpmLabel, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(g_rpmLabel, 1, 0);
    lv_label_set_text(g_rpmLabel, "RPM 0");
    lv_obj_align(g_rpmLabel, LV_ALIGN_CENTER, 0, 58);

    g_evLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_evLabel, &dinnext_28_ev, 0);
    lv_obj_set_style_text_color(g_evLabel, Colors::kEvGreen, 0);
    lv_obj_set_style_text_letter_space(g_evLabel, 2, 0);
    lv_label_set_text(g_evLabel, "EV");
    lv_obj_align(g_evLabel, LV_ALIGN_CENTER, 0, 58);
    lv_obj_add_flag(g_evLabel, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

void build(lv_obj_t *parent) {
    // Builds onto its own child object rather than restyling `parent`
    // directly, so this stays agnostic to whatever `parent` actually is
    // (AppUi currently passes a plain per-screen container -- see
    // AppUi::build()).
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, kScreenW, kScreenH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, Colors::kBg, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    BarGauge::create(&g_chg, root, kBarX0, kBarY, kChgW, kBarH, true, "CHG");
    createDivider(root);
    BarGauge::create(&g_pwr, root, kDividerX + kDividerW, kBarY, kPwrW, kBarH, false, "PWR");

    createTempClockSlot(root);
    createBatteryGauge(root);
    createCenterGroup(root);
}

void update(const VehicleState &state, time_t clockEpoch) {
    // CHG/PWR bar: hsi_power is already -100..100, negative=CHG, positive=PWR.
    float chgPct = state.hsi_power < 0 ? static_cast<float>(-state.hsi_power) : 0.0f;
    float pwrPct = state.hsi_power > 0 ? static_cast<float>(state.hsi_power) : 0.0f;
    BarGauge::setFillPct(&g_chg, chgPct);
    BarGauge::setFillPct(&g_pwr, pwrPct);

    // Battery gauge.
    int battPct = static_cast<int>(state.battery_soc_pct + 0.5f);
    if (battPct < 0) battPct = 0;
    if (battPct > 100) battPct = 100;
    lv_bar_set_value(g_battBar, battPct, LV_ANIM_OFF);
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d", battPct);
    lv_label_set_text(g_battValueLabel, battBuf);
    lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    // Right slot: ambient temperature + clock.
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%d\xC2\xB0" "C", static_cast<int>(state.ambient_temp_c + (state.ambient_temp_c >= 0 ? 0.5f : -0.5f)));
    lv_label_set_text(g_tempLabel, tempBuf);

    if (clockEpoch > 0) {
        struct tm tmVal;
        gmtime_r(&clockEpoch, &tmVal); // UTC, no local-timezone offset -- per the owner's request
        char clockBuf[8];
        snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", tmVal.tm_hour, tmVal.tm_min);
        lv_label_set_text(g_clockLabel, clockBuf);
    } else {
        lv_label_set_text(g_clockLabel, "");
    }

    // Speed / gear / units.
    char speedBuf[8];
    snprintf(speedBuf, sizeof(speedBuf), "%d", static_cast<int>(state.speed_kph + 0.5f));
    lv_label_set_text(g_speedLabel, speedBuf);

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
        char gearBuf[2] = {gearChar(state.gear), '\0'};
        lv_label_set_text(g_gearLabel, gearBuf);
        lv_obj_clear_flag(g_gearLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_accelTimeLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_accelThresholdLabel, LV_OBJ_FLAG_HIDDEN);
    }

    // RPM-or-EV row.
    if (state.ev_drive) {
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

} // namespace CockpitUi
