#include "lv_port_disp_sdl.h"
#include <lvgl.h>

#include <algorithm>
#include <cstdlib>

namespace SdlDispPort {

namespace {

SDL_Renderer *g_renderer = nullptr;
SDL_Texture *g_texture = nullptr;

lv_disp_draw_buf_t g_drawBuf;
lv_disp_drv_t g_dispDrv;
lv_color_t *g_buf1 = nullptr;
lv_color_t *g_buf2 = nullptr;

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    SDL_Rect rect{area->x1, area->y1, w, h};
    void *pixelsVoid = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(g_texture, &rect, &pixelsVoid, &pitch) == 0) {
        uint8_t *pixels = static_cast<uint8_t *>(pixelsVoid);
        for (int y = 0; y < h; y++) {
            uint16_t *rowOut = reinterpret_cast<uint16_t *>(pixels + y * pitch);
            for (int x = 0; x < w; x++) {
                // LV_COLOR_16_SWAP=1 (include/lv_conf.h) pre-swaps each pixel's
                // bytes for the real QSPI panel's wire format -- undo that
                // here since SDL's RGB565 texture wants normal-endian values.
                uint16_t v = color_p[y * w + x].full;
                rowOut[x] = static_cast<uint16_t>((v >> 8) | (v << 8));
            }
        }
        SDL_UnlockTexture(g_texture);
    }

    lv_disp_flush_ready(drv);
}

} // namespace

void init(SDL_Renderer *renderer, int logicalW, int logicalH) {
    g_renderer = renderer;

    g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                   logicalW, logicalH);

    size_t bufPixels = static_cast<size_t>(logicalW) * logicalH;
    g_buf1 = static_cast<lv_color_t *>(malloc(bufPixels * sizeof(lv_color_t)));
    g_buf2 = static_cast<lv_color_t *>(malloc(bufPixels * sizeof(lv_color_t)));
    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, bufPixels);

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = logicalW;
    g_dispDrv.ver_res = logicalH;
    g_dispDrv.flush_cb = flushCb;
    g_dispDrv.draw_buf = &g_drawBuf;
    lv_disp_drv_register(&g_dispDrv);
}

void present() {
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

} // namespace SdlDispPort
