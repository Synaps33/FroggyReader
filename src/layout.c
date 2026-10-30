#include "layout.h"
#include "text.h"
#include "fonts.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================== utilities */

typedef struct {
    char *text;
    EgStyle style;
} EgWordRun;

typedef struct {
    EgWordRun *runs;
    int run_count, run_cap;
    int width;
    int space_before;
    int forced_break;
} EgWord;

typedef struct {
    EgWord *words;
    int count, cap;
    EgStyle base;
    EgWord cur;
    int cur_active;
    int pending_space;
    int at_word_start;
    EgXmlNode *origin;
} InlineBuilder;

typedef struct {
    char *text;
    EgStyle style;
    int x;
    int w;
    int space_before;
} PLineRun;

typedef struct {
    PLineRun *runs;
    int run_count, run_cap;
    int width;
    int spaces;
    int height;
    int start_x;
    int avail;
} PLine;

typedef struct {
    PLine *lines;
    int count, cap;
} PLineList;

typedef struct {
    EgEpub *ep;
    EgChapter *ch;
    EgStylesheet css;
    EgLayoutSettings set;
    int page;
    int top, bottom;
    int x, width;
    int y;
    char chapter_dir[EG_MAX_PATH];
    int has_page_break;
} LayoutCtx;

static const EgFont *face_for(int size, int bold, int italic)
{
    return &eg_fonts[eg_font_index(size, bold ? (italic ? EG_STYLE_BOLD_ITALIC : EG_STYLE_BOLD)
                                             : (italic ? EG_STYLE_ITALIC : EG_STYLE_REGULAR))];
}

static int inline_style_equal(const EgStyle *a, const EgStyle *b)
{
    return a->font_size == b->font_size && a->bold == b->bold && a->italic == b->italic &&
           a->color_set == b->color_set && (!a->color_set || a->color == b->color) &&
           a->underline == b->underline && a->strike == b->strike &&
           a->vertical_align == b->vertical_align &&
           a->letter_spacing == b->letter_spacing &&
           a->text_transform == b->text_transform &&
           a->white_space == b->white_space;
}

/* ================================================================ commands */

static EgPage *cur_page(LayoutCtx *ctx)
{
    return &ctx->ch->pages[ctx->page];
}

static void page_reserve(EgPage *p, int extra)
{
    if (p->count + extra <= p->cap) return;
    int cap = p->cap ? p->cap : 64;
    while (cap < p->count + extra) cap += cap / 2;
    EgCmd *n = eg_realloc(p->cmds, sizeof(EgCmd) * (size_t)cap);
    if (!n) return;
    p->cmds = n;
    p->cap = cap;
}

static void page_insert_cmd(EgPage *p, int at, EgCmd cmd)
{
    if (at < 0 || at > p->count) at = p->count;
    page_reserve(p, 1);
    if (p->count > at)
        memmove(&p->cmds[at + 1], &p->cmds[at], sizeof(EgCmd) * (size_t)(p->count - at));
    p->cmds[at] = cmd;
    p->count++;
}

static void page_add_cmd(EgPage *p, EgCmd cmd)
{
    page_reserve(p, 1);
    p->cmds[p->count++] = cmd;
}

static EgCmd cmd_base(int type)
{
    EgCmd c;
    memset(&c, 0, sizeof(c));
    c.type = type;
    return c;
}

static void add_text_cmd(LayoutCtx *ctx, int x, int baseline, const char *text,
                         const EgStyle *style, int page_index, int insert_at)
{
    if (!text || !*text) return;
    EgCmd c = cmd_base(EG_CMD_TEXT);
    c.x = x;
    c.y = baseline;
    c.style = *style;
    c.text = eg_strdup(text);
    EgPage *p = &ctx->ch->pages[page_index];
    if (insert_at >= 0) page_insert_cmd(p, insert_at, c);
    else page_add_cmd(p, c);
}

static void add_rect_cmd(LayoutCtx *ctx, int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    EgCmd c = cmd_base(EG_CMD_RECT);
    c.x = x; c.y = y; c.w = w; c.h = h; c.color = color;
    page_add_cmd(cur_page(ctx), c);
}

static void add_line_cmd(LayoutCtx *ctx, int x1, int y1, int x2, int y2, uint16_t color, int width)
{
    EgCmd c = cmd_base(EG_CMD_LINE);
    c.x = x1; c.y = y1; c.w = x2 - x1; c.h = y2 - y1;
    c.color = color;
    if (width <= 0) width = 1;
    c.style.border_t = width;
    page_add_cmd(cur_page(ctx), c);
}

static void new_page(LayoutCtx *ctx)
{
    if (cur_page(ctx)->count == 0) {
        ctx->y = ctx->top;
        return;
    }
    if (ctx->ch->page_count == ctx->ch->cap) {
        int cap = ctx->ch->cap ? ctx->ch->cap * 2 : 32;
        EgPage *np = eg_realloc(ctx->ch->pages, sizeof(EgPage) * (size_t)cap);
        if (!np) return;
        memset(np + ctx->ch->cap, 0, sizeof(EgPage) * (size_t)(cap - ctx->ch->cap));
        ctx->ch->pages = np;
        ctx->ch->cap = cap;
    }
    memset(&ctx->ch->pages[ctx->ch->page_count], 0, sizeof(EgPage));
    ctx->page = ctx->ch->page_count++;
    ctx->y = ctx->top;
}

static void ensure_page_for(LayoutCtx *ctx, int height)
{
    if (ctx->y + height > ctx->bottom && cur_page(ctx)->count > 0) new_page(ctx);
}

static int page_height(const LayoutCtx *ctx)
{
    return ctx->bottom - ctx->top;
}

static int page_has_content(LayoutCtx *ctx)
{
    return cur_page(ctx)->count > 0;
}

/* ================================================================ anchors */

static void add_anchor(LayoutCtx *ctx, const EgXmlNode *node)
{
    const char *id = eg_xml_attr(node, "id");
    if (!id || !*id) return;
    EgChapter *ch = ctx->ch;
    if (ch->anchor_count == ch->anchor_cap) {
        int cap = ch->anchor_cap ? ch->anchor_cap * 2 : 32;
        EgAnchor *na = eg_realloc(ch->anchors, sizeof(EgAnchor) * (size_t)cap);
        if (!na) return;
        ch->anchors = na;
        ch->anchor_cap = cap;
    }
    EgAnchor *a = &ch->anchors[ch->anchor_count++];
    a->name = eg_strdup(id);
    a->page = ctx->page;
    a->y = ctx->y;
}

int eg_chapter_find_anchor(const EgChapter *ch, const char *fragment)
{
    if (!ch || !fragment) return -1;
    for (int i = 0; i < ch->anchor_count; i++)
        if (strcmp(ch->anchors[i].name, fragment) == 0) return ch->anchors[i].page;
    return -1;
}

