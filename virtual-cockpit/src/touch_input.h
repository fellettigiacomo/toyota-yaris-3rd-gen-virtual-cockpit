#pragma once

// Brings up the AXS15231B touch controller and registers it as an LVGL
// pointer input device, so lv_tileview can be swiped by finger on real
// hardware. UNVERIFIED ON REAL HARDWARE -- see touch_input.cpp's header
// comment for exactly what's derived-but-unconfirmed (the swap/mirror
// coordinate mapping) versus directly copied from proven code.
namespace TouchInput {

// Brings up the touch I2C bus + controller and registers the LVGL indev.
// Call once from the LVGL task, after initLvgl() (lv_disp_drv_register())
// and before the render loop starts, same rule as CockpitUi::build().
void begin();

} // namespace TouchInput
