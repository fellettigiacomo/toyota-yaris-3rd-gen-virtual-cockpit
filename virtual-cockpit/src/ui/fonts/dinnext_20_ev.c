/*******************************************************************************
 * Size: 20 px
 * Bpp: 4
 * Opts: --font fonts_src/DIN_Next_LT_Pro_Bold_Synthetic.otf --size 20 --bpp 4 --format lvgl --symbols EV -o src/ui/fonts/dinnext_20_ev.c --lv-font-name dinnext_20_ev --no-kerning
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef DINNEXT_20_EV
#define DINNEXT_20_EV 1
#endif

#if DINNEXT_20_EV

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0045 "E" */
    0x9f, 0xff, 0xd0, 0x60, 0x1f, 0x84, 0x0, 0x33,
    0x3e, 0xb0, 0x1, 0x33, 0x78, 0x80, 0x3f, 0xf8,
    0x4, 0xcd, 0xc6, 0x1, 0xc, 0xcf, 0x80, 0x3f,
    0x88, 0x2, 0x3f, 0xfe, 0x80, 0xf, 0xfe, 0x59,
    0x33, 0x78, 0x80, 0x3, 0x33, 0xeb, 0x30, 0xf,
    0xc2,

    /* U+0056 "V" */
    0xef, 0x70, 0xe, 0x4f, 0xf1, 0x28, 0x58, 0x7,
    0x68, 0x9, 0x70, 0x18, 0x80, 0x64, 0x7, 0x5,
    0x0, 0x38, 0x4, 0x44, 0xa, 0x0, 0x28, 0x68,
    0x4, 0xa0, 0x26, 0x0, 0xf0, 0x41, 0x0, 0x58,
    0x30, 0x4, 0xa0, 0x7, 0x1, 0x30, 0xb0, 0xc,
    0xa1, 0xa0, 0xe0, 0x46, 0x1, 0xb8, 0x10, 0x68,
    0x28, 0x3, 0x90, 0x41, 0x8c, 0x14, 0x3, 0xce,
    0x10, 0x6, 0x40, 0x1e, 0xa0, 0xa, 0xc0, 0x3e,
    0x31, 0x0, 0x28, 0x7, 0xe9, 0x4, 0x20, 0xc
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 180, .box_w = 10, .box_h = 14, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 41, .adv_w = 180, .box_w = 12, .box_h = 14, .ofs_x = 0, .ofs_y = 0}
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
const lv_font_t dinnext_20_ev = {
#else
lv_font_t dinnext_20_ev = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if DINNEXT_20_EV*/

