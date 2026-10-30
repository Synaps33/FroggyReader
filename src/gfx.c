#include "gfx.h"
#include "fonts.h"

#include <string.h>

static const EgFont *style_face(const EgStyle *s)
{
    if (s->sans)
        return &eg_ui_fonts[eg_ui_font_index(s->font_size, s->bold)];
    int idx = eg_font_index(s->font_size, s->bold ? (s->italic ? EG_STYLE_BOLD_ITALIC : EG_STYLE_BOLD)
                                                   : (s->italic ? EG_STYLE_ITALIC : EG_STYLE_REGULAR));
    return &eg_fonts[idx];
}

void eg_gfx_init(EgGfx *g, uint16_t *fb, int w, int h)
{
    g->fb = fb;
    g->w = w;
    g->h = h;
    g->dark_theme = 0;
    g->off_x = g->off_y = 0;
    g->clip_x0 = 0;
    g->clip_y0 = 0;
    g->clip_x1 = w;
    g->clip_y1 = h;
}

void eg_gfx_set_clip(EgGfx *g, int x, int y, int w, int h)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    g->clip_x0 = x0;
    g->clip_y0 = y0;
    g->clip_x1 = x1;
    g->clip_y1 = y1;
}

void eg_gfx_reset_clip(EgGfx *g)
{
    g->clip_x0 = 0;
    g->clip_y0 = 0;
    g->clip_x1 = g->w;
    g->clip_y1 = g->h;
}

void eg_gfx_set_offset(EgGfx *g, int x, int y)
{
    g->off_x = x;
    g->off_y = y;
}

void eg_gfx_clear(EgGfx *g, uint16_t color)
{
    int n = g->w * g->h;
    for (int i = 0; i < n; i++) g->fb[i] = color;
}

void eg_gfx_fill(EgGfx *g, int x, int y, int w, int h, uint16_t color)
{
    x += g->off_x;
    y += g->off_y;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    if (x0 < g->clip_x0) x0 = g->clip_x0;
    if (y0 < g->clip_y0) y0 = g->clip_y0;
    if (x1 > g->clip_x1) x1 = g->clip_x1;
    if (y1 > g->clip_y1) y1 = g->clip_y1;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = g->fb + (size_t)yy * g->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

void eg_gfx_blend(EgGfx *g, int x, int y, int w, int h, uint16_t color, int alpha)
{
    if (alpha <= 0) return;
    if (alpha >= 255) { eg_gfx_fill(g, x, y, w, h, color); return; }
    x += g->off_x;
    y += g->off_y;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    if (x0 < g->clip_x0) x0 = g->clip_x0;
    if (y0 < g->clip_y0) y0 = g->clip_y0;
    if (x1 > g->clip_x1) x1 = g->clip_x1;
    if (y1 > g->clip_y1) y1 = g->clip_y1;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = g->fb + (size_t)yy * g->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = eg_blend565(row[xx], color, alpha);
    }
}

void eg_gfx_hline(EgGfx *g, int x, int y, int w, uint16_t color)
{
    eg_gfx_fill(g, x, y, w, 1, color);
}

void eg_gfx_vline(EgGfx *g, int x, int y, int h, uint16_t color)
{
    eg_gfx_fill(g, x, y, 1, h, color);
}

void eg_gfx_line(EgGfx *g, int x1, int y1, int x2, int y2, uint16_t color, int thickness)
{
    if (thickness < 1) thickness = 1;
    if (y1 == y2) {
        int x = x1 < x2 ? x1 : x2;
        int w = (x1 < x2 ? x2 - x1 : x1 - x2) + 1;
        eg_gfx_fill(g, x, y1, w, thickness, color);
        return;
    }
    if (x1 == x2) {
        int y = y1 < y2 ? y1 : y2;
        int h = (y1 < y2 ? y2 - y1 : y1 - y2) + 1;
        eg_gfx_fill(g, x1, y, thickness, h, color);
        return;
    }
    int dx = x2 - x1;
    int dy = y2 - y1;
    int steps = dx > 0 ? dx : -dx;
    int ady = dy > 0 ? dy : -dy;
    if (ady > steps) steps = ady;
    if (steps <= 0) { eg_gfx_fill(g, x1, y1, thickness, thickness, color); return; }
    for (int i = 0; i <= steps; i++) {
        int x = x1 + dx * i / steps;
        int y = y1 + dy * i / steps;
        eg_gfx_fill(g, x, y, thickness, thickness, color);
    }
}

