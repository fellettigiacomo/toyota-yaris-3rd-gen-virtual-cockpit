#include "gmeter_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "g_meter.h"

#include <cmath>
#include <cstdio>

namespace GMeterUi {

namespace {

constexpr int16_t kScreenW = 640;
constexpr int16_t kScreenH = 172;

// --- g-ball (left) --------------------------------------------------------
// Centered in a square that leaves 16px top and bottom, mirroring the
// margins the cockpit screen's side columns use. The outer ring is full
// scale; the inner one is half of it.
constexpr int16_t kBallCx = 86;
constexpr int16_t kBallCy = 86;
constexpr int16_t kBallR = 70;
constexpr float kFullScaleG = 1.0f; // outer ring. A road car on road tyres lives well inside this.
constexpr int16_t kDotR = 7;

// --- current reading (middle) --------------------------------------------
// One vertically centered block: caption, big value, scale note. Y positions
// use the fonts' real line heights (montserrat_14: 16, dinnext_40_accel_time:
// 28, dinnext_24_label: 19), so 53..119 is centered on the ball's 86.
constexpr int16_t kValueCx = 255;
constexpr int16_t kCaptionY = 53;
constexpr int16_t kValueY = 71;
constexpr int16_t kNoteY = 103;
constexpr int16_t kStatusY = 75; // status word replaces the value; 24px, so a touch lower

// --- session peaks (right), 2x2 ------------------------------------------
constexpr int16_t kPeakCx[2] = {418, 566};
constexpr int16_t kPeakCaptionY[2] = {44, 91};
constexpr int16_t kPeakValueY[2] = {62, 109};

// Tile order: accel top-left, brake bottom-left, left top-right, right
// bottom-right -- the two longitudinal ones stacked in one column, the two
// lateral ones in the other, so a glance compares like with like.
enum Peak { PeakAccel = 0, PeakBrake, PeakLeft, PeakRight, PeakCount };
const char *kPeakCaption[PeakCount] = {"PEAK ACCEL", "PEAK BRAKE", "PEAK LEFT", "PEAK RIGHT"};
constexpr int16_t kPeakCol[PeakCount] = {0, 0, 1, 1};
constexpr int16_t kPeakRow[PeakCount] = {0, 1, 0, 1};

lv_obj_t *g_dot = nullptr;
lv_obj_t *g_valueLabel = nullptr;  // "0.42"
lv_obj_t *g_unitLabel = nullptr;   // "G" beside it
lv_obj_t *g_statusLabel = nullptr; // "ZEROING" / "LEARNING" / "NO SENSOR"
lv_obj_t *g_noteLabel = nullptr;   // muted line under the value
lv_obj_t *g_peakValue[PeakCount] = {nullptr, nullptr, nullptr, nullptr};

GMeter::Phase g_shownPhase = GMeter::Phase::NoSensor;
bool g_phaseValid = false;

// Content-sized label positioned by anchor -- same helper efficiency_ui.cpp
// uses, kept local for the same reason it is there (no shared UI toolkit in
// this project yet, and two copies is cheaper than the wrong abstraction).
enum Anchor { AnchorLeft, AnchorCenter };
lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                    int16_t anchorX, int16_t y, Anchor anchor) {
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, anchor == AnchorCenter ? static_cast<int16_t>(anchorX - sz.x / 2) : anchorX, y);
    lv_label_set_text(l, txt);
    return l;
}

void setCentered(lv_obj_t *l, const lv_font_t *font, int16_t cx, int16_t y, const char *txt) {
    lv_label_set_text(l, txt);
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(l, static_cast<int16_t>(cx - sz.x / 2), y);
}

// Hollow circle: an object with no fill and a 1px border, rounded all the
// way. Used for both rings.
void makeRing(lv_obj_t *parent, int16_t r, lv_color_t color) {
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, static_cast<int16_t>(2 * r), static_cast<int16_t>(2 * r));
    lv_obj_set_pos(ring, static_cast<int16_t>(kBallCx - r), static_cast<int16_t>(kBallCy - r));
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, color, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
}

