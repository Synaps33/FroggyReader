#ifndef EG_FONTS_H
#define EG_FONTS_H

#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    int16_t w, h;
    int16_t xoff, top;
    uint16_t advance;
    uint32_t offset;
} EgGlyph;

typedef struct {
    const char *name;
    const EgGlyph *glyphs;
    int glyph_count;
    const uint8_t *bitmap;
    int bitmap_size;
    int size;
    int ascent;
    int descent;
    int line_height;
    int bold;
    int italic;
} EgFont;

enum {
    EG_STYLE_REGULAR = 0,
    EG_STYLE_BOLD = 1,
    EG_STYLE_ITALIC = 2,
    EG_STYLE_BOLD_ITALIC = 3
};

#define EG_FONT_COUNT 16
#define EG_UI_FONT_COUNT 6
extern const EgFont eg_fonts[EG_FONT_COUNT];
extern const EgFont eg_ui_fonts[EG_UI_FONT_COUNT];

static inline int eg_ui_font_index(int size, int bold)
{
    int idx;
    if (size <= 12) idx = 0;
    else if (size <= 15) idx = 1;
    else idx = 2;
    return idx * 2 + (bold ? 1 : 0);
}

static inline int eg_font_index(int size, int style)
{
    int size_idx;
    if (size <= 12) size_idx = 0;
    else if (size <= 15) size_idx = 1;
    else if (size <= 19) size_idx = 2;
    else size_idx = 3;
    if (style < 0 || style > 3) style = 0;
    return size_idx * 4 + style;
}

const EgGlyph *eg_font_glyph(const EgFont *font, uint32_t cp);

#endif