/* ============================================================ inline build */

static void word_init(EgWord *w)
{
    memset(w, 0, sizeof(*w));
}

static void word_free(EgWord *w)
{
    for (int i = 0; i < w->run_count; i++) free(w->runs[i].text);
    free(w->runs);
    memset(w, 0, sizeof(*w));
}

static void word_add_run(EgWord *w, const EgStyle *style)
{
    if (w->run_count > 0 && inline_style_equal(&w->runs[w->run_count - 1].style, style)) return;
    if (w->run_count == w->run_cap) {
        int cap = w->run_cap ? w->run_cap * 2 : 4;
        EgWordRun *nr = eg_realloc(w->runs, sizeof(EgWordRun) * (size_t)cap);
        if (!nr) return;
        w->runs = nr;
        w->run_cap = cap;
    }
    EgWordRun *r = &w->runs[w->run_count++];
    memset(r, 0, sizeof(*r));
    r->style = *style;
}

static void word_append_cp(EgWord *w, uint32_t cp, const EgStyle *style)
{
    if (!w->run_count || !inline_style_equal(&w->runs[w->run_count - 1].style, style))
        word_add_run(w, style);
    if (!w->run_count) return;
    EgWordRun *r = &w->runs[w->run_count - 1];
    char buf[4];
    int n = eg_utf8_encode(cp, buf);
    if (!r->text) {
        r->text = eg_malloc((size_t)n + 1);
        memcpy(r->text, buf, (size_t)n);
        r->text[n] = 0;
    } else {
        int old = (int)strlen(r->text);
        char *nt = eg_realloc(r->text, (size_t)old + (size_t)n + 1);
        if (!nt) return;
        r->text = nt;
        memcpy(r->text + old, buf, (size_t)n);
        r->text[old + n] = 0;
    }
}

static void builder_init(InlineBuilder *ib, const EgStyle *base)
{
    memset(ib, 0, sizeof(*ib));
    ib->base = *base;
    word_init(&ib->cur);
    ib->at_word_start = 1;
}

static void builder_push_word(InlineBuilder *ib)
{
    if (!ib->cur_active) return;
    EgWord *w = &ib->cur;
    w->width = 0;
    for (int i = 0; i < w->run_count; i++)
        w->width += eg_text_width(w->runs[i].text, face_for(w->runs[i].style.font_size,
                                          w->runs[i].style.bold, w->runs[i].style.italic),
                                  w->runs[i].style.letter_spacing);
    w->space_before = ib->cur.space_before;
    if (ib->count == ib->cap) {
        int cap = ib->cap ? ib->cap * 2 : 64;
        EgWord *nw = eg_realloc(ib->words, sizeof(EgWord) * (size_t)cap);
        if (!nw) { word_free(w); return; }
        ib->words = nw;
        ib->cap = cap;
    }
    ib->words[ib->count++] = ib->cur;
    word_init(&ib->cur);
    ib->cur_active = 0;
}

static void builder_forced_break(InlineBuilder *ib)
{
    builder_push_word(ib);
    if (ib->count == ib->cap) {
        int cap = ib->cap ? ib->cap * 2 : 64;
        EgWord *nw = eg_realloc(ib->words, sizeof(EgWord) * (size_t)cap);
        if (!nw) return;
        ib->words = nw;
        ib->cap = cap;
    }
    EgWord w;
    word_init(&w);
    w.forced_break = 1;
    ib->words[ib->count++] = w;
    ib->pending_space = 0;
}

static void builder_append_text(InlineBuilder *ib, const char *text, const EgStyle *style)
{
    if (!text) return;
    const char *p = text;
    while (*p) {
        const char *before = p;
        uint32_t cp = 0;
        eg_utf8_decode(&p, &cp);
        (void)before;
        if (cp == '\r') continue;
        if (cp == '\n' || cp == '\t') cp = ' ';
        if (cp == 0x00AD) continue;
        int space = eg_utf8_is_space(cp);
        if (space) {
            if (cp == 0x00A0) {
                /* non-breaking space stays inside the word */
                if (!ib->cur_active) {
                    ib->cur_active = 1;
                    ib->cur.space_before = ib->pending_space;
                    ib->pending_space = 0;
                }
                word_append_cp(&ib->cur, ' ', style);
                continue;
            }
            builder_push_word(ib);
            ib->pending_space = 1;
            ib->at_word_start = 1;
            continue;
        }
        uint32_t out = cp;
        if (style->text_transform == 1) out = eg_utf8_toupper(cp);
        else if (style->text_transform == 2) out = eg_utf8_tolower(cp);
        else if (style->text_transform == 3 && ib->at_word_start) out = eg_utf8_toupper(cp);
        if (!ib->cur_active) {
            ib->cur_active = 1;
            ib->cur.space_before = ib->pending_space;
            ib->pending_space = 0;
        }
        word_append_cp(&ib->cur, out, style);
        ib->at_word_start = 0;
        if (eg_utf8_is_break_after(cp)) {
            builder_push_word(ib);
            ib->pending_space = 0;
            ib->at_word_start = 0;
        }
    }
}

static void builder_free(InlineBuilder *ib)
{
    for (int i = 0; i < ib->count; i++) word_free(&ib->words[i]);
    free(ib->words);
    word_free(&ib->cur);
    memset(ib, 0, sizeof(*ib));
}

static int is_inline_container(const EgXmlNode *node, const EgStyle *style)
{
    if (!node || !node->tag) return 0;
    if (style->display != EG_DISPLAY_INLINE) return 0;
    const char *tag = node->tag;
    if (strcmp(tag, "img") == 0 || strcmp(tag, "br") == 0) return 1;
    if (strcmp(tag, "table") == 0 || strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0 ||
        strcmp(tag, "pre") == 0 || strcmp(tag, "hr") == 0 || strcmp(tag, "figure") == 0)
        return 0;
    return 1;
}

static void builder_add_inline(LayoutCtx *ctx, InlineBuilder *ib,
                               const EgXmlNode *node, const EgStyle *style,
                               int skip_lists)
{
    if (!node || style->display == EG_DISPLAY_NONE || !style->visible) return;
    if (!node->tag) {
        if (node->text) builder_append_text(ib, node->text, style);
        return;
    }
    const char *tag = node->tag;
    if (strcmp(tag, "br") == 0) { builder_forced_break(ib); return; }
    if (strcmp(tag, "img") == 0) return;
    if (strcmp(tag, "script") == 0 || strcmp(tag, "style") == 0 ||
        strcmp(tag, "head") == 0 || strcmp(tag, "title") == 0 ||
        strcmp(tag, "meta") == 0 || strcmp(tag, "link") == 0 ||
        strcmp(tag, "nav") == 0)
        return;
    if (skip_lists && (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0)) return;
    for (const EgXmlNode *c = node->children; c; c = c->next) {
        if (!c->tag) {
            builder_append_text(ib, c->text, style);
        } else {
            if (skip_lists && (strcmp(c->tag, "ul") == 0 || strcmp(c->tag, "ol") == 0)) continue;
            EgStyle cs;
            eg_css_compute(&ctx->css, c, style, &cs, ctx->set.font_size);
            if (cs.display == EG_DISPLAY_NONE || !cs.visible) continue;
            if (strcmp(c->tag, "br") == 0) { builder_forced_break(ib); continue; }
            if (strcmp(c->tag, "img") == 0) continue;
            builder_add_inline(ctx, ib, c, &cs, skip_lists);
        }
    }
}

