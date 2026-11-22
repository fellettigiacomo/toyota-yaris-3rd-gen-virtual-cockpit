#include "gmeter_ui.h"
#include "colors.h"
#include "fonts/fonts.h"
#include "g_meter.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace GMeterUi {

namespace {

// --- g-ball (left of the cockpit's centre column) ---------------------------
// The outer ring is full scale; the inner one is half of it.
constexpr int16_t kBallCx = 122;
constexpr int16_t kBallCy = 86;
constexpr int16_t kBallR = 65;
constexpr float kFullScaleG = 1.0f; // outer ring. A road car on road tyres lives well inside this.
constexpr int16_t kDotR = 7;
constexpr lv_opa_t kOuterRingOpa = 82;
constexpr lv_opa_t kInnerLineOpa = 41;

constexpr int16_t kDividerX[2] = {232, 408};
constexpr int16_t kDividerY = 22;
constexpr int16_t kDividerH = 128;
constexpr lv_opa_t kDividerOpa = 26;

// --- current reading and session peaks (right) ------------------------------
constexpr int16_t kRightX = 424;
constexpr int16_t kCaptionBaseline = 28;
constexpr int16_t kValueBaseline = 64;
constexpr int16_t kStatusBaseline = 60;
constexpr int16_t kUnitGap = 6;

// Tile order: accel top-left, brake bottom-left, left top-right, right
// bottom-right -- the two longitudinal ones stacked in one column, the two
// lateral ones in the other, so a glance compares like with like.
constexpr int16_t kPeakX[2] = {424, 528};
constexpr int16_t kPeakCaptionBaseline[2] = {92, 138};
constexpr int16_t kPeakValueBaseline[2] = {116, 162};

enum Peak { PeakAccel = 0, PeakBrake, PeakLeft, PeakRight, PeakCount };
const char *kPeakCaption[PeakCount] = {"PEAK ACCEL", "PEAK BRAKE", "PEAK LEFT", "PEAK RIGHT"};
constexpr int16_t kPeakCol[PeakCount] = {0, 0, 1, 1};
constexpr int16_t kPeakRow[PeakCount] = {0, 1, 0, 1};

lv_obj_t *g_dot = nullptr;
lv_obj_t *g_caption = nullptr;     // "G-FORCE", or what the car has to do while calibrating
lv_obj_t *g_valueLabel = nullptr;  // "0.42"
lv_obj_t *g_unitLabel = nullptr;   // "G" beside it
lv_obj_t *g_statusLabel = nullptr; // "ZEROING" / "LEARNING" / "NO SENSOR", in the value's place
lv_obj_t *g_peakValue[PeakCount] = {nullptr, nullptr, nullptr, nullptr};

GMeter::Phase g_shownPhase = GMeter::Phase::NoSensor;
bool g_phaseValid = false;

lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color,
                    int16_t letterSpace = 0) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, letterSpace, 0);
    lv_label_set_text(l, txt);
    return l;
}

void placeOnBaseline(lv_obj_t *label, int16_t x, int16_t baseline) {
    const lv_font_t *f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_obj_set_pos(label, x, static_cast<int16_t>(baseline - (f->line_height - f->base_line)));
}

int16_t textW(lv_obj_t *label) {
    lv_point_t sz;
    lv_txt_get_size(&sz, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                    lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    return static_cast<int16_t>(sz.x);
}

void setText(lv_obj_t *label, const char *txt) {
    if (strcmp(lv_label_get_text(label), txt) != 0) lv_label_set_text(label, txt);
}

lv_obj_t *makeLine(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, lv_opa_t opa) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, w, h);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_bg_color(line, Colors::kText, 0);
    lv_obj_set_style_bg_opa(line, opa, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

void makeRing(lv_obj_t *parent, int16_t r, lv_opa_t opa) {
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, static_cast<int16_t>(2 * r + 1), static_cast<int16_t>(2 * r + 1));
    lv_obj_set_pos(ring, static_cast<int16_t>(kBallCx - r), static_cast<int16_t>(kBallCy - r));
    lv_obj_set_style_border_color(ring, Colors::kText, 0);
    lv_obj_set_style_border_opa(ring, opa, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
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

// While calibrating, the caption says what the car has to do next.
void setCaption(GMeter::Phase phase, float learnPct) {
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
            snprintf(buf, sizeof(buf), "G-FORCE");
            break;
    }
    setText(g_caption, buf);
}

} // namespace

