/*******************************************************************************
 * Size: 28 px
 * Bpp: 4
 * Opts: --font fonts_src/DIN_Next_LT_Pro_Bold_Synthetic.otf --size 28 --bpp 4 --format lvgl --symbols EV -o src/ui/fonts/dinnext_28_ev.c --lv-font-name dinnext_28_ev --no-kerning
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef DINNEXT_28_EV
#define DINNEXT_28_EV 1
#endif

#if DINNEXT_28_EV

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0045 "E" */
    0x1e, 0xff, 0xff, 0xa8, 0xc4, 0x3, 0xfe, 0x70,
    0xf, 0xfe, 0x33, 0x4c, 0xfe, 0xe0, 0xd, 0x8c,
    0xdf, 0x8c, 0x3, 0xff, 0xa9, 0x8c, 0xdf, 0x8,
    0x7, 0x34, 0xcf, 0xcc, 0x1, 0xff, 0xcf, 0x20,
    0xe, 0xaf, 0xff, 0x94, 0x3, 0xff, 0xc7, 0x8c,
    0xdf, 0x8c, 0x3, 0x34, 0xcf, 0xee, 0x0, 0xff,
    0xe1, 0x18, 0x80, 0x7f, 0xcc,

    /* U+0056 "V" */
    0xdf, 0xf3, 0x0, 0x7e, 0xaf, 0xf4, 0x8, 0x2,
    0x80, 0x3f, 0x28, 0x3, 0xe8, 0x0, 0x62, 0x1,
    0xe5, 0x0, 0x95, 0xc0, 0x24, 0x0, 0xf6, 0x0,
    0xc, 0x85, 0x0, 0x1e, 0x1, 0xe7, 0x0, 0x50,
    0x3, 0x80, 0xa, 0x1, 0xc6, 0x20, 0x7, 0x0,
    0x28, 0x4, 0x80, 0x1a, 0xc0, 0x8, 0x20, 0x12,
    0x80, 0x38, 0x3, 0x28, 0x3, 0x80, 0x37, 0x0,
    0x10, 0x2, 0x22, 0x0, 0x14, 0x3, 0x20, 0x80,
    0xa0, 0x1, 0x40, 0xa, 0x1, 0xe6, 0x0, 0x68,
    0x2, 0xc0, 0x1c, 0x1, 0xeb, 0x0, 0x38, 0x11,
    0x80, 0xa0, 0x7, 0x8c, 0x80, 0x51, 0x40, 0xc,
    0x1, 0xfa, 0x80, 0x1d, 0x60, 0xb, 0x0, 0xfc,
    0xa0, 0x5, 0x30, 0x23, 0x0, 0xfc, 0x46, 0x1,
    0xa8, 0x3, 0xfd, 0x40, 0x19, 0x40, 0x3f, 0xce,
    0x1, 0x19, 0x0, 0x7f, 0x85, 0x40, 0x14, 0x1,
    0xe0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 252, .box_w = 14, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 53, .adv_w = 252, .box_w = 16, .box_h = 19, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x11
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 69, .range_length = 18, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t dinnext_28_ev = {
#else
lv_font_t dinnext_28_ev = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 19,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if DINNEXT_28_EV*/