/* ================================================================= lines */

static void line_init(PLine *line)
{
    memset(line, 0, sizeof(*line));
}

static void line_free(PLine *line)
{
    for (int i = 0; i < line->run_count; i++) free(line->runs[i].text);
    free(line->runs);
    memset(line, 0, sizeof(*line));
}

static void lines_free(PLineList *ll)
{
    for (int i = 0; i < ll->count; i++) line_free(&ll->lines[i]);
    free(ll->lines);
    memset(ll, 0, sizeof(*ll));
}

static PLine *lines_push(PLineList *ll)
{
    if (ll->count == ll->cap) {
        int cap = ll->cap ? ll->cap * 2 : 16;
        PLine *nl = eg_realloc(ll->lines, sizeof(PLine) * (size_t)cap);
        if (!nl) return NULL;
        ll->lines = nl;
        ll->cap = cap;
    }
    PLine *l = &ll->lines[ll->count++];
    line_init(l);
    return l;
}

static void line_add_run(PLine *line, const char *text, const EgStyle *style, int x, int w, int space_before)
{
    if (line->run_count == line->run_cap) {
        int cap = line->run_cap ? line->run_cap * 2 : 8;
        PLineRun *nr = eg_realloc(line->runs, sizeof(PLineRun) * (size_t)cap);
        if (!nr) return;
        line->runs = nr;
        line->run_cap = cap;
    }
    PLineRun *r = &line->runs[line->run_count++];
    r->text = eg_strdup(text);
    r->style = *style;
    r->x = x;
    r->w = w;
    r->space_before = space_before;
}

static void builder_to_lines(LayoutCtx *ctx, InlineBuilder *ib, const EgStyle *style,
                             int width, int first_indent, PLineList *out)
{
    (void)ctx;
    builder_push_word(ib);
    const EgFont *base = face_for(style->font_size, style->bold, style->italic);
    int space_w = 0;
    const EgGlyph *sg = eg_font_glyph(base, ' ');
    if (sg) space_w = sg->advance;
    space_w += style->word_spacing;
    if (space_w < 2) space_w = 2;
    int base_h = style->line_height;
    if (base_h < style->font_size + 2) base_h = style->font_size + 2;

    PLine *line = NULL;
    int cursor = 0;
    int first = 1;
    int indent = first_indent;
    int avail = width - indent;
    if (avail < 20) { indent = 0; avail = width; }
    line = lines_push(out);
    if (!line) return;
    line->start_x = first ? indent : 0;
    line->avail = avail;
    line->height = base_h;

    for (int wi = 0; wi < ib->count; wi++) {
        EgWord *w = &ib->words[wi];
        if (w->forced_break) {
            line = lines_push(out);
            if (line) {
                line->start_x = 0;
                line->avail = width;
                line->height = base_h;
                first = 0;
            }
            cursor = 0;
            continue;
        }
        int gap = (line->run_count > 0 && w->space_before) ? space_w : 0;
        if (line->run_count > 0 && cursor + gap + w->width > line->avail) {
            line = lines_push(out);
            if (!line) return;
            line->start_x = 0;
            line->avail = width;
            line->height = base_h;
            first = 0;
            cursor = 0;
            gap = 0;
        }
        int run_x = cursor + gap;
        int has_gap = gap > 0;
        int line_lh = base_h;
        for (int ri = 0; ri < w->run_count; ri++) {
            EgWordRun *wr = &w->runs[ri];
            const EgFont *rf = face_for(wr->style.font_size, wr->style.bold, wr->style.italic);
            int rw = eg_text_width(wr->text, rf, wr->style.letter_spacing);
            int before = (ri == 0) ? has_gap : 0;
            line_add_run(line, wr->text, &wr->style, run_x, rw, before);
            run_x += rw;
            if (wr->style.font_size + 4 > line_lh) line_lh = wr->style.font_size + 4;
        }
        cursor += gap + w->width;
        line->width = cursor;
        if (has_gap) line->spaces++;
        if (line_lh > line->height) line->height = line_lh;
        (void)first;
    }
    if (line->run_count == 0 && out->count > 1) {
        line_free(line);
        out->count--;
    } else if (line->run_count == 0) {
        line->height = base_h;
    }
}

/* ============================================================== paragraph */

static void emit_line(LayoutCtx *ctx, PLine *line, const EgStyle *paragraph,
                      int is_first, int is_last, int marker_x, const char *marker,
                      const EgStyle *marker_style, int page_index, int insert_bg)
{
    (void)insert_bg;
    int align = paragraph->align;
    if (align == EG_ALIGN_JUSTIFY && (!ctx->set.justify || is_last)) align = EG_ALIGN_LEFT;
    int avail = line->avail;
    int offset = 0;
    int extra = 0;
    if (align == EG_ALIGN_CENTER) offset = (avail - line->width) / 2;
    else if (align == EG_ALIGN_RIGHT) offset = avail - line->width;
    else if (align == EG_ALIGN_JUSTIFY && line->spaces > 0) {
        extra = (avail - line->width) / line->spaces;
        if (extra < 0) extra = 0;
        if (extra > 4) extra = 4;
    }
    if (offset < 0) offset = 0;
    int base_x = ctx->x + line->start_x + offset;
    int shift = 0;
    const EgFont *base_face = face_for(paragraph->font_size, paragraph->bold, paragraph->italic);
    int asc = base_face->ascent, desc = base_face->descent;
    int baseline = ctx->y + (line->height - (asc + desc)) / 2 + asc;
    for (int i = 0; i < line->run_count; i++) {
        PLineRun *r = &line->runs[i];
        if (r->space_before) shift += extra;
        int x = base_x + r->x + shift;
        int y = baseline;
        if (r->style.vertical_align > 0) y -= 4;
        else if (r->style.vertical_align < 0) y += 4;
        add_text_cmd(ctx, x, y, r->text, &r->style, page_index, -1);
    }
    if (marker && is_first) {
        const EgFont *mf = face_for(marker_style->font_size, marker_style->bold, marker_style->italic);
        int mw = eg_text_width(marker, mf, marker_style->letter_spacing);
        EgStyle ms = *marker_style;
        int mx = marker_x - mw - 4;
        if (mx < 0) mx = 0;
        add_text_cmd(ctx, mx, baseline, marker, &ms, page_index, -1);
    }
}

