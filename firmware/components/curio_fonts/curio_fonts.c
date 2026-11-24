#include "curio_fonts.h"

extern const unsigned char font_start[] asm("_binary_NotoSansSC_Regular_sub_ttf_start");
extern const unsigned char font_end[] asm("_binary_NotoSansSC_Regular_sub_ttf_end");

lv_font_t curio_font_14, curio_font_18, curio_font_26, curio_font_32, curio_font_48;

bool curio_fonts_init(void)
{
    const struct { lv_font_t *font; const lv_font_t *latin; unsigned size; } fonts[] = {
        {&curio_font_14, &lv_font_montserrat_14, 14},
        {&curio_font_18, &lv_font_montserrat_18, 18},
        {&curio_font_26, &lv_font_montserrat_26, 26},
        {&curio_font_32, &lv_font_montserrat_32, 32},
        {&curio_font_48, &lv_font_montserrat_48, 48},
    };
    lv_fs_path_ex_t path;
    lv_fs_make_path_from_buffer(&path, LV_FS_MEMFS_LETTER, font_start, font_end - font_start, "ttf");
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        *fonts[i].font = *fonts[i].latin;
        fonts[i].font->fallback = lv_freetype_font_create((const char *)&path,
            LV_FREETYPE_FONT_RENDER_MODE_BITMAP, fonts[i].size, LV_FREETYPE_FONT_STYLE_NORMAL);
        if (!fonts[i].font->fallback) return false;
    }
    return true;
}
