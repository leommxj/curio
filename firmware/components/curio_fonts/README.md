# Fonts

`NotoSansSC-Regular-sub.ttf` is the unchanged font from the [Mosaico factory firmware](https://github.com/esp-mosaico/esp-mosaico-claw/blob/1074bac0b11b23c27d2b9953c7205b57122d62be/fatfs_image/system/fonts/NotoSansSC-Regular-sub.ttf).

- Size: 1,182,444 bytes; 3,503 Han characters plus punctuation and other glyphs.
- SHA-256: `05e87bbb097a9ee6697df35dfcb811472e145d7ada813639744bcaa62bb429d2`.
- Copyright: Adobe; SIL Open Font License 1.1, see [OFL.txt](OFL.txt).

The firmware embeds one copy. FreeType renders Chinese at 14, 18, 26, 32 and 48 pixels, sharing a 64-entry bitmap cache. Montserrat supplies Latin text, numbers and LVGL icons. Use `curio_font_<size>` for both measurement and rendering. Characters outside the font's coverage use LVGL's missing-glyph placeholder.

The TTF resides in PSRAM with the application's read-only data. `main/curio_lvgl_memory.c` puts LVGL objects, FreeType workspaces and glyph bitmaps in PSRAM. `freetype.cmake` selects LVGL's FreeType memory and file port, so loading the embedded font needs no disk filesystem.