static void place_line_block(LayoutCtx *ctx, PLineList *ll, const EgStyle *style,
                             const char *marker)
{
    if (ll->count == 0) return;
    int total = ll->count;
    int base_line = style->line_height;
    if (style->keep_with_next && page_has_content(ctx)) {
        int total_h = 0;
        for (int i = 0; i < total; i++) total_h += ll->lines[i].height;
        if (ctx->y + total_h + base_line * 2 > ctx->bottom) new_page(ctx);
    }
    int i = 0;
    int first_line = 1;
    while (i < total) {
        int avail_h = ctx->bottom - ctx->y;
        int fit = 0;
        int hsum = 0;
        while (i + fit < total) {
            int lh = ll->lines[i + fit].height;
            if (hsum + lh > avail_h) break;
            hsum += lh;
            fit++;
        }
        int remaining = total - i;
        if (fit == 0) {
            if (page_has_content(ctx)) { new_page(ctx); continue; }
            fit = 1; /* oversized line: clip rather than loop forever */
        }
        if (remaining > fit) {
            if (fit == 1) {
                if (page_has_content(ctx)) { new_page(ctx); continue; }
            } else if (remaining - fit == 1 && fit > 2) {
                fit--;
            }
        }
        if (fit < 1) fit = 1;
        int page_index = ctx->page;
        EgPage *seg_page = &ctx->ch->pages[page_index];
        int insert_index = seg_page->count;
        int seg_y = ctx->y;
        int seg_h = 0;
        for (int k = 0; k < fit; k++) {
            PLine *line = &ll->lines[i + k];
            int is_last = (i + k == total - 1);
            int seg_first = (k == 0);
            emit_line(ctx, line, style, seg_first && first_line, is_last,
                      ctx->x, (seg_first && first_line) ? marker : NULL, style,
                      ctx->page, 0);
            ctx->y += line->height;
            seg_h += line->height;
        }
        int seg_is_first = (i == 0);
        int seg_is_last = (i + fit == total);
        int box_top = seg_y - (seg_is_first ? style->padding_t : 0);
        int box_bottom = seg_y + seg_h + (seg_is_last ? style->padding_b : 0);
        if (style->has_bg) {
            EgCmd c = cmd_base(EG_CMD_RECT);
            c.x = ctx->x - style->padding_l;
            c.y = box_top;
            c.w = ctx->width + style->padding_l + style->padding_r;
            c.h = box_bottom - box_top;
            c.color = style->bg;
            page_insert_cmd(seg_page, insert_index, c);
            insert_index++;
        }
        uint16_t bc = style->border_color ? style->border_color : eg_rgb565(150, 130, 100);
        if (style->border_l > 0) {
            EgCmd c = cmd_base(EG_CMD_RECT);
            c.x = ctx->x - style->padding_l - style->border_l;
            if (c.x < 0) c.x = 0;
            c.y = box_top;
            c.w = style->border_l;
            c.h = box_bottom - box_top;
            c.color = bc;
            page_insert_cmd(seg_page, insert_index, c);
            insert_index++;
        }
        if (style->border_r > 0) {
            EgCmd c = cmd_base(EG_CMD_RECT);
            c.x = ctx->x + ctx->width + style->padding_r;
            c.y = box_top;
            c.w = style->border_r;
            c.h = box_bottom - box_top;
            c.color = bc;
            page_insert_cmd(seg_page, insert_index, c);
            insert_index++;
        }
        if (style->border_t > 0 && seg_is_first) {
            EgCmd c = cmd_base(EG_CMD_RECT);
            c.x = ctx->x - style->padding_l - style->border_l;
            c.y = box_top - style->border_t;
            c.w = ctx->width + style->padding_l + style->padding_r + style->border_l + style->border_r;
            c.h = style->border_t;
            c.color = bc;
            page_insert_cmd(seg_page, insert_index, c);
            insert_index++;
        }
        if (style->border_b > 0 && seg_is_last) {
            EgCmd c = cmd_base(EG_CMD_RECT);
            c.x = ctx->x - style->padding_l - style->border_l;
            c.y = box_bottom;
            c.w = ctx->width + style->padding_l + style->padding_r + style->border_l + style->border_r;
            c.h = style->border_b;
            c.color = bc;
            page_insert_cmd(seg_page, insert_index, c);
        }
        i += fit;
        first_line = 0;
        if (i < total) new_page(ctx);
    }
}

/* =============================================================== block box */

static void layout_block(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *parent);
static void layout_contents(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style);
static void layout_image(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style);
static void layout_table(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style);
static void layout_list(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style);
static void layout_pre(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style);

static int is_skipped_tag(const char *tag)
{
    return !tag || strcmp(tag, "head") == 0 || strcmp(tag, "script") == 0 ||
           strcmp(tag, "style") == 0 || strcmp(tag, "title") == 0 ||
           strcmp(tag, "meta") == 0 || strcmp(tag, "link") == 0 ||
           strcmp(tag, "nav") == 0 || strcmp(tag, "svg") == 0;
}

static int is_block_boundary(const EgXmlNode *node, const EgStyle *style)
{
    if (!node || !node->tag) return 0;
    if (style->display == EG_DISPLAY_NONE) return 1;
    if (style->display != EG_DISPLAY_INLINE) return 1;
    const char *t = node->tag;
    return strcmp(t, "p") == 0 || strcmp(t, "h1") == 0 || strcmp(t, "h2") == 0 ||
           strcmp(t, "h3") == 0 || strcmp(t, "h4") == 0 || strcmp(t, "h5") == 0 ||
           strcmp(t, "h6") == 0 || strcmp(t, "div") == 0 || strcmp(t, "section") == 0 ||
           strcmp(t, "article") == 0 || strcmp(t, "header") == 0 || strcmp(t, "footer") == 0 ||
           strcmp(t, "aside") == 0 || strcmp(t, "main") == 0 || strcmp(t, "center") == 0 ||
           strcmp(t, "blockquote") == 0 || strcmp(t, "figure") == 0 ||
           strcmp(t, "figcaption") == 0 || strcmp(t, "table") == 0 ||
           strcmp(t, "ul") == 0 || strcmp(t, "ol") == 0 || strcmp(t, "li") == 0 ||
           strcmp(t, "pre") == 0 || strcmp(t, "hr") == 0 || strcmp(t, "img") == 0 ||
           strcmp(t, "tr") == 0 || strcmp(t, "td") == 0 || strcmp(t, "th") == 0;
}

/* The builder must be converted exactly once; helper used by layout_contents. */
static void flush_builder(LayoutCtx *ctx, InlineBuilder *ib, const EgStyle *style)
{
    if (ib->count == 0 && !ib->cur_active) return;
    PLineList ll;
    memset(&ll, 0, sizeof(ll));
    builder_to_lines(ctx, ib, style, ctx->width, style->text_indent, &ll);
    place_line_block(ctx, &ll, style, NULL);
    lines_free(&ll);
    builder_free(ib);
    builder_init(ib, style);
}

