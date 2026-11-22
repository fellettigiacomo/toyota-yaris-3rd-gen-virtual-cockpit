#include "glow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace Glow {

namespace {

constexpr int kW = 640;
constexpr int kH = 172;
constexpr float kStrength = 0.40f;
constexpr float kReachX = 0.75f; // ellipse radii, as fractions of the screen
constexpr float kReachY = 0.95f;

lv_img_dsc_t g_image = {};
bool g_ready = false;

lv_color_t *allocFrame() {
    size_t bytes = static_cast<size_t>(kW) * kH * sizeof(lv_color_t);
#ifdef ESP_PLATFORM
    return static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    return static_cast<lv_color_t *>(malloc(bytes));
#endif
}

// Share of the glow colour at normalised elliptical distance r from the bottom
// centre: full at the edge, 40% at r = 0.42, gone at r = 1. The outer stretch
// eases out (smoothstep) instead of ending on a slope, which the eye would
// pick out as a hard edge against the near-black above it.
float falloff(float r) {
    if (r < 0.42f) return 1.0f - 0.6f * r / 0.42f;
    if (r >= 1.0f) return 0.0f;
    float u = (r - 0.42f) / 0.58f;
    return 0.4f * (1.0f - u * u * (3.0f - 2.0f * u));
}

const lv_img_dsc_t *image() {
    if (g_ready) return &g_image;

    lv_color_t *px = allocFrame();
    static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    // 8-bit channels, before RGB565 truncation: bg is Colors::kBg, keep in sync.
    const float bg[3] = {0x0a, 0x0b, 0x0d};
    const float glow[3] = {0x1f, 0xb0, 0xff};
    const float step[3] = {8, 4, 8}; // RGB565 quantisation step per channel
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x++) {
            float dx = (x + 0.5f - kW / 2.0f) / (kReachX * kW);
            float dy = (kH - (y + 0.5f)) / (kReachY * kH);
            float a = kStrength * falloff(std::sqrt(dx * dx + dy * dy));
            float threshold = kBayer[(y & 3) * 4 + (x & 3)] / 16.0f;
            uint8_t c[3];
            for (int i = 0; i < 3; i++) {
                float v = bg[i] + (glow[i] - bg[i]) * a + threshold * step[i];
                c[i] = static_cast<uint8_t>(std::min(std::max(static_cast<int>(v), 0), 255));
            }
            px[y * kW + x] = lv_color_make(c[0], c[1], c[2]);
        }
    }

    g_image.header.cf = LV_IMG_CF_TRUE_COLOR;
    g_image.header.always_zero = 0;
    g_image.header.w = kW;
    g_image.header.h = kH;
    g_image.data_size = static_cast<uint32_t>(kW) * kH * sizeof(lv_color_t);
    g_image.data = reinterpret_cast<const uint8_t *>(px);
    g_ready = true;
    return &g_image;
}

} // namespace

lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, image());
    lv_obj_set_pos(img, 0, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

} // namespace Glow
