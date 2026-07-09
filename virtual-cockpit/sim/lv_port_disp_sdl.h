#pragma once

#include <SDL2/SDL.h>

// Registers an LVGL display driver that flushes into an SDL2 texture at the
// real panel's logical resolution (640x172), letting main_sim.cpp stretch
// that texture to fill whatever window size it likes.
namespace SdlDispPort {

void init(SDL_Renderer *renderer, int logicalW, int logicalH);

// Call once per frame after lv_timer_handler(): presents whatever the last
// flush_cb wrote into the texture.
void present();

} // namespace SdlDispPort
