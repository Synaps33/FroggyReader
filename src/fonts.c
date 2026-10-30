#include <stddef.h>
#include "fonts.h"

const EgGlyph *eg_font_glyph(const EgFont *font, uint32_t cp)
{
    if (!font || !font->glyphs || font->glyph_count <= 0) return NULL;
    if (cp == 0x00A0 || cp == 0x202F || cp == 0x2007) cp = ' ';
    if (cp < 0x20 && cp != '\t') return NULL;
    int lo = 0, hi = font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t c = font->glyphs[mid].codepoint;
        if (c == cp) return &font->glyphs[mid];
        if (c < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    if (cp != '?') return eg_font_glyph(font, '?');
    return NULL;
}
