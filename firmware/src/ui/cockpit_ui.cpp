#include "cockpit_ui.h"
#include "colors.h"
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
constexpr int16_t kSideValueY = kGaugeBottom - 18; // 18 = dinnext_26_battery's line_height
// Each column's "top row" sits just above that: the battery icon on the left
// (12px tall), the CHG/PWR caption on the right (10px line height).
constexpr int16_t kSideIconY = kSideValueY - 3 - 12;    // 3px gap above the value row
constexpr int16_t kSideCaptionY = kSideValueY - 3 - 10; // same gap, 10px line height

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

lv_obj_t *g_battBar = nullptr;
lv_obj_t *g_battValueLabel = nullptr;
lv_obj_t *g_battPctLabel = nullptr;

lv_obj_t *g_pwrBar = nullptr;
lv_obj_t *g_pwrValueLabel = nullptr;
lv_obj_t *g_pwrPctLabel = nullptr;
lv_obj_t *g_pwrCaptionLabel = nullptr;
bool g_pwrIsChg = false; // last-applied fill color side, so update() only restyles on a flip

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

// Vertical tick gauge shared by both side columns: an lv_bar (auto-oriented
// vertical since h > w, filling bottom-up by default -- exactly the spec's
// behavior, no extra code needed for the fill direction) with 11 static tick
// marks layered on top, evenly spaced, drawn once.
lv_obj_t *createTickGauge(lv_obj_t *parent, int16_t x, lv_color_t color) {
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

    for (int i = 0; i < 11; i++) {
        lv_obj_t *tick = lv_obj_create(parent);
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

    g_battBar = createTickGauge(parent, kBattTickX, Colors::kBatteryBlue);
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
    lv_txt_get_size(&pctSize, "%", &dinnext_13_pct, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int16_t valueBoxX = kPwrTextRight - 2 - static_cast<int16_t>(pctSize.x) - kPwrTextBoxW;

    g_pwrCaptionLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pwrCaptionLabel, &dinnext_14_chgpwr, 0);
    lv_obj_set_style_text_letter_space(g_pwrCaptionLabel, 1, 0);
    lv_obj_set_style_text_color(g_pwrCaptionLabel, Colors::kPwrWhite, 0);
    lv_obj_set_width(g_pwrCaptionLabel, kPwrTextBoxW);
    lv_obj_set_style_text_align(g_pwrCaptionLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_pwrCaptionLabel, "PWR");
    lv_obj_set_pos(g_pwrCaptionLabel, kPwrTextRight - kPwrTextBoxW, kSideCaptionY);

    g_pwrValueLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pwrValueLabel, &dinnext_26_battery, 0);
    lv_obj_set_style_text_color(g_pwrValueLabel, Colors::kText, 0);
    lv_obj_set_width(g_pwrValueLabel, kPwrTextBoxW);
    lv_obj_set_style_text_align(g_pwrValueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_pwrValueLabel, "--");
    lv_obj_set_pos(g_pwrValueLabel, valueBoxX, kSideValueY);

    g_pwrPctLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_pwrPctLabel, &dinnext_13_pct, 0);
    lv_obj_set_style_text_color(g_pwrPctLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_pwrPctLabel, "%");
    lv_obj_align_to(g_pwrPctLabel, g_pwrValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

    g_pwrBar = createTickGauge(parent, kPwrTickX, Colors::kPwrWhite);
    g_pwrIsChg = false;
}

