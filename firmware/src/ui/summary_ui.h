#pragma once

#include <lvgl.h>

// Trip summary: shown over whatever screen is up once the car has been
// switched off, for the seconds the board stays powered afterwards. Eight
// figures from HybridStats in two rows of four. build() creates it hidden;
// show()/hide() are AppUi's to call, from the LVGL task like every screen.
namespace SummaryUi {

void build(lv_obj_t *parent);
void show(); // refreshes the figures from HybridStats, then unhides
void hide();

} // namespace SummaryUi