void eg_gfx_rect(EgGfx *g, int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    eg_gfx_hline(g, x, y, w, color);
    eg_gfx_hline(g, x, y + h - 1, w, color);
    eg_gfx_vline(g, x, y, h, color);
    eg_gfx_vline(g, x + w - 1, y, h, color);
}

void eg_gfx_round_rect(EgGfx *g, int x, int y, int w, int h, int radius,
                       uint16_t fill, int has_fill, uint16_t border)
{
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (has_fill) {
        eg_gfx_fill(g, x + radius, y, w - radius * 2, h, fill);
        eg_gfx_fill(g, x, y + radius, radius, h - radius * 2, fill);
        eg_gfx_fill(g, x + w - radius, y + radius, radius, h - radius * 2, fill);
    }
    if (border) {
        eg_gfx_hline(g, x + radius, y, w - radius * 2, border);
        eg_gfx_hline(g, x + radius, y + h - 1, w - radius * 2, border);
        eg_gfx_vline(g, x, y + radius, h - radius * 2, border);
        eg_gfx_vline(g, x + w - 1, y + radius, h - radius * 2, border);
    }
}

void eg_gfx_vgradient(EgGfx *g, int x, int y, int w, int h,
                      uint16_t top, uint16_t bottom)
{
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        uint16_t c = top;
        if (h > 1) c = eg_gfx_mix(top, bottom, i * 255 / (h - 1));
        eg_gfx_fill(g, x, y + i, w, 1, c);
    }
}

void eg_gfx_image(EgGfx *g, const EgImage *img, int x, int y, int w, int h)
{
    if (!img || !img->pixels || w <= 0 || h <= 0) return;
    x += g->off_x;
    y += g->off_y;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    if (x0 < g->clip_x0) x0 = g->clip_x0;
    if (y0 < g->clip_y0) y0 = g->clip_y0;
    if (x1 > g->clip_x1) x1 = g->clip_x1;
    if (y1 > g->clip_y1) y1 = g->clip_y1;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; yy++) {
        int sy = (yy - y) * img->h / h;
        if (sy >= img->h) sy = img->h - 1;
        uint16_t *row = g->fb + (size_t)yy * g->w;
        for (int xx = x0; xx < x1; xx++) {
            int sx = (xx - x) * img->w / w;
            if (sx >= img->w) sx = img->w - 1;
            row[xx] = img->pixels[(size_t)sy * img->w + sx];
        }
    }
}

static void draw_glyph(EgGfx *g, int x, int y, const EgGlyph *gl,
                       const EgFont *f, uint16_t color)
{
    if (!gl || gl->w <= 0 || gl->h <= 0) return;
    int x0 = x + gl->xoff + g->off_x;
    int y0 = y + gl->top + g->off_y;
    for (int yy = 0; yy < gl->h; yy++) {
        int fy = y0 + yy;
        if (fy < g->clip_y0 || fy >= g->clip_y1) continue;
        uint16_t *row = g->fb + (size_t)fy * g->w;
        for (int xx = 0; xx < gl->w; xx++) {
            int fx = x0 + xx;
            if (fx < g->clip_x0 || fx >= g->clip_x1) continue;
            uint32_t idx = gl->offset + (uint32_t)(yy * gl->w + xx) / 2;
            if ((int)idx >= f->bitmap_size) continue;
            uint8_t b = f->bitmap[idx];
            int a = ((yy * gl->w + xx) & 1) ? (b >> 4) : (b & 15);
            if (!a) continue;
            row[fx] = eg_blend565(row[fx], color, a * 17);
        }
    }
}

void eg_gfx_draw_text(EgGfx *g, int x, int baseline, const char *text,
                      const EgStyle *style, uint16_t color)
{
    if (!text || !style) return;
    const EgFont *f = style_face(style);
    int pen = x;
    int start = x;
    int letter = style->letter_spacing;
    const char *p = text;
    while (*p) {
        uint32_t cp = 0;
        eg_utf8_decode(&p, &cp);
        if (cp == '\t') { pen += f->size * 2; continue; }
        const EgGlyph *gl = eg_font_glyph(f, cp);
        if (!gl) continue;
        draw_glyph(g, pen, baseline, gl, f, color);
        pen += gl->advance + letter;
    }
    if (style->underline) eg_gfx_hline(g, start, baseline + 2, pen - start, color);
    if (style->strike) eg_gfx_hline(g, start, baseline - style->font_size * 3 / 10, pen - start, color);
}