static void layout_contents(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style)
{
    InlineBuilder ib;
    builder_init(&ib, style);
    for (const EgXmlNode *c = node->children; c; c = c->next) {
        if (!c->tag) {
            builder_append_text(&ib, c->text, style);
            continue;
        }
        if (is_skipped_tag(c->tag)) continue;
        EgStyle cs;
        eg_css_compute(&ctx->css, c, style, &cs, ctx->set.font_size);
        if (cs.display == EG_DISPLAY_NONE || !cs.visible) continue;
        if (is_block_boundary(c, &cs)) {
            flush_builder(ctx, &ib, style);
            layout_block(ctx, c, style);
        } else {
            builder_add_inline(ctx, &ib, c, &cs, 0);
        }
    }
    flush_builder(ctx, &ib, style);
}

static EgImage *chapter_image_get(LayoutCtx *ctx, const char *href,
                                  int target_w, int target_h,
                                  const uint8_t *data, size_t size)
{
    EgChapter *ch = ctx->ch;
    for (int i = 0; i < ch->image_count; i++)
        if (strcmp(ch->images[i].href, href) == 0) return &ch->images[i].image;
    if (ch->image_count == ch->image_cap) {
        int cap = ch->image_cap ? ch->image_cap * 2 : 8;
        EgChapterImage *ni = eg_realloc(ch->images, sizeof(EgChapterImage) * (size_t)cap);
        if (!ni) return NULL;
        ch->images = ni;
        ch->image_cap = cap;
    }
    EgChapterImage *e = &ch->images[ch->image_count];
    memset(e, 0, sizeof(*e));
    if (!eg_image_decode_to(data, size, target_w, target_h, eg_rgb565(247, 237, 216), &e->image)) {
        return NULL;
    }
    e->href = eg_strdup(href);
    ch->image_count++;
    return &e->image;
}

static void layout_image(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style)
{
    const char *src = eg_xml_attr(node, "src");
    if (!src) return;
    char path[EG_MAX_PATH];
    eg_path_normalize(path, sizeof(path), ctx->chapter_dir, src);
    uint8_t *data = NULL;
    size_t size = 0;
    if (!eg_epub_read(ctx->ep, path, &data, &size)) {
        eg_log("layout: missing image %s\n", path);
        return;
    }
    int nw = 0, nh = 0;
    if (!eg_image_info(data, size, &nw, &nh)) {
        free(data);
        return;
    }
    int avail_w = ctx->width;
    int avail_h = page_height(ctx);
    int target_w = 0, target_h = 0;
    int css_w = style->width_pct > 0 ? avail_w * style->width_pct / 100 : style->width_px;
    int css_h = style->height_pct > 0 ? avail_h * style->height_pct / 100 : style->height_px;
    const char *wa = eg_xml_attr(node, "width");
    const char *ha = eg_xml_attr(node, "height");
    int attr_w = wa ? atoi(wa) : 0;
    int attr_h = ha ? atoi(ha) : 0;
    int have_css_w = css_w > 0;
    int have_css_h = css_h > 0;
    if (have_css_w && !have_css_h) {
        target_w = css_w;
        target_h = target_w * nh / nw;
    } else if (have_css_h && !have_css_w) {
        target_h = css_h;
        target_w = target_h * nw / nh;
    } else if (have_css_w && have_css_h) {
        target_w = css_w;
        target_h = css_h;
    } else if (attr_w > 0 && attr_h > 0) {
        target_w = attr_w;
        target_h = attr_h;
    } else if (attr_w > 0) {
        target_w = attr_w;
        target_h = target_w * nh / nw;
    } else if (attr_h > 0) {
        target_h = attr_h;
        target_w = target_h * nw / nh;
    } else {
        target_w = nw;
        target_h = nh;
    }
    if (style->max_width_pct > 0) {
        int mw = avail_w * style->max_width_pct / 100;
        if (target_w > mw) { target_h = target_h * mw / target_w; target_w = mw; }
    } else if (style->max_width_px > 0 && target_w > style->max_width_px) {
        target_h = target_h * style->max_width_px / target_w;
        target_w = style->max_width_px;
    }
    if (target_w > avail_w) {
        target_h = target_h * avail_w / target_w;
        target_w = avail_w;
    }
    if (target_h > avail_h) {
        target_w = target_w * avail_h / target_h;
        target_h = avail_h;
    }
    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;
    if (target_h < page_height(ctx) && ctx->y + target_h > ctx->bottom && page_has_content(ctx))
        new_page(ctx);
    int x = ctx->x;
    if (style->align == EG_ALIGN_CENTER || style->align == EG_ALIGN_JUSTIFY)
        x = ctx->x + (ctx->width - target_w) / 2;
    else if (style->align == EG_ALIGN_RIGHT)
        x = ctx->x + ctx->width - target_w;
    EgImage *img = chapter_image_get(ctx, path, target_w, target_h, data, size);
    free(data);
    if (img) {
        EgCmd c = cmd_base(EG_CMD_IMAGE);
        c.x = x; c.y = ctx->y; c.w = target_w; c.h = target_h; c.image = img;
        page_add_cmd(cur_page(ctx), c);
    }
    ctx->y += target_h;
}

/* =============================================================== pre block */

static void layout_pre(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style)
{
    char *text = eg_xml_text(node);
    if (!text) return;
    const EgFont *face = face_for(style->font_size, style->bold, style->italic);
    int lh = style->line_height;
    if (lh < style->font_size + 3) lh = style->font_size + 3;
    int asc = face->ascent, desc = face->descent;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0 && p[n - 1] == '\r') n--;
        char *line = eg_strndup(p, n);
        if (ctx->y + lh > ctx->bottom && page_has_content(ctx)) new_page(ctx);
        if (line[0]) {
            EgStyle s = *style;
            int y = ctx->y + (lh - (asc + desc)) / 2 + asc;
            add_text_cmd(ctx, ctx->x, y, line, &s, ctx->page, -1);
        }
        ctx->y += lh;
        free(line);
        if (!e) break;
        p = e + 1;
    }
    free(text);
}

/* ============================================================== hr block */

static void layout_hr(LayoutCtx *ctx)
{
    ensure_page_for(ctx, 2);
    uint16_t col = eg_rgb565(170, 148, 112);
    add_line_cmd(ctx, ctx->x, ctx->y, ctx->x + ctx->width, ctx->y, col, 1);
    ctx->y += 1;
}

/* ================================================================ tables */

typedef struct {
    const EgXmlNode *node;
    int colspan;
    EgStyle style;
} TableCell;

typedef struct {
    TableCell *cells;
    int count, cap;
} TableRow;

typedef struct {
    TableRow *rows;
    int count, cap;
    int cols;
} TableData;