void makeHairline(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, w, h);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_bg_color(line, Colors::kBarTrack, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

const char *statusWord(GMeter::Phase phase) {
    switch (phase) {
        case GMeter::Phase::NoSensor: return "NO SENSOR";
        case GMeter::Phase::Zeroing: return "ZEROING";
        case GMeter::Phase::Learning: return "LEARNING";
        case GMeter::Phase::Ready: return "";
    }
    return "";
}

// The note line doubles as the explanation of whatever the screen is doing:
// while calibrating it says what the car has to do next, and once it is
// running it states the ring's scale.
void setNote(GMeter::Phase phase, float learnPct) {
    char buf[40];
    switch (phase) {
        case GMeter::Phase::NoSensor:
            snprintf(buf, sizeof(buf), "IMU NOT ANSWERING");
            break;
        case GMeter::Phase::Zeroing:
            snprintf(buf, sizeof(buf), "STOP TO FIND LEVEL");
            break;
        case GMeter::Phase::Learning:
            snprintf(buf, sizeof(buf), "DRIVE STRAIGHT %d%%", static_cast<int>(learnPct));
            break;
        case GMeter::Phase::Ready:
            snprintf(buf, sizeof(buf), "OUTER RING 1.0 G");
            break;
    }
    setCentered(g_noteLabel, &lv_font_montserrat_14, kValueCx, kNoteY, buf);
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

    // Crosshair first, so the rings and the dot draw over it.
    makeHairline(root, static_cast<int16_t>(kBallCx - kBallR), kBallCy,
                 static_cast<int16_t>(2 * kBallR), 1);
    makeHairline(root, kBallCx, static_cast<int16_t>(kBallCy - kBallR), 1,
                 static_cast<int16_t>(2 * kBallR));
    makeRing(root, static_cast<int16_t>(kBallR / 2), Colors::kBarTrack);
    makeRing(root, kBallR, Colors::kTickDim);

    // The dot: same cyan-with-a-glow treatment the cockpit's gauges use for
    // the one thing on screen that moves.
    g_dot = lv_obj_create(root);
    lv_obj_remove_style_all(g_dot);
    lv_obj_set_size(g_dot, static_cast<int16_t>(2 * kDotR), static_cast<int16_t>(2 * kDotR));
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_dot, Colors::kAccentCyan, 0);
    lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(g_dot, Colors::kAccentCyan, 0);
    lv_obj_set_style_shadow_width(g_dot, 12, 0);
    lv_obj_set_style_shadow_spread(g_dot, 1, 0);
    lv_obj_set_style_shadow_opa(g_dot, LV_OPA_60, 0);
    lv_obj_clear_flag(g_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(g_dot, static_cast<int16_t>(kBallCx - kDotR), static_cast<int16_t>(kBallCy - kDotR));

    makeLabel(root, "G-FORCE", &lv_font_montserrat_14, Colors::kMutedText, kValueCx, kCaptionY,
              AnchorCenter);

    // Value and unit are one group centered together: the number is
    // 40px (digits and '.' only -- that font has no letters), the "G" beside
    // it is the 28px A-Z face, bottom-aligned to it.
    g_valueLabel = makeLabel(root, "0.00", &dinnext_40_accel_time, Colors::kAccentCyan, kValueCx,
                             kValueY, AnchorCenter);
    g_unitLabel = lv_label_create(root);
    lv_obj_set_style_text_font(g_unitLabel, &dinnext_28_stat, 0);
    lv_obj_set_style_text_color(g_unitLabel, Colors::kMutedText, 0);
    lv_label_set_text(g_unitLabel, "G");
    lv_obj_align_to(g_unitLabel, g_valueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -2);

    g_statusLabel = makeLabel(root, "", &dinnext_24_label, Colors::kText, kValueCx, kStatusY,
                              AnchorCenter);
    g_noteLabel = makeLabel(root, "", &lv_font_montserrat_14, Colors::kMutedText, kValueCx, kNoteY,
                            AnchorCenter);

    for (int i = 0; i < PeakCount; i++) {
        int16_t cx = kPeakCx[kPeakCol[i]];
        makeLabel(root, kPeakCaption[i], &lv_font_montserrat_14, Colors::kMutedText, cx,
                  kPeakCaptionY[kPeakRow[i]], AnchorCenter);
        g_peakValue[i] = makeLabel(root, "0.00 G", &dinnext_24_label, Colors::kText, cx,
                                   kPeakValueY[kPeakRow[i]], AnchorCenter);
    }
}

void update(const VehicleState &) {
    GMeter::Reading r = GMeter::get();
    bool ready = (r.phase == GMeter::Phase::Ready);

    // The value/unit pair and the status word share the middle slot -- swap
    // them only on an actual phase change, not every frame (same reason the
    // cockpit's gauges only restyle on a zone change).
    if (!g_phaseValid || r.phase != g_shownPhase) {
        g_phaseValid = true;
        g_shownPhase = r.phase;
        if (ready) {
            lv_obj_clear_flag(g_valueLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_unitLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_statusLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_valueLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_unitLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_statusLabel, LV_OBJ_FLAG_HIDDEN);
            // No axes yet means no honest place to put the dot -- hide it
            // rather than park it at a centre that would read as "0.00 g".
            lv_obj_add_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
            setCentered(g_statusLabel, &dinnext_24_label, kValueCx, kStatusY, statusWord(r.phase));
        }
        // Peaks read 0.00 G until something has actually been measured, which
        // would otherwise look like a reading rather than an empty slate.
        // Dim them until the axes exist.
        lv_color_t peakColor = ready ? Colors::kText : Colors::kTickDim;
        for (int i = 0; i < PeakCount; i++) {
            lv_obj_set_style_text_color(g_peakValue[i], peakColor, 0);
        }
    }
    setNote(r.phase, r.learnPct);

    if (!ready) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(r.magG));
    setCentered(g_valueLabel, &dinnext_40_accel_time, kValueCx, kValueY, buf);
    lv_obj_align_to(g_unitLabel, g_valueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -2);

    // Dot position: +long is up the screen, +lat is to the right. Clamped to
    // the outer ring so an off-scale hit parks on the rim instead of
    // wandering across the peak tiles.
    float sx = r.latG / kFullScaleG;
    float sy = r.longG / kFullScaleG;
    float mag = sx * sx + sy * sy;
    if (mag > 1.0f) {
        float inv = 1.0f / std::sqrt(mag);
        sx *= inv;
        sy *= inv;
    }
    int16_t dx = static_cast<int16_t>(kBallCx + sx * kBallR - kDotR + 0.5f);
    int16_t dy = static_cast<int16_t>(kBallCy - sy * kBallR - kDotR + 0.5f);
    lv_obj_set_pos(g_dot, dx, dy);

    const float peaks[PeakCount] = {r.peakAccelG, r.peakBrakeG, r.peakLeftG, r.peakRightG};
    for (int i = 0; i < PeakCount; i++) {
        snprintf(buf, sizeof(buf), "%.2f G", static_cast<double>(peaks[i]));
        setCentered(g_peakValue[i], &dinnext_24_label, kPeakCx[kPeakCol[i]],
                    kPeakValueY[kPeakRow[i]], buf);
    }
}

} // namespace GMeterUi
