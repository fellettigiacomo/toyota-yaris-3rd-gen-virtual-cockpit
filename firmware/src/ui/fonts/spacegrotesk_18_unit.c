/*******************************************************************************
 * Size: 18 px
 * Bpp: 4
 * Opts: --font fonts_src/SpaceGrotesk-Medium.ttf --size 18 --bpp 4 --format lvgl --symbols L/100A -o src/ui/fonts/spacegrotesk_18_unit.c --lv-font-name spacegrotesk_18_unit --no-kerning
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef SPACEGROTESK_18_UNIT
#define SPACEGROTESK_18_UNIT 1
#endif

#if SPACEGROTESK_18_UNIT

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002F "/" */
    0x0, 0xf7, 0xe8, 0x7, 0x28, 0x28, 0x7, 0x70,
    0x50, 0x7, 0x21, 0x10, 0x3, 0x28, 0x28, 0x7,
    0x78, 0x50, 0x7, 0x29, 0x10, 0x3, 0x28, 0x28,
    0x7, 0x78, 0x50, 0x7, 0x29, 0x10, 0x3, 0x28,
    0x28, 0x7, 0x78, 0x58, 0x7, 0x29, 0x18, 0x6,
    0x50, 0x50, 0xe, 0xf0, 0xb0, 0xe, 0x52, 0x30,
    0xc,

    /* U+0030 "0" */
    0x0, 0xc, 0x6f, 0xfb, 0x14, 0x2, 0x1f, 0x73,
    0x43, 0x35, 0x48, 0x2, 0x5, 0x7a, 0xf3, 0x45,
    0x8c, 0x1c, 0x2c, 0x2, 0x29, 0xb, 0x1, 0x18,
    0x3, 0x18, 0x8, 0x7, 0xf8, 0xc0, 0x3f, 0xf9,
    0x26, 0x2, 0x30, 0x6, 0x30, 0x10, 0x40, 0xb0,
    0x8, 0xa4, 0x2c, 0x2c, 0x57, 0xaf, 0x34, 0x58,
    0xc0, 0x7d, 0x8d, 0xc, 0xd3, 0x20,

    /* U+0031 "1" */
    0x0, 0x1f, 0xfd, 0x0, 0x8, 0x5, 0x0, 0x8d,
    0x11, 0x80, 0x17, 0x84, 0x0, 0x48, 0x68, 0x60,
    0x13, 0xd5, 0x0, 0x31, 0xa8, 0x80, 0x7f, 0xf6,
    0xc0,

    /* U+0041 "A" */
    0x0, 0x8b, 0xfe, 0x80, 0xf, 0x28, 0x30, 0x28,
    0x7, 0xac, 0x2c, 0xc8, 0x80, 0x18, 0x4c, 0x89,
    0x60, 0x80, 0x19, 0x1, 0x41, 0x43, 0x40, 0x37,
    0x85, 0x81, 0x11, 0x0, 0x32, 0x89, 0x80, 0x14,
    0x14, 0x0, 0x80, 0x39, 0x92, 0x87, 0x80, 0x34,
    0x0, 0x67, 0x10, 0x20, 0x1, 0x3, 0xbf, 0xf2,
    0x9, 0x91, 0xc, 0x40, 0x36, 0x85, 0xa8, 0x20,
    0x7, 0x20, 0x28,

    /* U+004C "L" */
    0xbf, 0x40, 0xf, 0xff, 0xf8, 0x7, 0xff, 0xa,
    0x2e, 0xf8, 0xc0, 0x6, 0x89, 0xcc
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 109, .box_w = 8, .box_h = 16, .ofs_x = -1, .ofs_y = -4},
    {.bitmap_index = 49, .adv_w = 186, .box_w = 11, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 103, .adv_w = 125, .box_w = 7, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 128, .adv_w = 182, .box_w = 11, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 187, .adv_w = 157, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x1, 0x2, 0x12, 0x1d
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 47, .range_length = 30, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 5, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
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
const lv_font_t spacegrotesk_18_unit = {
#else
lv_font_t spacegrotesk_18_unit = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if SPACEGROTESK_18_UNIT*/