int eg_gfx_text_width(const char *text, const EgStyle *style)
{
    if (!text || !style) return 0;
    const EgFont *f = style_face(style);
    int w = 0;
    const char *p = text;
    while (*p) {
        uint32_t cp = 0;
        eg_utf8_decode(&p, &cp);
        if (cp == '\t') { w += f->size * 2; continue; }
        const EgGlyph *gl = eg_font_glyph(f, cp);
        if (gl) w += gl->advance + style->letter_spacing;
    }
    return w;
}

void eg_gfx_draw_cmd(EgGfx *g, const EgCmd *cmd, uint16_t default_ink)
{
    switch (cmd->type) {
        case EG_CMD_TEXT: {
            uint16_t color = cmd->style.color_set ? cmd->style.color : default_ink;
            if (g->dark_theme && cmd->style.color_set) {
                int r = (color >> 11) & 31, gg = (color >> 5) & 63, b = color & 31;
                int lum = r * 3 + gg * 4 + b * 3;
                if (lum < 300) color = eg_gfx_mix(color, default_ink, 225);
            }
            eg_gfx_draw_text(g, cmd->x, cmd->y, cmd->text, &cmd->style, color);
            break;
        }
        case EG_CMD_IMAGE:
            eg_gfx_image(g, cmd->image, cmd->x, cmd->y, cmd->w, cmd->h);
            break;
        case EG_CMD_RECT:
            eg_gfx_fill(g, cmd->x, cmd->y, cmd->w, cmd->h, cmd->color);
            break;
        case EG_CMD_LINE:
            eg_gfx_line(g, cmd->x, cmd->y, cmd->x + cmd->w, cmd->y + cmd->h,
                        cmd->color, cmd->style.border_t ? cmd->style.border_t : 1);
            break;
        default:
            break;
    }
}

void eg_gfx_draw_page(EgGfx *g, const EgChapter *ch, int page, uint16_t ink)
{
    if (!ch || page < 0 || page >= ch->page_count) return;
    const EgPage *p = &ch->pages[page];
    for (int i = 0; i < p->count; i++)
        eg_gfx_draw_cmd(g, &p->cmds[i], ink);
}

void eg_gfx_render_page(EgGfx *g, const EgChapter *ch, int page,
                        uint16_t paper, uint16_t ink)
{
    eg_gfx_clear(g, paper);
    eg_gfx_reset_clip(g);
    eg_gfx_set_offset(g, 0, 0);
    eg_gfx_draw_page(g, ch, page, ink);
}

void eg_gfx_draw_text_wrapped(EgGfx *g, int x, int y, int w,
                              const char *text, const EgStyle *style,
                              uint16_t color, int max_lines, int line_height)
{
    if (!text) return;
    int lines = 0;
    const char *p = text;
    while (*p && (max_lines <= 0 || lines < max_lines)) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *line_start = p;
        const char *last_space = NULL;
        const char *q = p;
        while (*q) {
            if (*q == ' ') last_space = q;
            const char *next = q;
            uint32_t cp;
            eg_utf8_decode(&next, &cp);
            int n = (int)(next - line_start);
            char tmp[256];
            if (n > 255) n = 255;
            memcpy(tmp, line_start, (size_t)n);
            tmp[n] = 0;
            if (eg_gfx_text_width(tmp, style) > w && q != line_start) {
                if (last_space) q = last_space;
                break;
            }
            q = next;
        }
        int n = (int)(q - line_start);
        char tmp[256];
        if (n > 255) n = 255;
        memcpy(tmp, line_start, (size_t)n);
        tmp[n] = 0;
        eg_gfx_draw_text(g, x, y, tmp, style, color);
        y += line_height;
        lines++;
        p = q;
        while (*p == ' ') p++;
    }
}

uint16_t eg_gfx_gray(int v)
{
    return eg_rgb565(v, v, v);
}

uint16_t eg_gfx_mix(uint16_t a, uint16_t b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = (ar * (255 - t) + br * t) / 255;
    int gg = (ag * (255 - t) + bg * t) / 255;
    int bl = (ab * (255 - t) + bb * t) / 255;
    return (uint16_t)((r << 11) | (gg << 5) | bl);
}
