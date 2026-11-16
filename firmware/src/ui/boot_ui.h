#pragma once

#include <lvgl.h>

// Power-on splash: the Toyota logo, then the GR Yaris one, then it gets
// out of the way and leaves the cockpit underneath. Drawn as an opaque
// overlay on top of the already-built screens, so the UI behind it is live
// (and its stats already integrating) the whole time it plays.
//
// Driven by its own lv_timer from elapsed time, not by a chain of lv_anims,
// so skip() is one call and the timeline lives in one function.
namespace BootUi {

// Builds the overlay on `parent` and starts the sequence. Call last in
// AppUi::build(), so it is drawn above every screen container.
void build(lv_obj_t *parent);

// True while the splash is still on the panel.
bool active();

// Ends the splash immediately (BOOT button / tap while it plays).
void skip();

} // namespace BootUi