static void table_free(TableData *t)
{
    for (int i = 0; i < t->count; i++) free(t->rows[i].cells);
    free(t->rows);
    memset(t, 0, sizeof(*t));
}

static void table_collect_rows(LayoutCtx *ctx, const EgXmlNode *node,
                               const EgStyle *parent_style, TableData *t)
{
    for (const EgXmlNode *c = node->children; c; c = c->next) {
        if (!c->tag) continue;
        if (strcmp(c->tag, "tr") == 0) {
            if (t->count == t->cap) {
                int cap = t->cap ? t->cap * 2 : 8;
                TableRow *nr = eg_realloc(t->rows, sizeof(TableRow) * (size_t)cap);
                if (!nr) return;
                t->rows = nr;
                t->cap = cap;
            }
            TableRow *row = &t->rows[t->count++];
            memset(row, 0, sizeof(*row));
            int cols = 0;
            for (const EgXmlNode *cell = c->children; cell; cell = cell->next) {
                if (!cell->tag || (strcmp(cell->tag, "td") != 0 && strcmp(cell->tag, "th") != 0))
                    continue;
                if (row->count == row->cap) {
                    int cap = row->cap ? row->cap * 2 : 4;
                    TableCell *nc = eg_realloc(row->cells, sizeof(TableCell) * (size_t)cap);
                    if (!nc) return;
                    row->cells = nc;
                    row->cap = cap;
                }
                TableCell *tc = &row->cells[row->count++];
                memset(tc, 0, sizeof(*tc));
                tc->node = cell;
                const char *cs = eg_xml_attr(cell, "colspan");
                tc->colspan = cs ? atoi(cs) : 1;
                if (tc->colspan < 1) tc->colspan = 1;
                eg_css_compute(&ctx->css, cell, parent_style, &tc->style, ctx->set.font_size);
                cols += tc->colspan;
            }
            if (cols > t->cols) t->cols = cols;
        } else if (strcmp(c->tag, "thead") == 0 || strcmp(c->tag, "tbody") == 0 ||
                   strcmp(c->tag, "tfoot") == 0) {
            table_collect_rows(ctx, c, parent_style, t);
        }
    }
}

static void cell_words(LayoutCtx *ctx, InlineBuilder *ib, const EgXmlNode *node,
                       const EgStyle *style)
{
    if (!node) return;
    if (!node->tag) {
        builder_append_text(ib, node->text, style);
        return;
    }
    if (strcmp(node->tag, "br") == 0) { builder_forced_break(ib); return; }
    if (strcmp(node->tag, "img") == 0) return;
    for (const EgXmlNode *c = node->children; c; c = c->next) {
        if (!c->tag) builder_append_text(ib, c->text, style);
        else {
            EgStyle cs;
            eg_css_compute(&ctx->css, c, style, &cs, ctx->set.font_size);
            if (cs.display == EG_DISPLAY_NONE || !cs.visible) continue;
            cell_words(ctx, ib, c, &cs);
        }
    }
}

static void emit_cell_text(LayoutCtx *ctx, PLineList *ll, const EgStyle *style,
                           int x0, int y0, int width)
{
    int y = y0 + style->padding_t;
    for (int i = 0; i < ll->count; i++) {
        PLine *line = &ll->lines[i];
        int align = style->align;
        int avail = width;
        int offset = 0;
        int extra = 0;
        if (align == EG_ALIGN_CENTER) offset = (avail - line->width) / 2;
        else if (align == EG_ALIGN_RIGHT) offset = avail - line->width;
        else if (align == EG_ALIGN_JUSTIFY && i != ll->count - 1 && line->spaces > 0) {
            extra = (avail - line->width) / line->spaces;
            if (extra < 0) extra = 0;
            if (extra > 4) extra = 4;
        }
        if (offset < 0) offset = 0;
        const EgFont *bf = face_for(style->font_size, style->bold, style->italic);
        int baseline = y + (line->height - (bf->ascent + bf->descent)) / 2 + bf->ascent;
        int shift = 0;
        for (int r = 0; r < line->run_count; r++) {
            PLineRun *run = &line->runs[r];
            if (run->space_before) shift += extra;
            add_text_cmd(ctx, x0 + offset + line->start_x + run->x + shift, baseline,
                         run->text, &run->style, ctx->page, -1);
        }
        y += line->height;
    }
}

static void layout_table(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style)
{
    TableData t;
    memset(&t, 0, sizeof(t));
    table_collect_rows(ctx, node, style, &t);
    if (t.count == 0 || t.cols == 0) { table_free(&t); return; }
    int table_w = ctx->width;
    int base_w = table_w / t.cols;
    int rem = table_w - base_w * t.cols;
    int col_w[32];
    int ci = 0;
    for (int i = 0; i < t.cols && i < 32; i++) col_w[i] = base_w + (i < rem ? 1 : 0);
    (void)ci;
    for (int ri = 0; ri < t.count; ri++) {
        TableRow *row = &t.rows[ri];
        int max_h = 0;
        PLineList *cell_lines = eg_calloc((size_t)row->count, sizeof(PLineList));
        int *cell_x = eg_calloc((size_t)row->count, sizeof(int));
        int *cell_w = eg_calloc((size_t)row->count, sizeof(int));
        if (!cell_lines || !cell_x || !cell_w) {
            free(cell_lines); free(cell_x); free(cell_w);
            break;
        }
        int col = 0;
        for (int c = 0; c < row->count; c++) {
            TableCell *cell = &row->cells[c];
            int span = cell->colspan;
            if (col + span > t.cols) span = t.cols - col;
            int cw = 0;
            for (int k = 0; k < span; k++) cw += col_w[col + k];
            if (cw < 12) cw = 12;
            cell_x[c] = 0;
            for (int k = 0; k < col; k++) cell_x[c] += col_w[k];
            cell_w[c] = cw;
            int inner_w = cw - cell->style.padding_l - cell->style.padding_r;
            if (inner_w < 8) inner_w = 8;
            InlineBuilder ib;
            builder_init(&ib, &cell->style);
            cell_words(ctx, &ib, cell->node, &cell->style);
            builder_to_lines(ctx, &ib, &cell->style, inner_w, 0, &cell_lines[c]);
            builder_free(&ib);
            int h = cell->style.padding_t + cell->style.padding_b;
            for (int k = 0; k < cell_lines[c].count; k++) h += cell_lines[c].lines[k].height;
            if (h > max_h) max_h = h;
            col += span;
        }
        if (max_h < 12) max_h = 12;
        if (ctx->y + max_h > ctx->bottom && page_has_content(ctx)) new_page(ctx);
        int text_start = 0;
        for (int c = 0; c < row->count; c++) {
            TableCell *cell = &row->cells[c];
            EgStyle *cs = &cell->style;
            int x = ctx->x + cell_x[c];
            int y = ctx->y;
            int w = cell_w[c];
            if (cs->has_bg) add_rect_cmd(ctx, x, y, w, max_h, cs->bg);
            uint16_t bc = cs->border_color ? cs->border_color : eg_rgb565(150, 130, 100);
            if (cs->border_t) add_line_cmd(ctx, x, y, x + w, y, bc, cs->border_t);
            if (cs->border_b) add_line_cmd(ctx, x, y + max_h - 1, x + w, y + max_h - 1, bc, cs->border_b);
            if (cs->border_l) add_line_cmd(ctx, x, y, x, y + max_h, bc, cs->border_l);
            if (cs->border_r) add_line_cmd(ctx, x + w - 1, y, x + w - 1, y + max_h, bc, cs->border_r);
            int inner_w = w - cs->padding_l - cs->padding_r;
            if (inner_w < 8) inner_w = 8;
            EgPage *save_page = &ctx->ch->pages[ctx->page];
            (void)save_page;
            emit_cell_text(ctx, &cell_lines[c], cs, x + cs->padding_l, y, inner_w);
            (void)text_start;
        }
        for (int c = 0; c < row->count; c++) lines_free(&cell_lines[c]);
        free(cell_lines); free(cell_x); free(cell_w);
        ctx->y += max_h;
    }
    table_free(&t);
}