void build(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 640, 172);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    for (int16_t x : kDividerX) makeLine(root, x, kDividerY, 1, kDividerH, kDividerOpa);

    // Crosshair first, so the rings and the dot draw over it.
    makeLine(root, static_cast<int16_t>(kBallCx - kBallR), kBallCy, static_cast<int16_t>(2 * kBallR + 1), 1,
             kInnerLineOpa);
    makeLine(root, kBallCx, static_cast<int16_t>(kBallCy - kBallR), 1, static_cast<int16_t>(2 * kBallR + 1),
             kInnerLineOpa);
    makeRing(root, static_cast<int16_t>(kBallR / 2), kInnerLineOpa);
    makeRing(root, kBallR, kOuterRingOpa);

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

    g_caption = makeLabel(root, "G-FORCE", &spacegrotesk_12_label, Colors::kMutedText, 1);
    placeOnBaseline(g_caption, kRightX, kCaptionBaseline);

    g_valueLabel = makeLabel(root, "0.00", &spacegrotesk_35_value, Colors::kAccentCyan);
    placeOnBaseline(g_valueLabel, kRightX, kValueBaseline);
    g_unitLabel = makeLabel(root, "G", &spacegrotesk_19_value, Colors::kMutedText);
    placeOnBaseline(g_unitLabel, static_cast<int16_t>(kRightX + textW(g_valueLabel) + kUnitGap), kValueBaseline);

    g_statusLabel = makeLabel(root, "", &spacegrotesk_23_label, Colors::kText);
    placeOnBaseline(g_statusLabel, kRightX, kStatusBaseline);

    for (int i = 0; i < PeakCount; i++) {
        int16_t x = kPeakX[kPeakCol[i]];
        lv_obj_t *caption = makeLabel(root, kPeakCaption[i], &spacegrotesk_12_label, Colors::kMutedText, 1);
        placeOnBaseline(caption, x, kPeakCaptionBaseline[kPeakRow[i]]);
        g_peakValue[i] = makeLabel(root, "0.00 G", &spacegrotesk_19_value, Colors::kText);
        placeOnBaseline(g_peakValue[i], x, kPeakValueBaseline[kPeakRow[i]]);
    }
}

void update(const VehicleState &) {
    GMeter::Reading r = GMeter::get();
    bool ready = (r.phase == GMeter::Phase::Ready);

    // The value/unit pair and the status word share one slot -- swap them
    // only on an actual phase change, not every frame.
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
            setText(g_statusLabel, statusWord(r.phase));
        }
        // Peaks read 0.00 G until something has actually been measured, which
        // would otherwise look like a reading rather than an empty slate.
        // Dim them until the axes exist.
        lv_color_t peakColor = ready ? Colors::kText : Colors::kTickDim;
        for (int i = 0; i < PeakCount; i++) {
            lv_obj_set_style_text_color(g_peakValue[i], peakColor, 0);
        }
    }
    setCaption(r.phase, r.learnPct);

    if (!ready) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(r.magG));
    setText(g_valueLabel, buf);
    placeOnBaseline(g_unitLabel, static_cast<int16_t>(kRightX + textW(g_valueLabel) + kUnitGap), kValueBaseline);

    // Dot position: +long is up the screen, +lat is to the right. Clamped to
    // the outer ring so an off-scale hit parks on the rim instead of
    // wandering across the dividers.
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
        setText(g_peakValue[i], buf);
    }
}

} // namespace GMeterUi