void createCenterGroup(lv_obj_t *parent) {
    // Speed + the RPM/EV row form one vertically-centered block (see the
    // layout comment above): speed dy=-19, RPM/EV dy=+50.
    g_speedLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_speedLabel, &dinnext_120_speed, 0);
    lv_obj_set_style_text_color(g_speedLabel, Colors::kText, 0);
    lv_obj_set_style_text_letter_space(g_speedLabel, -3, 0);
    lv_label_set_text(g_speedLabel, "0");
    lv_obj_align(g_speedLabel, LV_ALIGN_CENTER, 0, -19);

    // Gear keeps its own fixed anchor (dy=-4), holding the same offset
    // relative to the speed number it flanks.
    g_gearLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_gearLabel, &dinnext_40_gear, 0);
    lv_obj_set_style_text_color(g_gearLabel, Colors::kAccentCyan, 0);
    lv_label_set_text(g_gearLabel, "P");
    lv_obj_align(g_gearLabel, LV_ALIGN_CENTER, -158, -4);

    // 0-50/0-100 timer readout, sharing the gear letter's anchor (dx=-158) --
    // hidden by default, swapped in for the gear letter by update() while
    // AccelTimer has a result to show. Two stacked labels, same centering
    // trick the RPM/EV row already uses one anchor over. Both lines use the
    // same 40px size as the gear letter itself (dinnext_40_gear's
    // line_height is 28px) for readability at a glance. Centered on the gear
    // letter's own anchor (dy=-4): seconds on top (dy=-22), threshold caption
    // below (dy=14), 36px apart so the two 28px-tall lines don't touch.
    g_accelTimeLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_accelTimeLabel, &dinnext_40_accel_time, 0);
    lv_obj_set_style_text_color(g_accelTimeLabel, Colors::kAccentCyan, 0);
    lv_label_set_text(g_accelTimeLabel, "0.00");
    lv_obj_align(g_accelTimeLabel, LV_ALIGN_CENTER, -158, -22);
    lv_obj_add_flag(g_accelTimeLabel, LV_OBJ_FLAG_HIDDEN);

    g_accelThresholdLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_accelThresholdLabel, &dinnext_40_accel_label, 0);
    lv_obj_set_style_text_color(g_accelThresholdLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_accelThresholdLabel, "0-50");
    lv_obj_align(g_accelThresholdLabel, LV_ALIGN_CENTER, -158, 14);
    lv_obj_add_flag(g_accelThresholdLabel, LV_OBJ_FLAG_HIDDEN);

    g_unitsLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_unitsLabel, &dinnext_30_units, 0);
    lv_obj_set_style_text_color(g_unitsLabel, Colors::kAccentCyan, 0);
    lv_obj_set_style_text_letter_space(g_unitsLabel, 2, 0);
    lv_label_set_text(g_unitsLabel, "KM/H");
    lv_obj_align(g_unitsLabel, LV_ALIGN_CENTER, 168, -4);

    // RPM row and EV row share the same anchor; exactly one of the two is
    // visible at a time based on ev_drive.
    g_rpmLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_rpmLabel, &dinnext_26_rpm, 0);
    lv_obj_set_style_text_color(g_rpmLabel, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(g_rpmLabel, 1, 0);
    lv_label_set_text(g_rpmLabel, "RPM 0");
    lv_obj_align(g_rpmLabel, LV_ALIGN_CENTER, 0, 50);

    g_evLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(g_evLabel, &dinnext_28_ev, 0);
    lv_obj_set_style_text_color(g_evLabel, Colors::kEvGreen, 0);
    lv_obj_set_style_text_letter_space(g_evLabel, 2, 0);
    lv_label_set_text(g_evLabel, "EV");
    lv_obj_align(g_evLabel, LV_ALIGN_CENTER, 0, 50);
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

    createBatteryGauge(root);
    createPowerGauge(root);
    createCenterGroup(root);
}

void update(const VehicleState &state) {
    // CHG/PWR gauge: hsi_power is -100..+255, negative=CHG, positive=PWR.
    // One vertical bar carries both, since the car is never charging and
    // powering at the same instant -- the sign picks the color (white=PWR,
    // green=CHG) and the magnitude the fill.
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

    // Restyle only on a CHG<->PWR flip, not every frame -- a style change
    // invalidates the object, and the sign holds for many frames at a time.
    if (isChg != g_pwrIsChg) {
        g_pwrIsChg = isChg;
        lv_color_t color = isChg ? Colors::kChgGreen : Colors::kPwrWhite;
        lv_obj_set_style_bg_color(g_pwrBar, color, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(g_pwrBar, color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(g_pwrCaptionLabel, color, 0);
        lv_label_set_text(g_pwrCaptionLabel, isChg ? "CHG" : "PWR");
    }
    lv_bar_set_value(g_pwrBar, pwrPct, LV_ANIM_OFF);
    char pwrBuf[8];
    snprintf(pwrBuf, sizeof(pwrBuf), "%d", pwrPct);
    lv_label_set_text(g_pwrValueLabel, pwrBuf);

    // Battery gauge.
    int battPct = static_cast<int>(state.battery_soc_pct + 0.5f);
    if (battPct < 0) battPct = 0;
    if (battPct > 100) battPct = 100;
    lv_bar_set_value(g_battBar, battPct, LV_ANIM_OFF);
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d", battPct);
    lv_label_set_text(g_battValueLabel, battBuf);
    lv_obj_align_to(g_battPctLabel, g_battValueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);

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

    // RPM-or-EV row. Force "EV" whenever RPM reads 0 (boot before any CAN
    // traffic, or any RPM==0 state) so the cluster never shows a broken
    // "RPM 0". Raw ev_drive/ice_running are unaffected -- selection only.
    if (state.ev_drive || state.rpm == 0) {
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
