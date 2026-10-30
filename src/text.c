#include "util.h"
#include "fonts.h"
#include "css.h"

int eg_text_width(const char *text, const EgFont *font, int letter_spacing)
{
    if (!text || !font) return 0;
    int w = 0;
    while (*text) {
        const char *p = text;
        uint32_t cp = 0;
        eg_utf8_decode(&p, &cp);
        int n = (int)(p - text);
        text = p;
        const EgGlyph *g = eg_font_glyph(font, cp);
        if (g) w += g->advance + letter_spacing;
        else if (cp == '\t') w += font->size * 2;
        (void)n;
    }
    return w;
}
