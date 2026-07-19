#include <lvgl.h>
#include <SDL2/SDL.h>

#include "lv_port_disp_sdl.h"

#include "ui/app_ui.h"
#include "can_decoder.h"
#include "app_config.h"
#include "screen_nav.h"
#include "touch_nav.h"

#include <cstdint>
#include <cstdio>
#include <ctime>

// Arduino.h shim's simulated GPIO for screen_nav.cpp's BOOT-button read;
// set from the spacebar below.
extern bool g_simButtonHeld;

// Wire.h shim's simulated touch-controller response for touch_nav.cpp; set
// from the left mouse button below -- a click anywhere in the window stands
// in for a tap anywhere on the panel (TouchNav never looks at coordinates).
extern bool g_simTouchHeld;

namespace {
constexpr int kLogicalW = 640; // matches the real panel's logical (post-rotation) resolution
constexpr int kLogicalH = 172;
constexpr int kWindowScale = 3; // the real panel is tiny -- scale the window up for a Mac screen
} // namespace

int main(int, char **) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window =
        SDL_CreateWindow("Yaris Virtual Cockpit -- Simulator", SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED, kLogicalW * kWindowScale, kLogicalH * kWindowScale,
                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }
    // Lets the window be freely resized while everything drawn through
    // `renderer` still uses 640x172 coordinates -- SDL handles the scaling.
    SDL_RenderSetLogicalSize(renderer, kLogicalW, kLogicalH);

    lv_init();
    SdlDispPort::init(renderer, kLogicalW, kLogicalH);

    ScreenNav::begin();
    TouchNav::begin();
    CanDecoder::begin();
    AppUi::build();

    std::printf("Yaris virtual cockpit simulator -- press SPACE or click anywhere in the window to "
                "cycle screens, close the window to quit.\n");

    uint32_t lastTickMs = SDL_GetTicks();
    uint32_t lastSyncMs = 0;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE && !e.key.repeat) {
                g_simButtonHeld = true;
            } else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE) {
                g_simButtonHeld = false;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                g_simTouchHeld = true;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                g_simTouchHeld = false;
            }
        }

        uint32_t nowMs = SDL_GetTicks();
        lv_tick_inc(nowMs - lastTickMs);
        lastTickMs = nowMs;

        lv_timer_handler();
        SdlDispPort::present();

        if (nowMs - lastSyncMs >= UI_SYNC_INTERVAL_MS) {
            lastSyncMs = nowMs;
            VehicleState state = CanDecoder::getSnapshot();
            AppUi::update(state);
        }

        SDL_Delay(LVGL_TASK_DELAY_MS);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
