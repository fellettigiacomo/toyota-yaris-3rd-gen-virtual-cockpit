#pragma once

#include <SDL2/SDL.h>

// Registers an LVGL pointer indev backed by the mouse, so lv_tileview can be
// "swiped" with a click-and-drag exactly like a finger on the real touch
// panel. This exercises the same tileview swipe/snap logic touch_input.cpp
// feeds on hardware; it does not stand in for touch_input.cpp itself (which
// is ESP32/I2C-specific and out of scope for a desktop build).
namespace SdlIndevPort {

void init();

// Feed every SDL_Event here; non-mouse events are ignored. `renderer` is
// needed to map window pixel coordinates back to the 640x172 logical
// coordinates LVGL expects, via SDL_RenderWindowToLogical.
void handleEvent(const SDL_Event &e, SDL_Renderer *renderer);

} // namespace SdlIndevPort
