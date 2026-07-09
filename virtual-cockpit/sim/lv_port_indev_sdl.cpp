#include "lv_port_indev_sdl.h"
#include <lvgl.h>

namespace SdlIndevPort {

namespace {

int g_x = 0;
int g_y = 0;
bool g_pressed = false;

void readCb(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point.x = g_x;
    data->point.y = g_y;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace

void init() {
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = readCb;
    lv_indev_drv_register(&drv);
}

void handleEvent(const SDL_Event &e, SDL_Renderer *renderer) {
    int rawX = 0, rawY = 0;
    bool haveCoords = true;

    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN:
            rawX = e.button.x;
            rawY = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) g_pressed = true;
            break;
        case SDL_MOUSEBUTTONUP:
            rawX = e.button.x;
            rawY = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) g_pressed = false;
            break;
        case SDL_MOUSEMOTION:
            rawX = e.motion.x;
            rawY = e.motion.y;
            break;
        default:
            haveCoords = false;
            break;
    }

    if (haveCoords) {
        float lx = 0.0f, ly = 0.0f;
        SDL_RenderWindowToLogical(renderer, rawX, rawY, &lx, &ly);
        g_x = static_cast<int>(lx);
        g_y = static_cast<int>(ly);
    }
}

} // namespace SdlIndevPort
