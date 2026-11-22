/*******************************************************************************
 * Size: 27 px
 * Bpp: 4
 * Opts: --font fonts_src/SpaceGrotesk-SemiBold.ttf --size 27 --bpp 4 --format lvgl --symbols EV -o src/ui/fonts/spacegrotesk_27_ev.c --lv-font-name spacegrotesk_27_ev --no-kerning
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef SPACEGROTESK_27_EV
#define SPACEGROTESK_27_EV 1
#endif

#if SPACEGROTESK_27_EV

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0045 "E" */
    0x2f, 0xff, 0xfb, 0x80, 0x3f, 0xfa, 0xbb, 0xff,
    0xf7, 0x0, 0x7f, 0xfc, 0x77, 0xff, 0xe2, 0x0,
    0xff, 0xea, 0xef, 0xff, 0xc4, 0x1, 0xff, 0xf1,
    0xdf, 0xff, 0xc0, 0x1f, 0xfd, 0x10,

    /* U+0056 "V" */
    0x5f, 0xf8, 0x3, 0xf2, 0x7f, 0xc0, 0x80, 0x12,
    0x0, 0x7d, 0x80, 0x3, 0x1, 0x30, 0x6, 0x0,
    0x7c, 0x80, 0x4, 0x0, 0x20, 0x1, 0x0, 0x3c,
    0x26, 0x0, 0xb0, 0x6, 0x0, 0xc, 0x3, 0xc8,
    0x1, 0x18, 0x1, 0x0, 0x24, 0x0, 0xec, 0x0,
    0x20, 0x6, 0x40, 0x6, 0x0, 0x72, 0x0, 0x30,
    0x3, 0x60, 0x1, 0x0, 0x30, 0x90, 0x1, 0x40,
    0x32, 0x0, 0xc, 0x3, 0x20, 0x0, 0x48, 0x3,
    0x18, 0x80, 0x10, 0x2, 0xc0, 0x2, 0x0, 0x79,
    0x0, 0x18, 0x1, 0x20, 0x3, 0x0, 0x3d, 0x80,
    0x4, 0x0, 0x9, 0x0, 0x1c, 0x3, 0xc8, 0x0,
    0x30, 0x2, 0x0, 0x8, 0x40, 0x3c, 0x42, 0x0,
    0x40, 0xc0, 0x2, 0x0, 0x7e, 0x40, 0x6, 0x2,
    0x0, 0x30, 0x3, 0xf7, 0x80, 0x10, 0x48, 0x0,
    0x80, 0x1f, 0x90, 0x0, 0x6c, 0x0, 0x31, 0x0,
    0xfc, 0x24, 0x0, 0x80, 0x2, 0x0, 0x7f, 0x90,
    0x3, 0xb0, 0x3, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 240, .box_w = 14, .box_h = 19, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 267, .box_w = 17, .box_h = 19, .ofs_x = 0, .ofs_y = 0}
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
const lv_font_t spacegrotesk_27_ev = {
#else
lv_font_t spacegrotesk_27_ev = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 19,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -3,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if SPACEGROTESK_27_EV*/