/* ================================================================== lists */

static void int_to_roman(int n, int upper, char *out, int outsz)
{
    static const char *rom[] = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
    static const int val[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
    out[0] = 0;
    if (n <= 0) n = 1;
    if (n > 3999) n = 3999;
    for (int i = 0; i < 13 && n > 0; i++) {
        while (n >= val[i]) {
            strncat(out, rom[i], (size_t)(outsz - (int)strlen(out) - 1));
            n -= val[i];
        }
    }
    if (!upper) for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
}

static void list_marker(int kind, int n, char *out, int outsz)
{
    if (kind == EG_LIST_NONE) { out[0] = 0; return; }
    if (kind == EG_LIST_DECIMAL) { snprintf(out, (size_t)outsz, "%d.", n); return; }
    if (kind == EG_LIST_LOWER_ALPHA || kind == EG_LIST_UPPER_ALPHA) {
        char c = (char)('a' + (n - 1) % 26);
        if (kind == EG_LIST_UPPER_ALPHA) c = (char)toupper((unsigned char)c);
        snprintf(out, (size_t)outsz, "%c.", c);
        return;
    }
    if (kind == EG_LIST_LOWER_ROMAN || kind == EG_LIST_UPPER_ROMAN) {
        char r[32];
        int_to_roman(n, kind == EG_LIST_UPPER_ROMAN, r, sizeof(r));
        snprintf(out, (size_t)outsz, "%s.", r);
        return;
    }
    if (kind == EG_LIST_SQUARE) snprintf(out, (size_t)outsz, "▪");
    else if (kind == EG_LIST_CIRCLE) snprintf(out, (size_t)outsz, "○");
    else snprintf(out, (size_t)outsz, "•");
}

static void layout_list(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *style)
{
    int kind = style->list_style;
    if (strcmp(node->tag, "ol") == 0 && kind == EG_LIST_DISC) kind = EG_LIST_DECIMAL;
    const char *start_attr = eg_xml_attr(node, "start");
    int number = start_attr ? atoi(start_attr) : 1;
    for (const EgXmlNode *li = node->children; li; li = li->next) {
        if (!li->tag || strcmp(li->tag, "li") != 0) continue;
        EgStyle ls;
        eg_css_compute(&ctx->css, li, style, &ls, ctx->set.font_size);
        if (ls.display == EG_DISPLAY_NONE || !ls.visible) continue;
        char marker[32];
        list_marker(kind, number++, marker, sizeof(marker));
        if (ls.margin_t > 0) ctx->y += ls.margin_t;
        InlineBuilder ib;
        builder_init(&ib, &ls);
        for (const EgXmlNode *c = li->children; c; c = c->next) {
            if (!c->tag) builder_append_text(&ib, c->text, &ls);
            else {
                EgStyle cs;
                eg_css_compute(&ctx->css, c, &ls, &cs, ctx->set.font_size);
                if (cs.display == EG_DISPLAY_NONE || !cs.visible) continue;
                if (strcmp(c->tag, "ul") == 0 || strcmp(c->tag, "ol") == 0) continue;
                builder_add_inline(ctx, &ib, c, &cs, 1);
            }
        }
        PLineList ll;
        memset(&ll, 0, sizeof(ll));
        builder_to_lines(ctx, &ib, &ls, ctx->width, 0, &ll);
        builder_free(&ib);
        if (ll.count > 0) {
            const EgFont *mf = face_for(ls.font_size, ls.bold, ls.italic);
            int mw = eg_text_width(marker, mf, 0);
            int mx = ctx->x - mw - 4;
            if (mx < 0) mx = 0;
            place_line_block(ctx, &ll, &ls, marker);
            (void)mx;
        }
        lines_free(&ll);
        for (const EgXmlNode *c = li->children; c; c = c->next) {
            if (!c->tag) continue;
            if (strcmp(c->tag, "ul") == 0 || strcmp(c->tag, "ol") == 0) {
                EgStyle cs;
                eg_css_compute(&ctx->css, c, &ls, &cs, ctx->set.font_size);
                if (cs.display != EG_DISPLAY_NONE && cs.visible) layout_block(ctx, c, &ls);
            }
        }
        ctx->y += ls.margin_b;
    }
}

/* ================================================================== blocks */

static void layout_block(LayoutCtx *ctx, const EgXmlNode *node, const EgStyle *parent)
{
    if (!node || !node->tag) return;
    if (is_skipped_tag(node->tag)) return;
    EgStyle style;
    eg_css_compute(&ctx->css, node, parent, &style, ctx->set.font_size);
    if (style.display == EG_DISPLAY_NONE || !style.visible) return;
    if (strcmp(node->tag, "br") == 0) return;

    add_anchor(ctx, node);
    int is_body = strcmp(node->tag, "body") == 0 || strcmp(node->tag, "#document") == 0;
    if (style.page_break_before && page_has_content(ctx)) new_page(ctx);
    if (!is_body && style.margin_t > 0 && ctx->y > ctx->top) ctx->y += style.margin_t;

    if (!is_body && style.padding_t > 0) ctx->y += style.padding_t;
    int save_x = ctx->x, save_w = ctx->width;
    int content_x = ctx->x + style.margin_l + style.padding_l;
    int content_w = ctx->width - style.margin_l - style.margin_r - style.padding_l - style.padding_r;
    if (content_w < 30) content_w = 30;
    ctx->x = content_x;
    ctx->width = content_w;

    if (strcmp(node->tag, "img") == 0) {
        layout_image(ctx, node, &style);
    } else if (strcmp(node->tag, "table") == 0) {
        layout_table(ctx, node, &style);
    } else if (strcmp(node->tag, "ul") == 0 || strcmp(node->tag, "ol") == 0) {
        layout_list(ctx, node, &style);
    } else if (strcmp(node->tag, "pre") == 0) {
        layout_pre(ctx, node, &style);
    } else if (strcmp(node->tag, "hr") == 0) {
        layout_hr(ctx);
    } else if (strcmp(node->tag, "style") == 0 || strcmp(node->tag, "script") == 0) {
        /* skip */
    } else {
        layout_contents(ctx, node, &style);
    }

    ctx->x = save_x;
    ctx->width = save_w;
    if (!is_body && style.padding_b > 0) ctx->y += style.padding_b;
    if (!is_body && style.margin_b > 0) ctx->y += style.margin_b;
    if (style.page_break_after && page_has_content(ctx)) new_page(ctx);
}

/* ================================================================ CSS load */

static void load_css_for_document(LayoutCtx *ctx, const EgXmlNode *doc)
{
    const EgXmlNode *htmldoc = doc;
    const EgXmlNode *head = NULL;
    for (const EgXmlNode *c = htmldoc->children; c; c = c->next) {
        if (c->tag && strcmp(c->tag, "html") == 0) {
            for (const EgXmlNode *h = c->children; h; h = h->next)
                if (h->tag && strcmp(h->tag, "head") == 0) { head = h; break; }
        }
    }
    if (!head) head = eg_xml_find(doc, "head");
    if (!head) return;
    for (const EgXmlNode *c = head->children; c; c = c->next) {
        if (!c->tag) continue;
        if (strcmp(c->tag, "link") == 0) {
            const char *rel = eg_xml_attr(c, "rel");
            const char *href = eg_xml_attr(c, "href");
            if (!href) continue;
            if (rel && strstr(rel, "stylesheet")) {
                uint8_t *data = NULL;
                size_t size = 0;
                if (eg_epub_read_rel(ctx->ep, ctx->chapter_dir, href, &data, &size)) {
                    eg_css_parse(&ctx->css, (const char *)data, (int)size);
                    free(data);
                }
            }
        } else if (strcmp(c->tag, "style") == 0) {
            char *css = eg_xml_text(c);
            if (css) {
                eg_css_parse(&ctx->css, css, (int)strlen(css));
                free(css);
            }
        }
    }
}

/* ================================================================== entry */

static char *chapter_heading(const EgXmlNode *body)
{
    const EgXmlNode *h = eg_xml_find(body, "h1");
    if (!h) h = eg_xml_find(body, "h2");
    if (!h) h = eg_xml_find(body, "title");
    if (!h) return NULL;
    char *t = eg_xml_text(h);
    if (!t) return NULL;
    char *trim = eg_trim(t);
    if (*trim) {
        char *out = eg_strdup(trim);
        free(t);
        return out;
    }
    free(t);
    return NULL;
}

int eg_layout_chapter(EgEpub *ep, int chapter_index,
                      const EgLayoutSettings *settings, EgChapter *out)
{
    memset(out, 0, sizeof(*out));
    out->index = chapter_index;
    if (!ep || chapter_index < 0 || chapter_index >= ep->spine_count) return 0;
    const char *path = eg_epub_chapter_href(ep, chapter_index);
    if (!path) return 0;
    uint8_t *data = NULL;
    size_t size = 0;
    if (!eg_epub_read(ep, path, &data, &size)) {
        eg_log("layout: cannot read chapter %d (%s)\n", chapter_index, path);
        return 0;
    }
    EgXmlNode *doc = eg_xml_parse((const char *)data, (int)size);
    free(data);
    if (!doc) return 0;

    LayoutCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ep = ep;
    ctx.ch = out;
    ctx.set = *settings;
    ctx.top = settings->content_top;
    ctx.bottom = settings->content_bottom;
    ctx.x = settings->margin_lr;
    ctx.width = EG_W - settings->margin_lr * 2;
    ctx.y = ctx.top;
    eg_path_dirname(path, ctx.chapter_dir, sizeof(ctx.chapter_dir));
    eg_css_init(&ctx.css);

    out->cap = 8;
    out->pages = eg_calloc((size_t)out->cap, sizeof(EgPage));
    out->page_count = 1;
    ctx.page = 0;

    load_css_for_document(&ctx, doc);
    const EgXmlNode *body = eg_xml_find(doc, "body");
    if (!body) body = eg_xml_find(doc, "html");
    if (!body) body = doc;

    EgStyle root;
    eg_style_default(&root);
    root.font_size = settings->font_size;
    root.line_height_normal = 1;
    root.line_height = (root.font_size * 13 + 5) / 10;
    if (body->tag) {
        EgStyle bs;
        eg_css_compute(&ctx.css, body, NULL, &bs, settings->font_size);
        bs.display = EG_DISPLAY_BLOCK;
        layout_block(&ctx, body, NULL);
        (void)bs;
    } else {
        layout_contents(&ctx, body, &root);
    }

    if (out->page_count > 1 && out->pages[out->page_count - 1].count == 0)
        out->page_count--;

    char *title = chapter_heading(body);
    int auto_title = 0;
    out->is_cover = (strstr(path, "cover") != NULL);
    if (!title) {
        const char *tt = eg_epub_chapter_title(ep, chapter_index);
        if (tt && tt[0]) title = eg_strdup(tt);
    }
    if (!title) {
        auto_title = 1;
        title = eg_strdup(out->is_cover ? "Cover" : "Chapter");
    }
    out->auto_title = auto_title;
    out->title = title;

    eg_css_free(&ctx.css);
    eg_xml_free(doc);
    eg_log("layout: chapter %d '%s' -> %d pages, %d images\n",
           chapter_index, out->title ? out->title : "?", out->page_count, out->image_count);
    return 1;
}

void eg_chapter_free(EgChapter *ch)
{
    if (!ch) return;
    for (int i = 0; i < ch->page_count; i++) {
        EgPage *p = &ch->pages[i];
        for (int j = 0; j < p->count; j++) {
            EgCmd *c = &p->cmds[j];
            free(c->text);
        }
        free(p->cmds);
    }
    free(ch->pages);
    for (int i = 0; i < ch->anchor_count; i++) free(ch->anchors[i].name);
    free(ch->anchors);
    for (int i = 0; i < ch->image_count; i++) {
        free(ch->images[i].href);
        eg_image_free(&ch->images[i].image);
    }
    free(ch->images);
    free(ch->title);
    memset(ch, 0, sizeof(*ch));
}

int eg_chapter_page_count(const EgChapter *ch)
{
    return ch ? ch->page_count : 0;
}

int eg_chapter_cmd_count(const EgChapter *ch)
{
    int n = 0;
    if (!ch) return 0;
    for (int i = 0; i < ch->page_count; i++) n += ch->pages[i].count;
    return n;
}
