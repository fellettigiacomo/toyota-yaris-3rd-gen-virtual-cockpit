/*******************************************************************************
 * Size: 14 px
 * Bpp: 4
 * Opts: --font fonts_src/DIN_Next_LT_Pro_Bold_Synthetic.otf --size 14 --bpp 4 --format lvgl --symbols CHGPWR -o src/ui/fonts/dinnext_14_chgpwr.c --lv-font-name dinnext_14_chgpwr --no-kerning
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef DINNEXT_14_CHGPWR
#define DINNEXT_14_CHGPWR 1
#endif

#if DINNEXT_14_CHGPWR

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0043 "C" */
    0x0, 0x46, 0xfe, 0x18, 0x2, 0xdc, 0xce, 0xd1,
    0x24, 0x3e, 0xca, 0x14, 0x70, 0xa0, 0x2, 0xfa,
    0x98, 0x18, 0x7, 0xff, 0x4, 0xc0, 0xc0, 0x39,
    0xc2, 0x80, 0xb, 0xea, 0x48, 0x7d, 0x94, 0x28,
    0x16, 0xc0, 0x66, 0xd1,

    /* U+0047 "G" */
    0x0, 0x46, 0xfe, 0x28, 0x5, 0x6e, 0x67, 0x52,
    0x1, 0x21, 0xf6, 0x68, 0x98, 0x38, 0x50, 0x0,
    0xb9, 0xc0, 0xc0, 0xc1, 0xaa, 0x92, 0x1, 0xd8,
    0xaa, 0x61, 0x30, 0x30, 0xaf, 0x30, 0x17, 0xa,
    0x0, 0x11, 0x90, 0x12, 0x1f, 0x66, 0x84, 0x0,
    0x2d, 0xc8, 0xcd, 0x4a, 0x0,

    /* U+0048 "H" */
    0xf, 0x90, 0x9, 0xfc, 0x84, 0xc, 0x2, 0xf0,
    0x10, 0xf, 0xfe, 0x42, 0x55, 0x4a, 0x1, 0x85,
    0x56, 0x20, 0xc, 0xbf, 0xf3, 0x80, 0x7f, 0xf2,
    0x4, 0xc, 0x2, 0xf0, 0x10,

    /* U+0050 "P" */
    0xf, 0xfd, 0xb0, 0x2, 0x2, 0x88, 0x37, 0x80,
    0x2, 0x5d, 0xb8, 0x58, 0x3, 0xfc, 0x97, 0x6e,
    0x16, 0x0, 0xa, 0x20, 0xde, 0x0, 0xb, 0xfe,
    0xd8, 0x0, 0xff, 0xe2, 0x88, 0x18, 0x7, 0x0,

    /* U+0052 "R" */
    0xf, 0xfd, 0xb0, 0x0, 0x10, 0x14, 0x41, 0xbc,
    0x0, 0x49, 0x76, 0xe0, 0x60, 0xf, 0x84, 0xc0,
    0x24, 0xbb, 0x68, 0xa8, 0x4, 0x28, 0x82, 0x78,
    0x0, 0x97, 0xe8, 0x8, 0x3, 0xca, 0x4a, 0x40,
    0x1e, 0x80, 0x80, 0x10, 0x30, 0x2, 0x92, 0x80,

    /* U+0057 "W" */
    0xdc, 0x0, 0xf, 0x40, 0x0, 0xfd, 0x5c, 0xc0,
    0x8, 0x28, 0x0, 0xb0, 0x7c, 0x4, 0xc, 0x3,
    0x10, 0x31, 0x24, 0x3, 0x4, 0x40, 0x20, 0x22,
    0x0, 0x87, 0x4, 0xb8, 0xb0, 0xc3, 0x0, 0x8,
    0x87, 0x4, 0x54, 0x50, 0x40, 0x6, 0x5, 0x90,
    0xe1, 0x50, 0x90, 0x1, 0x0, 0x94, 0x10, 0xd,
    0x0, 0x22, 0x10, 0xc0, 0x2, 0x6, 0x0, 0x67,
    0x14, 0x0, 0x60, 0x20, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 127, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 36, .adv_w = 139, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 81, .adv_w = 146, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 133, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 138, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 182, .adv_w = 184, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x4, 0x5, 0xd, 0xf, 0x14
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 67, .range_length = 21, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 6, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
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
const lv_font_t dinnext_14_chgpwr = {
#else
lv_font_t dinnext_14_chgpwr = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
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



#endif /*#if DINNEXT_14_CHGPWR*/

