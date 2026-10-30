#ifndef EG_GFX_H
#define EG_GFX_H

#include "util.h"
#include "css.h"
#include "image.h"
#include "layout.h"

typedef struct {
    uint16_t *fb;
    int w, h;
    int dark_theme;
    int off_x, off_y;
    int clip_x0, clip_y0, clip_x1, clip_y1;
} EgGfx;

void eg_gfx_init(EgGfx *g, uint16_t *fb, int w, int h);
void eg_gfx_set_clip(EgGfx *g, int x, int y, int w, int h);
void eg_gfx_reset_clip(EgGfx *g);
void eg_gfx_set_offset(EgGfx *g, int x, int y);
void eg_gfx_clear(EgGfx *g, uint16_t color);
void eg_gfx_fill(EgGfx *g, int x, int y, int w, int h, uint16_t color);
void eg_gfx_blend(EgGfx *g, int x, int y, int w, int h, uint16_t color, int alpha);
void eg_gfx_hline(EgGfx *g, int x, int y, int w, uint16_t color);
void eg_gfx_vline(EgGfx *g, int x, int y, int h, uint16_t color);
void eg_gfx_line(EgGfx *g, int x1, int y1, int x2, int y2, uint16_t color, int thickness);
void eg_gfx_rect(EgGfx *g, int x, int y, int w, int h, uint16_t color);
void eg_gfx_round_rect(EgGfx *g, int x, int y, int w, int h, int radius,
                       uint16_t fill, int has_fill, uint16_t border);
void eg_gfx_vgradient(EgGfx *g, int x, int y, int w, int h,
                      uint16_t top, uint16_t bottom);
void eg_gfx_image(EgGfx *g, const EgImage *img, int x, int y, int w, int h);
void eg_gfx_draw_text(EgGfx *g, int x, int baseline, const char *text,
                      const EgStyle *style, uint16_t color);
int eg_gfx_text_width(const char *text, const EgStyle *style);
void eg_gfx_draw_cmd(EgGfx *g, const EgCmd *cmd, uint16_t default_ink);
void eg_gfx_render_page(EgGfx *g, const EgChapter *ch, int page,
                        uint16_t paper, uint16_t ink);
void eg_gfx_draw_page(EgGfx *g, const EgChapter *ch, int page, uint16_t ink);
void eg_gfx_draw_text_wrapped(EgGfx *g, int x, int y, int w,
                              const char *text, const EgStyle *style,
                              uint16_t color, int max_lines, int line_height);
uint16_t eg_gfx_gray(int v);
uint16_t eg_gfx_mix(uint16_t a, uint16_t b, int t);

#endif
