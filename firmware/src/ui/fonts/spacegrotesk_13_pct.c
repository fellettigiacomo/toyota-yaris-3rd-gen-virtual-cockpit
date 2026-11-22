/*******************************************************************************
 * Size: 13 px
 * Bpp: 4
 * Opts: --font fonts_src/SpaceGrotesk-SemiBold.ttf --size 13 --bpp 4 --format lvgl --symbols %A -o src/ui/fonts/spacegrotesk_13_pct.c --lv-font-name spacegrotesk_13_pct --no-kerning
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef SPACEGROTESK_13_PCT
#define SPACEGROTESK_13_PCT 1
#endif

#if SPACEGROTESK_13_PCT

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0025 "%" */
    0x0, 0xff, 0xe, 0x7d, 0x88, 0x1, 0xa4, 0x24,
    0x39, 0x98, 0x5, 0x38, 0x2, 0xe0, 0x20, 0xf,
    0x65, 0xb, 0x6e, 0x6, 0xa0, 0x90, 0x1, 0x6f,
    0xe3, 0xaf, 0x80, 0x78, 0xe6, 0x88, 0x3, 0x87,
    0x99, 0x2f, 0xa8, 0x2, 0xa1, 0x99, 0x3e, 0xbb,
    0x3, 0xae, 0x80, 0xb8, 0x80, 0x86, 0xd0, 0x85,
    0xbe, 0xb3, 0x2, 0x90, 0x0, 0x59, 0xf6, 0x20,

    /* U+0041 "A" */
    0x0, 0x37, 0xfa, 0x40, 0x36, 0xa, 0xa0, 0x6,
    0x74, 0xd2, 0x10, 0x0, 0x96, 0xa0, 0x20, 0x1,
    0x1, 0xc, 0x30, 0x1, 0x80, 0x60, 0x68, 0x0,
    0x41, 0x88, 0x1, 0x8, 0x88, 0x5d, 0xc6, 0x8,
    0x80, 0xbf, 0xf7, 0x6, 0x60, 0x10, 0x0, 0x26,
    0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 158, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 56, .adv_w = 132, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x1c
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 37, .range_length = 29, .glyph_id_start = 1,
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
const lv_font_t spacegrotesk_13_pct = {
#else
lv_font_t spacegrotesk_13_pct = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 12,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if SPACEGROTESK_13_PCT*/

