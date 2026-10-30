#include "app.h"
#include "gfx.h"
#include "epub.h"
#include "layout.h"
#include "fonts.h"
#include "lang.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define BOOK_W 62
#define BOOK_H 82
#define BOOKS_VISIBLE 6
#define BOOKS_PER_ROW 3
#define MAX_FILES 256
#define MAX_STATES 192
#define MAX_BOOKMARKS 16
#define APP_VERSION "1.0"
#define READER_TOP 22
#define READER_BOTTOM 228
#define READER_CONTENT_H (READER_BOTTOM - READER_TOP)
#define READER_SCROLL_STEP 4

typedef struct {
    uint16_t paper, ink, dim, accent, line, panel, panel_ink, panel_dim, shadow;
    uint16_t lib_bg, lib_ink, lib_dim, lib_line;
    int dark;
} AppTheme;

static const AppTheme g_themes[3] = {
    { 0xF77B, 0x2903, 0x6AE9, 0x3331, 0xDEFB, 0xFFDF, 0x18E3, 0x6B4D, 0x0000,
      0xF7BE, 0x18E3, 0x6B4D, 0xDEDA, 0 },
    { 0xEE95, 0x3943, 0x72E8, 0x4B6F, 0xD656, 0xF77C, 0x3964, 0x734B, 0x0000,
      0xF75B, 0x3964, 0x734B, 0xDE56, 0 },
    { 0x10A3, 0xD657, 0x83EE, 0x8D99, 0x39E8, 0x18E4, 0xE71C, 0x94B2, 0x0000,
      0x1082, 0xCE79, 0x7BEF, 0x2987, 1 }
};

typedef struct {
    int chapter, page;
} EgMark;

typedef struct {
    uint32_t hash;
    int chapter, page, percent;
    EgMark bm[MAX_BOOKMARKS];
    int bm_count;
    int used;
} EgStateBook;

typedef struct {
    char path[EG_MAX_PATH];
    char filename[192];
    char title[256];
    char author[160];
    uint32_t hash;
    int has_cover;
    EgImage cover;
    int chapter, page, percent;
    EgMark bm[MAX_BOOKMARKS];
    int bm_count;
} EgBook;

enum { VIEW_SCAN = 0, VIEW_LIBRARY, VIEW_READER };
enum { OV_NONE = 0, OV_MENU, OV_TOC, OV_SETTINGS, OV_BOOKMARKS, OV_INFO, OV_HELP };

struct EgApp {
    uint16_t fb[EG_W * EG_H];
    uint16_t page_cache[2][EG_W * EG_H];
    EgGfx g;
    char base_dir[EG_MAX_PATH];
    char books_dir[EG_MAX_PATH];
    char save_dir[EG_MAX_PATH];
    char state_path[EG_MAX_PATH];
    char **files;
    int file_count;
    int file_index;
    int scanning;
    EgBook *books;
    int book_count, book_cap;
    EgStateBook states[MAX_STATES];
    int state_count;
    int theme;
    int language;
    int font_size;
    int justify;
    int margin;
    int sort_mode;
    int selected;
    int shelf_page;
    int marquee_frame;
    enum { SCAN_INIT = 0, SCAN_ACTIVE, SCAN_DONE } scan_state;
    int view;
    int overlay;
    int menu_sel;
    int toc_sel, toc_scroll;
    int set_sel;
    int bm_sel, bm_scroll;
    int needs_reflow;
    int book_index;
    EgEpub epub;
    int epub_open;
    EgChapter chapter;
    int chapter_loaded;
    int page;
    int layout_serial;
    int cache_valid[2];
    int cache_page[2];
    int cache_chapter[2];
    int cache_theme[2];
    int cache_serial[2];
    int scroll;
    int scroll_stopped;
    int scroll_cooldown;
    int prev_pad;
    int hold[4];
    int frame;
    int dirty;
    int state_dirty;
    int save_timer;
    char toast[160];
    int toast_timer;
    int info_scroll;
    char status[192];
    int unknown_pad;
};

static const int g_col_x[3] = { 34, 129, 224 };
static const int g_row_y[2] = { 25, 123 };
#define LIB_HEADER_LINE 22
#define LIB_TITLE_GAP 12
#define LIB_FOOTER_LINE 223
#define LIB_FOOTER_BASE 236
#define MARQUEE_W 90

static const AppTheme *app_theme(EgApp *a);

/* ---------------------------------------------------------------- helpers */

static EgStyle style_make(int size, int bold, int italic)
{
    EgStyle s;
    eg_style_default(&s);
    s.font_size = size;
    s.bold = bold;
    s.italic = italic;
    s.line_height = (size * 13 + 5) / 10;
    s.line_height_normal = 0;
    s.display = EG_DISPLAY_BLOCK;
    s.align = EG_ALIGN_LEFT;
    s.text_indent = 0;
    s.sans = 1;
    return s;
}

static void toast(EgApp *a, const char *msg)
{
    snprintf(a->toast, sizeof(a->toast), "%s", msg ? msg : "");
    a->toast_timer = 100;
    a->dirty = 1;
}

static void clear_toast(EgApp *a)
{
    a->toast_timer = 0;
    a->toast[0] = 0;
}

static void open_settings_overlay(EgApp *a);
static void reader_settings_input(EgApp *a, uint16_t pressed);

static const char *T(EgApp *a, int id)
{
    return eg_tr(a->language, id);
}

static void book_count_text(EgApp *a, int n, char *buf, size_t size)
{
    const char *fmt;
    if (a->language == LANG_PL) {
        int m100 = n % 100, m10 = n % 10;
        if (n == 1) fmt = T(a, L_BOOK_ONE);
        else if (m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14)) fmt = T(a, L_BOOK_FEW);
        else fmt = T(a, L_BOOK_MANY);
    } else if (a->language == LANG_JA) {
        fmt = T(a, L_BOOK_MANY);
    } else if (n == 1) {
        fmt = T(a, L_BOOK_ONE);
    } else {
        fmt = T(a, L_BOOK_MANY);
    }
    snprintf(buf, size, fmt, n);
}

static const char *display_author(EgApp *a, const EgBook *b)
{
    return (b && b->author[0]) ? b->author : T(a, L_UNKNOWN_AUTHOR);
}

static void chapter_title_text(EgApp *a, char *buf, size_t size)
{
    if (!a->chapter_loaded) {
        snprintf(buf, size, "%s", "");
    } else if (a->chapter.is_cover) {
        snprintf(buf, size, "%s", T(a, L_COVER));
    } else if (a->chapter.auto_title) {
        snprintf(buf, size, T(a, L_CHAPTER_FMT), a->chapter.index + 1);
    } else {
        snprintf(buf, size, "%s", a->chapter.title ? a->chapter.title : "");
    }
}

static const char *toc_title_text(EgApp *a, int entry_index, char *buf, size_t size)
{
    if (!a->epub_open || entry_index < 0 || entry_index >= a->epub.toc_count) return "";
    EgTocEntry *e = &a->epub.toc[entry_index];
    if (e->title && e->title[0]) return e->title;
    if (e->chapter >= 0) {
        const char *href = eg_epub_chapter_href(&a->epub, e->chapter);
        if (e->chapter == 0 && href && strstr(href, "cover")) {
            snprintf(buf, size, "%s", T(a, L_COVER));
        } else {
            snprintf(buf, size, T(a, L_CHAPTER_FMT), e->chapter + 1);
        }
        return buf;
    }
    return "";
}

static void draw_clipped(EgGfx *g, int x, int baseline, int max_w,
                         const char *text, const EgStyle *style, uint16_t color)
{
    if (!text || !*text) return;
    if (eg_gfx_text_width(text, style) <= max_w) {
        eg_gfx_draw_text(g, x, baseline, text, style, color);
        return;
    }
    char buf[256];
    int len = (int)strlen(text);
    if (len > 250) len = 250;
    const char *p = text;
    const char *cut = text;
    while (*p && (int)(p - text) < len) {
        const char *next = p;
        uint32_t cp;
        eg_utf8_decode(&next, &cp);
        int n = (int)(next - text);
        char tmp[256];
        memcpy(tmp, text, (size_t)n);
        memcpy(tmp + n, "…", 4);
        tmp[n + 3] = 0;
        if (eg_gfx_text_width(tmp, style) > max_w) break;
        cut = next;
        p = next;
    }
    int n = (int)(cut - text);
    if (n > 250) n = 250;
    memcpy(buf, text, (size_t)n);
    memcpy(buf + n, "…", 4);
    buf[n + 3] = 0;
    if (n == 0) {
        const char *ell = "…";
        eg_gfx_draw_text(g, x, baseline, ell, style, color);
    } else {
        eg_gfx_draw_text(g, x, baseline, buf, style, color);
    }
}

static void draw_text_right(EgGfx *g, int right, int baseline, const char *text,
                            const EgStyle *style, uint16_t color)
{
    int w = eg_gfx_text_width(text, style);
    eg_gfx_draw_text(g, right - w, baseline, text, style, color);
}

static void draw_text_center(EgGfx *g, int cx, int baseline, const char *text,
                             const EgStyle *style, uint16_t color)
{
    int w = eg_gfx_text_width(text, style);
    eg_gfx_draw_text(g, cx - w / 2, baseline, text, style, color);
}

static void draw_centered_clipped(EgGfx *g, int cx, int baseline, int max_w,
                                 const char *text, const EgStyle *style, uint16_t color)
{
    if (!text || !*text) return;
    if (eg_gfx_text_width(text, style) <= max_w) {
        draw_text_center(g, cx, baseline, text, style, color);
        return;
    }
    char buf[256];
    const char *p = text;
    const char *cut = text;
    while (*p) {
        const char *next = p;
        uint32_t cp;
        eg_utf8_decode(&next, &cp);
        int n = (int)(next - text);
        char tmp[256];
        memcpy(tmp, text, (size_t)n);
        memcpy(tmp + n, "…", 4);
        tmp[n + 3] = 0;
        if (eg_gfx_text_width(tmp, style) > max_w) break;
        cut = next;
        p = next;
    }
    int n = (int)(cut - text);
    if (n > 250) n = 250;
    memcpy(buf, text, (size_t)n);
    memcpy(buf + n, "…", 4);
    buf[n + 3] = 0;
    if (n == 0) draw_text_center(g, cx, baseline, "…", style, color);
    else draw_text_center(g, cx, baseline, buf, style, color);
}

static void draw_panel(EgGfx *g, int x, int y, int w, int h, const AppTheme *th)
{
    eg_gfx_blend(g, x + 3, y + 3, w, h, th->shadow, 120);
    eg_gfx_round_rect(g, x, y, w, h, 6, th->panel, 1, th->accent);
    eg_gfx_round_rect(g, x + 1, y + 1, w - 2, h - 2, 5, 0, 0, th->line);
}

static void draw_dim(EgGfx *g)
{
    eg_gfx_blend(g, 0, 0, EG_W, EG_H, 0x0000, 120);
}

static int ui_visible_rows(int panel_h)
{
    int n = (panel_h - 44) / 18;
    if (n < 1) n = 1;
    return n;
}

static void draw_footer_hint(EgGfx *g, int x, int y, int w, const char *hint,
                             const AppTheme *th, const EgStyle *small)
{
    int hw = eg_gfx_text_width(hint, small);
    if (hw > w) {
        draw_clipped(g, x, y, w, hint, small, th->panel_dim);
    } else {
        draw_text_center(g, x + w / 2, y, hint, small, th->panel_dim);
    }
}

/* ------------------------------------------------------------------ state */

static uint32_t book_hash(const char *filename)
{
    return eg_hash32(filename);
}

static EgStateBook *state_find(EgApp *a, uint32_t hash)
{
    for (int i = 0; i < a->state_count; i++)
        if (a->states[i].used && a->states[i].hash == hash) return &a->states[i];
    return NULL;
}

static void state_reset(EgApp *a)
{
    a->theme = 0;
    a->language = LANG_EN;
    a->font_size = 15;
    a->justify = 1;
    a->margin = 18;
    a->sort_mode = 0;
    a->state_count = 0;
}

static void state_load(EgApp *a)
{
    state_reset(a);
    size_t size = 0;
    uint8_t *data = eg_read_file(a->state_path, &size);
    if (!data) return;
    char *txt = (char *)data;
    char *p = txt;
    while (p && *p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = 0; p = nl + 1; }
        else p = NULL;
        char *s = eg_trim(line);
        if (!*s) continue;
        if (strncmp(s, "theme ", 6) == 0) a->theme = eg_clampi(atoi(s + 6), 0, 2);
        else if (strncmp(s, "language ", 9) == 0) a->language = eg_clampi(atoi(s + 9), 0, LANG_COUNT - 1);
        else if (strncmp(s, "font ", 5) == 0) {
            int v = atoi(s + 5);
            if (v == 12 || v == 15 || v == 19 || v == 24) a->font_size = v;
        } else if (strncmp(s, "justify ", 8) == 0) a->justify = atoi(s + 8) ? 1 : 0;
        else if (strncmp(s, "margin ", 7) == 0) {
            int v = atoi(s + 7);
            if (v == 12 || v == 18 || v == 26) a->margin = v;
        } else if (strncmp(s, "sort ", 5) == 0) a->sort_mode = eg_clampi(atoi(s + 5), 0, 1);
        else if (strncmp(s, "book ", 5) == 0 && a->state_count < MAX_STATES) {
            unsigned hash = 0;
            int ch = 0, page = 0, pct = 0, nbm = 0, used = 0;
            if (sscanf(s + 5, "%x %d %d %d %d %n", &hash, &ch, &page, &pct, &nbm, &used) >= 5) {
                EgStateBook *st = &a->states[a->state_count++];
                memset(st, 0, sizeof(*st));
                st->hash = (uint32_t)hash;
                st->chapter = ch < 0 ? 0 : ch;
                st->page = page < 0 ? 0 : page;
                st->percent = eg_clampi(pct, 0, 1000);
                st->used = 1;
                char *r = s + 5 + used;
                for (int i = 0; i < nbm && i < MAX_BOOKMARKS; i++) {
                    int mc = 0, mp = 0, q = 0;
                    if (sscanf(r, "%d:%d %n", &mc, &mp, &q) < 2) break;
                    st->bm[st->bm_count].chapter = mc;
                    st->bm[st->bm_count].page = mp;
                    st->bm_count++;
                    r += q;
                }
            }
        }
    }
    free(data);
    eg_log("state: theme=%d language=%d font=%d margin=%d justify=%d books=%d\n",
           a->theme, a->language, a->font_size, a->margin, a->justify, a->state_count);
}

void eg_app_save(EgApp *a)
{
    if (!a) return;
    if (a->view == VIEW_READER && a->epub_open && a->chapter_loaded) {
        if (a->book_index >= 0 && a->book_index < a->book_count) {
            EgBook *b = &a->books[a->book_index];
            b->chapter = a->chapter.index;
            b->page = a->page;
        }
    }
    EgStr s;
    eg_str_init(&s);
    eg_str_appendf(&s, "EGREADER 1\ntheme %d\nlanguage %d\nfont %d\njustify %d\nmargin %d\nsort %d\n",
                   a->theme, a->language, a->font_size, a->justify, a->margin, a->sort_mode);
    for (int i = 0; i < a->book_count; i++) {
        EgBook *b = &a->books[i];
        eg_str_appendf(&s, "book %08x %d %d %d %d", b->hash, b->chapter, b->page,
                       b->percent, b->bm_count);
        for (int j = 0; j < b->bm_count; j++)
            eg_str_appendf(&s, " %d:%d", b->bm[j].chapter, b->bm[j].page);
        eg_str_append(&s, "\n", 1);
    }
    eg_mkdir_p(a->save_dir);
    if (!eg_write_file(a->state_path, s.data ? s.data : "", (size_t)s.len))
        eg_log("state: cannot write %s\n", a->state_path);
    eg_str_free(&s);
    a->state_dirty = 0;
    a->save_timer = 0;
}

static void state_touch(EgApp *a)
{
    a->state_dirty = 1;
    a->save_timer = 60;
}

/* ---------------------------------------------------------------- storage */

static int sort_mode_global;
static int cmp_book(const void *pa, const void *pb)
{
    const EgBook *a = (const EgBook *)pa;
    const EgBook *b = (const EgBook *)pb;
    int r = 0;
    if (sort_mode_global == 1) r = eg_stricmp(a->author, b->author);
    if (r == 0) r = eg_stricmp(a->title, b->title);
    if (r == 0) r = eg_stricmp(a->filename, b->filename);
    return r;
}

static void sort_books(EgApp *a)
{
    sort_mode_global = a->sort_mode;
    if (a->book_count > 1) qsort(a->books, (size_t)a->book_count, sizeof(EgBook), cmp_book);
}

static int cmp_file(const void *pa, const void *pb)
{
    const char *a = *(const char *const *)pa;
    const char *b = *(const char *const *)pb;
    return eg_stricmp(a, b);
}

static void free_files(EgApp *a)
{
    for (int i = 0; i < a->file_count; i++) free(a->files[i]);
    free(a->files);
    a->files = NULL;
    a->file_count = 0;
    a->file_index = 0;
}

static int list_epubs(EgApp *a)
{
    free_files(a);
    DIR *d = opendir(a->books_dir);
    if (!d) {
        eg_log("library: cannot open %s\n", a->books_dir);
        return 0;
    }
    a->files = eg_calloc(MAX_FILES, sizeof(char *));
    if (!a->files) { closedir(d); return 0; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && a->file_count < MAX_FILES) {
        const char *name = de->d_name;
        if (!name || name[0] == '.') continue;
        if (!eg_has_suffix_ci(name, ".epub")) continue;
        a->files[a->file_count++] = eg_strdup(name);
    }
    closedir(d);
    if (a->file_count > 1)
        qsort(a->files, (size_t)a->file_count, sizeof(char *), cmp_file);
    return a->file_count;
}

static void book_cover_clear(EgBook *b)
{
    if (b->has_cover) {
        eg_image_free(&b->cover);
        b->has_cover = 0;
    }
}

static void books_free(EgApp *a)
{
    for (int i = 0; i < a->book_count; i++) book_cover_clear(&a->books[i]);
    free(a->books);
    a->books = NULL;
    a->book_count = 0;
    a->book_cap = 0;
}

static void scan_book(EgApp *a, const char *filename)
{
    char path[EG_MAX_PATH];
    eg_path_join(path, sizeof(path), a->books_dir, filename);
    EgEpub ep;
    if (!eg_epub_open(&ep, path)) {
        eg_log("library: skipping %s\n", filename);
        return;
    }
    if (a->book_count == a->book_cap) {
        int cap = a->book_cap ? a->book_cap * 2 : 16;
        EgBook *nb = eg_realloc(a->books, sizeof(EgBook) * (size_t)cap);
        if (!nb) { eg_epub_close(&ep); return; }
        a->books = nb;
        a->book_cap = cap;
    }
    EgBook *b = &a->books[a->book_count++];
    memset(b, 0, sizeof(*b));
    snprintf(b->path, sizeof(b->path), "%s", path);
    snprintf(b->filename, sizeof(b->filename), "%s", filename);
    b->hash = book_hash(filename);
    snprintf(b->title, sizeof(b->title), "%s", ep.title[0] ? ep.title : filename);
    snprintf(b->author, sizeof(b->author), "%s", ep.author[0] ? ep.author : "");
    uint8_t *cover = NULL;
    size_t cover_size = 0;
    if (eg_epub_load_cover(&ep, &cover, &cover_size)) {
        if (eg_image_load_cover(cover, cover_size, BOOK_W, BOOK_H,
                                eg_rgb565(238, 225, 201), &b->cover)) {
            b->has_cover = 1;
        }
        free(cover);
    }
    EgStateBook *st = state_find(a, b->hash);
    if (st) {
        b->chapter = st->chapter;
        b->page = st->page;
        b->percent = st->percent;
        b->bm_count = st->bm_count;
        memcpy(b->bm, st->bm, sizeof(EgMark) * (size_t)st->bm_count);
    }
    eg_epub_close(&ep);
}

static void finish_scan(EgApp *a)
{
    a->scanning = 0;
    a->scan_state = SCAN_DONE;
    sort_books(a);
    a->selected = 0;
    a->shelf_page = 0;
    a->marquee_frame = 0;
    a->view = VIEW_LIBRARY;
    a->dirty = 1;
    eg_log("library: scan done, %d books\n", a->book_count);
}

static void scan_step(EgApp *a)
{
    if (!a->scanning) return;
    if (a->file_index >= a->file_count) { finish_scan(a); return; }
    scan_book(a, a->files[a->file_index++]);
    if (a->file_index >= a->file_count) finish_scan(a);
    else {
        a->scan_state = SCAN_ACTIVE;
        a->dirty = 1;
    }
}

/* --------------------------------------------------------------- surfaces */

static void draw_book_icon(EgGfx *g, int cx, int cy, int scale, uint16_t paper, uint16_t edge, uint16_t ink)
{
    int w = 35 * scale / 100;
    int h = 46 * scale / 100;
    int gap = 3 * scale / 100 + 1;
    if (w < 8) w = 8;
    if (h < 10) h = 10;
    int x0 = cx - w - gap / 2;
    int x1 = cx + gap / 2;
    int y0 = cy - h / 2;
    eg_gfx_round_rect(g, x0, y0, w, h, 2, paper, 1, edge);
    eg_gfx_round_rect(g, x1, y0, w, h, 2, paper, 1, edge);
    eg_gfx_fill(g, cx - gap / 2, y0 + 2, gap, h - 4, edge);
    for (int i = 0; i < 5; i++) {
        int yy = y0 + 8 + i * (h - 18) / 6;
        eg_gfx_fill(g, x0 + 5, yy, w - 10, 1, ink);
        eg_gfx_fill(g, x1 + 5, yy, w - 10, 1, ink);
    }
    eg_gfx_line(g, x0, y0 + h, x1 + w, y0 + h, edge, 1);
}


static void draw_fallback_cover(EgApp *a, EgBook *b, int x, int y)
{
    const AppTheme *th = app_theme(a);
    eg_gfx_fill(&a->g, x, y, BOOK_W, BOOK_H, th->panel);
    eg_gfx_rect(&a->g, x, y, BOOK_W, BOOK_H, th->lib_line);
    EgStyle title = style_make(12, 1, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_centered_clipped(&a->g, x + BOOK_W / 2, y + 34, BOOK_W - 12,
                          b->title, &title, th->panel_ink);
    eg_gfx_hline(&a->g, x + 14, y + 41, BOOK_W - 28, th->accent);
    draw_centered_clipped(&a->g, x + BOOK_W / 2, y + 56, BOOK_W - 12,
                          b->author[0] ? b->author : T(a, L_UNKNOWN_AUTHOR),
                          &small, th->panel_dim);
}

static void draw_selected_title(EgApp *a, int x, int y, const char *title,
                                 const EgStyle *style, uint16_t color)
{
    const AppTheme *th = app_theme(a);
    int cx = x + BOOK_W / 2;
    int baseline = y + BOOK_H + LIB_TITLE_GAP;
    int band_y = baseline - 11;
    int wx = cx - MARQUEE_W / 2;
    eg_gfx_fill(&a->g, wx, band_y, MARQUEE_W, 15, th->lib_bg);
    int w = eg_gfx_text_width(title, style);
    if (w <= MARQUEE_W) {
        draw_text_center(&a->g, cx, baseline, title, style, color);
        return;
    }
    int overflow = w - MARQUEE_W;
    int pause = 30;
    int cycle = 2 * (pause + overflow);
    int p = cycle > 0 ? a->marquee_frame % cycle : 0;
    int off;
    if (p < pause) off = 0;
    else if (p < pause + overflow) off = p - pause;
    else if (p < pause + overflow + pause) off = overflow;
    else off = overflow - (p - pause - overflow - pause);
    if (off < 0) off = 0;
    if (off > overflow) off = overflow;
    eg_gfx_set_clip(&a->g, wx, band_y, MARQUEE_W, 15);
    eg_gfx_draw_text(&a->g, wx - off, baseline, title, style, color);
    eg_gfx_reset_clip(&a->g);
}

static int selected_title_overflow(EgApp *a)
{
    if (a->book_count <= 0 || a->selected < 0 || a->selected >= a->book_count) return 0;
    EgStyle title = style_make(12, 1, 0);
    int w = eg_gfx_text_width(a->books[a->selected].title, &title);
    return w > MARQUEE_W ? w - MARQUEE_W : 0;
}

static void redraw_selected_title(EgApp *a)
{
    if (a->book_count <= 0 || a->selected < 0 || a->selected >= a->book_count) return;
    int slot = a->selected - a->shelf_page * BOOKS_VISIBLE;
    if (slot < 0 || slot >= BOOKS_VISIBLE) return;
    EgBook *b = &a->books[a->selected];
    EgStyle title = style_make(12, 1, 0);
    draw_selected_title(a, g_col_x[slot % BOOKS_PER_ROW], g_row_y[slot / BOOKS_PER_ROW],
                        b->title, &title, app_theme(a)->accent);
}

static void draw_shelf_book(EgApp *a, EgBook *b, int idx, int slot)
{
    const AppTheme *th = app_theme(a);
    int col = slot % BOOKS_PER_ROW;
    int row = slot / BOOKS_PER_ROW;
    int x = g_col_x[col];
    int y = g_row_y[row];
    int selected = (a->selected == idx && a->view == VIEW_LIBRARY);

    eg_gfx_blend(&a->g, x + 1, y + 2, BOOK_W, BOOK_H, 0x0000, 28);
    if (b->has_cover) {
        eg_gfx_image(&a->g, &b->cover, x, y, BOOK_W, BOOK_H);
    } else {
        draw_fallback_cover(a, b, x, y);
    }
    eg_gfx_rect(&a->g, x, y, BOOK_W, BOOK_H, th->lib_line);
    if (selected) {
        eg_gfx_rect(&a->g, x + 1, y + 1, BOOK_W - 2, BOOK_H - 2, th->accent);
        eg_gfx_rect(&a->g, x + 2, y + 2, BOOK_W - 4, BOOK_H - 4, th->accent);
    }
    if (b->percent > 0) {
        eg_gfx_fill(&a->g, x + 2, y + BOOK_H - 4, BOOK_W - 4, 2, 0x18E3);
        int fw = (BOOK_W - 4) * b->percent / 1000;
        if (fw > 0) eg_gfx_fill(&a->g, x + 2, y + BOOK_H - 4, fw, 2, th->accent);
    }
    if (b->bm_count > 0) {
        eg_gfx_fill(&a->g, x + BOOK_W - 6, y + 1, 3, 7, th->accent);
        eg_gfx_fill(&a->g, x + BOOK_W - 6, y + 8, 3, 2, th->lib_line);
    }

    EgStyle title = style_make(12, selected ? 1 : 0, 0);
    if (selected)
        draw_selected_title(a, x, y, b->title, &title, th->accent);
    else
        draw_centered_clipped(&a->g, x + BOOK_W / 2, y + BOOK_H + LIB_TITLE_GAP,
                              90, b->title, &title, th->lib_ink);
}

static void draw_library_header(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle hdr = style_make(19, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    eg_gfx_draw_text(&a->g, 16, 17, T(a, L_LIBRARY_MENU), &hdr, th->lib_ink);
    char count[48], buf[96];
    int pages = (a->book_count + BOOKS_VISIBLE - 1) / BOOKS_VISIBLE;
    if (pages < 1) pages = 1;
    book_count_text(a, a->book_count, count, sizeof(count));
    if (a->book_count == 0)
        snprintf(buf, sizeof(buf), "%s", count);
    else
        snprintf(buf, sizeof(buf), "%s   %d/%d", count, a->shelf_page + 1, pages);
    draw_text_right(&a->g, EG_W - 16, 16, buf, &small, th->lib_dim);
    eg_gfx_hline(&a->g, 16, LIB_HEADER_LINE, EG_W - 32, th->lib_line);
}

static void draw_library_footer(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle small = style_make(12, 0, 0);
    eg_gfx_hline(&a->g, 16, LIB_FOOTER_LINE, EG_W - 32, th->lib_line);
    if (a->book_count > 0 && a->selected < a->book_count) {
        EgBook *b = &a->books[a->selected];
        draw_clipped(&a->g, 16, LIB_FOOTER_BASE, 130,
                     display_author(a, b), &small, th->lib_dim);
        draw_text_right(&a->g, EG_W - 16, LIB_FOOTER_BASE,
                        T(a, L_SORT_HINT), &small, th->lib_dim);
    } else {
        draw_text_center(&a->g, EG_W / 2, LIB_FOOTER_BASE,
                         T(a, L_HELP_HINT), &small, th->lib_dim);
    }
}

static void draw_empty_library(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(19, 0, 0);
    EgStyle s = style_make(12, 0, 0);
    draw_book_icon(&a->g, 160, 82, 150, th->lib_bg, th->lib_dim, th->lib_dim);
    draw_text_center(&a->g, 160, 143, T(a, L_EMPTY_TITLE), &h, th->lib_ink);
    draw_text_center(&a->g, 160, 166, T(a, L_EMPTY_LINE1), &s, th->lib_dim);
    draw_text_center(&a->g, 160, 184, T(a, L_EMPTY_PATH), &s, th->accent);
    draw_text_center(&a->g, 160, 202, T(a, L_EMPTY_LINE2), &s, th->lib_dim);
}

static void draw_library(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    eg_gfx_clear(&a->g, th->lib_bg);
    eg_gfx_init(&a->g, a->fb, EG_W, EG_H);
    a->g.dark_theme = th->dark;
    draw_library_header(a);
    if (a->book_count == 0) {
        draw_empty_library(a);
        draw_library_footer(a);
        return;
    }
    int base = a->shelf_page * BOOKS_VISIBLE;
    for (int i = 0; i < BOOKS_VISIBLE; i++) {
        int idx = base + i;
        if (idx >= a->book_count) break;
        draw_shelf_book(a, &a->books[idx], idx, i);
    }
    draw_library_footer(a);
}

static void draw_scan(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgGfx *g = &a->g;
    eg_gfx_clear(g, th->lib_bg);
    draw_book_icon(g, 160, 88, 150, th->lib_bg, th->lib_dim, th->lib_dim);
    EgStyle title = style_make(19, 1, 0);
    EgStyle label = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    title.letter_spacing = 0;
    draw_text_center(g, 160, 143, "EPUB Reader", &title, th->lib_ink);
    draw_text_center(g, 160, 168, T(a, L_SCAN_TITLE), &label, th->lib_dim);
    char buf[96];
    if (a->file_count > 0)
        snprintf(buf, sizeof(buf), T(a, L_SCAN_FMT), a->file_index, a->file_count);
    else
        snprintf(buf, sizeof(buf), "%s", T(a, L_SCAN));
    draw_text_center(g, 160, 190, buf, &small, th->lib_dim);
    int bw = 200, bx = (EG_W - bw) / 2, by = 210;
    eg_gfx_fill(g, bx, by, bw, 3, th->lib_line);
    int fillw = a->file_count > 0 ? bw * a->file_index / a->file_count : 0;
    if (fillw > 0) eg_gfx_fill(g, bx, by, fillw, 3, th->accent);
}

/* ----------------------------------------------------------------- reader */

static EgLayoutSettings current_layout_settings(EgApp *a)
{
    EgLayoutSettings s;
    s.font_size = a->font_size;
    s.justify = a->justify;
    s.margin_lr = a->margin;
    s.content_top = READER_TOP;
    s.content_bottom = READER_BOTTOM;
    return s;
}

static const AppTheme *app_theme(EgApp *a)
{
    return &g_themes[eg_clampi(a->theme, 0, 2)];
}

static void update_percent(EgApp *a)
{
    if (a->book_index < 0 || a->book_index >= a->book_count) return;
    EgBook *b = &a->books[a->book_index];
    int pages = eg_chapter_page_count(&a->chapter);
    int spine = a->epub.spine_count > 0 ? a->epub.spine_count : 1;
    int pct = 0;
    const char *chref = eg_epub_chapter_href(&a->epub, a->chapter.index);
    int is_cover = chref && strstr(chref, "cover");
    if (!is_cover && pages > 0) {
        pct = (a->chapter.index * 1000 +
               (a->page + 1) * 1000 / pages) / spine;
    }
    b->percent = eg_clampi(pct, 0, 1000);
    b->chapter = a->chapter.index;
    b->page = a->page;
    a->shelf_page = a->book_index / BOOKS_VISIBLE;
}

static int load_chapter(EgApp *a, int index, int page, const char *fragment)
{
    if (index < 0 || index >= a->epub.spine_count) return 0;
    if (a->chapter_loaded) {
        eg_chapter_free(&a->chapter);
        a->chapter_loaded = 0;
    }
    a->page = 0;
    a->scroll = 0;
    a->scroll_stopped = 0;
    a->scroll_cooldown = 0;
    EgLayoutSettings set = current_layout_settings(a);
    a->g.dark_theme = app_theme(a)->dark;
    if (!eg_layout_chapter(&a->epub, index, &set, &a->chapter)) {
        toast(a, T(a, L_CANNOT_CHAPTER));
        return 0;
    }
    a->chapter_loaded = 1;
    a->layout_serial++;
    if (fragment) {
        int p = eg_chapter_find_anchor(&a->chapter, fragment);
        if (p >= 0) page = p;
    }
    a->page = eg_clampi(page, 0, a->chapter.page_count > 0 ? a->chapter.page_count - 1 : 0);
    update_percent(a);
    a->dirty = 1;
    return 1;
}

static void close_reader(EgApp *a)
{
    if (a->epub_open) {
        update_percent(a);
        eg_epub_close(&a->epub);
        a->epub_open = 0;
        state_touch(a);
    }
    if (a->chapter_loaded) {
        eg_chapter_free(&a->chapter);
        a->chapter_loaded = 0;
    }
    a->view = VIEW_LIBRARY;
    a->overlay = OV_NONE;
    a->book_index = -1;
    a->scroll = 0;
    clear_toast(a);
    a->selected = eg_clampi(a->selected, 0, a->book_count > 0 ? a->book_count - 1 : 0);
    a->shelf_page = a->selected / BOOKS_VISIBLE;
    a->dirty = 1;
}

static void open_book(EgApp *a)
{
    if (a->selected < 0 || a->selected >= a->book_count) return;
    EgBook *b = &a->books[a->selected];
    if (a->epub_open) close_reader(a);
    if (!eg_epub_open(&a->epub, b->path)) {
        toast(a, T(a, L_CANNOT_OPEN));
        return;
    }
    a->epub_open = 1;
    a->chapter_loaded = 0;
    a->book_index = a->selected;
    clear_toast(a);
    int chapter = b->chapter;
    if (chapter < 0 || chapter >= a->epub.spine_count) chapter = 0;
    int page = b->page;
    if (!load_chapter(a, chapter, page, NULL)) {
        close_reader(a);
        return;
    }
    a->view = VIEW_READER;
    a->overlay = OV_NONE;
    a->dirty = 1;
    eg_log("reader: opened '%s' chapter=%d page=%d/%d\n", b->title, chapter,
           a->page + 1, a->chapter.page_count);
}

static void next_page(EgApp *a)
{
    if (!a->chapter_loaded) return;
    if (a->page + 1 < a->chapter.page_count) {
        a->page++;
        a->scroll = 0;
        update_percent(a);
        state_touch(a);
        a->dirty = 1;
        return;
    }
    if (a->epub_open && a->chapter.index + 1 < a->epub.spine_count) {
        if (load_chapter(a, a->chapter.index + 1, 0, NULL)) {
            state_touch(a);
            a->dirty = 1;
        }
        return;
    }
    toast(a, T(a, L_END_OF_BOOK));
}

static void prev_page(EgApp *a)
{
    if (!a->chapter_loaded) return;
    if (a->page > 0) {
        a->page--;
        a->scroll = 0;
        update_percent(a);
        state_touch(a);
        a->dirty = 1;
        return;
    }
    if (a->epub_open && a->chapter.index > 0) {
        int idx = a->chapter.index - 1;
        if (load_chapter(a, idx, 0, NULL)) {
            a->page = a->chapter.page_count > 0 ? a->chapter.page_count - 1 : 0;
            update_percent(a);
            state_touch(a);
            a->dirty = 1;
        }
        return;
    }
    toast(a, T(a, L_START_OF_BOOK));
}

static void next_chapter(EgApp *a)
{
    if (!a->epub_open) return;
    if (a->chapter.index + 1 < a->epub.spine_count) {
        if (load_chapter(a, a->chapter.index + 1, 0, NULL)) state_touch(a);
    } else toast(a, T(a, L_LAST_CHAPTER));
}

static void prev_chapter(EgApp *a)
{
    if (!a->epub_open) return;
    if (a->chapter.index > 0) {
        if (load_chapter(a, a->chapter.index - 1, 0, NULL)) state_touch(a);
    } else toast(a, T(a, L_FIRST_CHAPTER));
}

static void reflow_reader(EgApp *a)
{
    if (!a->chapter_loaded) return;
    int old_pages = a->chapter.page_count;
    int old_page = a->page;
    a->needs_reflow = 0;
    EgLayoutSettings set = current_layout_settings(a);
    int chapter = a->chapter.index;
    EgChapter fresh;
    a->g.dark_theme = app_theme(a)->dark;
    if (!eg_layout_chapter(&a->epub, chapter, &set, &fresh)) {
        toast(a, T(a, L_REFLOW_FAILED));
        return;
    }
    eg_chapter_free(&a->chapter);
    a->chapter = fresh;
    a->chapter_loaded = 1;
    a->layout_serial++;
    int np = 0;
    if (old_pages > 0)
        np = (old_page * fresh.page_count) / old_pages;
    a->page = eg_clampi(np, 0, fresh.page_count > 0 ? fresh.page_count - 1 : 0);
    a->scroll = 0;
    update_percent(a);
    a->dirty = 1;
    state_touch(a);
}

static void toggle_bookmark(EgApp *a)
{
    if (a->book_index < 0 || a->book_index >= a->book_count) return;
    EgBook *b = &a->books[a->book_index];
    int ch = a->chapter.index;
    for (int i = 0; i < b->bm_count; i++) {
        if (b->bm[i].chapter == ch && b->bm[i].page == a->page) {
            for (int j = i; j + 1 < b->bm_count; j++) b->bm[j] = b->bm[j + 1];
            b->bm_count--;
            toast(a, T(a, L_BOOKMARK_REMOVED));
            state_touch(a);
            return;
        }
    }
    if (b->bm_count >= MAX_BOOKMARKS) {
        toast(a, T(a, L_BOOKMARK_LIMIT));
        return;
    }
    b->bm[b->bm_count].chapter = ch;
    b->bm[b->bm_count].page = a->page;
    b->bm_count++;
    toast(a, T(a, L_BOOKMARK_ADDED));
    state_touch(a);
}

static uint16_t *page_cache_get(EgApp *a, int page, int slot)
{
    if (!a->chapter_loaded || page < 0 || page >= a->chapter.page_count || slot < 0 || slot > 1)
        return NULL;
    const AppTheme *th = app_theme(a);
    if (a->cache_valid[slot] &&
        a->cache_page[slot] == page &&
        a->cache_chapter[slot] == a->chapter.index &&
        a->cache_theme[slot] == a->theme &&
        a->cache_serial[slot] == a->layout_serial)
        return a->page_cache[slot];

    EgGfx cg;
    eg_gfx_init(&cg, a->page_cache[slot], EG_W, EG_H);
    cg.dark_theme = th->dark;
    eg_gfx_render_page(&cg, &a->chapter, page, th->paper, th->ink);
    a->cache_valid[slot] = 1;
    a->cache_page[slot] = page;
    a->cache_chapter[slot] = a->chapter.index;
    a->cache_theme[slot] = a->theme;
    a->cache_serial[slot] = a->layout_serial;
    return a->page_cache[slot];
}

static void draw_reader(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    a->g.dark_theme = th->dark;
    uint16_t *cur = page_cache_get(a, a->page, 0);
    if (!cur) {
        eg_gfx_clear(&a->g, th->paper);
        return;
    }

    uint16_t *next = NULL;
    if (a->scroll > 0 && a->page + 1 < a->chapter.page_count)
        next = page_cache_get(a, a->page + 1, 1);

    eg_gfx_clear(&a->g, th->paper);
    int split = READER_CONTENT_H - a->scroll;
    for (int r = 0; r < READER_CONTENT_H; r++) {
        uint16_t *dst = a->fb + (size_t)(READER_TOP + r) * EG_W;
        if (r < split) {
            int sy = READER_TOP + a->scroll + r;
            if (sy >= EG_H) sy = EG_H - 1;
            memcpy(dst, cur + (size_t)sy * EG_W, EG_W * sizeof(uint16_t));
        } else if (next) {
            int sy = READER_TOP + r - split;
            if (sy >= 0 && sy < EG_H)
                memcpy(dst, next + (size_t)sy * EG_W, EG_W * sizeof(uint16_t));
        }
    }

    if (a->scroll > 0)
        eg_gfx_fill(&a->g, 0, READER_TOP, EG_W, 2, th->paper);

    EgStyle head = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    char title[256];
    chapter_title_text(a, title, sizeof(title));
    draw_clipped(&a->g, 18, 14, 218, title, &head, th->dim);
    char pbuf[32];
    int pages = a->chapter.page_count > 0 ? a->chapter.page_count : 1;
    snprintf(pbuf, sizeof(pbuf), "%d/%d", a->page + 1, pages);
    draw_text_right(&a->g, EG_W - 18, 14, pbuf, &small, th->dim);
    eg_gfx_hline(&a->g, 18, 19, EG_W - 36, th->line);
    int pct = 0;
    if (a->book_index >= 0 && a->book_index < a->book_count)
        pct = a->books[a->book_index].percent;
    eg_gfx_fill(&a->g, 18, 236, EG_W - 36, 2, th->line);
    int w = (EG_W - 36) * pct / 1000;
    if (w > 0) eg_gfx_fill(&a->g, 18, 236, w, 2, th->accent);
}

/* --------------------------------------------------------------- overlays */

static const int menu_ids[] = {
    L_CONTENTS, L_BOOKMARKS, L_SETTINGS, L_BOOK_INFO, L_LIBRARY_MENU
};
#define MENU_COUNT 5

static void prepare_toc(EgApp *a)
{
    /* The raw TOC list is used directly; entries with a bad spine index are
     * still shown but are unavailable. */
    a->toc_sel = 0;
    a->toc_scroll = 0;
    if (!a->epub_open) return;
    int current = -1;
    for (int i = 0; i < a->epub.toc_count; i++) {
        if (a->epub.toc[i].chapter == a->chapter.index) { current = i; break; }
    }
    if (current >= 0) a->toc_sel = current;
    int rows = 8;
    if (a->toc_sel >= rows) a->toc_scroll = a->toc_sel - rows + 1;
}

static void toc_jump(EgApp *a)
{
    if (!a->epub_open || a->toc_sel < 0 || a->toc_sel >= a->epub.toc_count) return;
    EgTocEntry *e = &a->epub.toc[a->toc_sel];
    if (e->chapter < 0 || e->chapter >= a->epub.spine_count) {
        toast(a, T(a, L_CHAPTER_UNAVAILABLE));
        return;
    }
    if (load_chapter(a, e->chapter, 0, e->fragment)) {
        a->overlay = OV_NONE;
        state_touch(a);
        a->dirty = 1;
    }
}

static void open_toc_overlay(EgApp *a)
{
    clear_toast(a);
    a->overlay = OV_TOC;
    prepare_toc(a);
    a->dirty = 1;
}

static void settings_adjust(EgApp *a, int dir)
{
    if (a->set_sel == 0) {
        a->theme = (a->theme + (dir > 0 ? 1 : 2)) % 3;
    } else if (a->set_sel == 1) {
        static const int sizes[] = {12, 15, 19, 24};
        int idx = 1;
        for (int i = 0; i < 4; i++) if (sizes[i] == a->font_size) idx = i;
        idx = (idx + (dir > 0 ? 1 : 3)) % 4;
        a->font_size = sizes[idx];
        a->needs_reflow = 1;
    } else if (a->set_sel == 2) {
        a->justify = !a->justify;
        a->needs_reflow = 1;
    } else if (a->set_sel == 3) {
        static const int margins[] = {12, 18, 26};
        int idx = 1;
        for (int i = 0; i < 3; i++) if (margins[i] == a->margin) idx = i;
        idx = (idx + (dir > 0 ? 1 : 2)) % 3;
        a->margin = margins[idx];
        a->needs_reflow = 1;
    } else if (a->set_sel == 4) {
        a->language = (a->language + (dir > 0 ? 1 : LANG_COUNT - 1)) % LANG_COUNT;
    }
    a->dirty = 1;
}

static void close_overlay(EgApp *a)
{
    clear_toast(a);
    if (a->overlay == OV_SETTINGS) {
        state_touch(a);
        a->overlay = OV_NONE;
        if (a->needs_reflow) reflow_reader(a);
    } else {
        a->overlay = OV_NONE;
    }
    a->dirty = 1;
}

static void menu_action(EgApp *a)
{
    clear_toast(a);
    switch (a->menu_sel) {
        case 0: open_toc_overlay(a); break;
        case 1:
            a->overlay = OV_BOOKMARKS;
            a->bm_sel = 0;
            a->bm_scroll = 0;
            a->dirty = 1;
            break;
        case 2:
            open_settings_overlay(a);
            break;
        case 3:
            a->overlay = OV_INFO;
            a->info_scroll = 0;
            a->dirty = 1;
            break;
        case 4:
            close_reader(a);
            break;
        default: break;
    }
}

static void bookmark_jump(EgApp *a)
{
    if (a->book_index < 0 || a->book_index >= a->book_count) return;
    EgBook *b = &a->books[a->book_index];
    if (a->bm_sel < 0 || a->bm_sel >= b->bm_count) return;
    EgMark m = b->bm[a->bm_sel];
    if (load_chapter(a, m.chapter, m.page, NULL)) {
        a->overlay = OV_NONE;
        state_touch(a);
    }
}

static void bookmark_delete(EgApp *a)
{
    if (a->book_index < 0 || a->book_index >= a->book_count) return;
    EgBook *b = &a->books[a->book_index];
    if (a->bm_sel < 0 || a->bm_sel >= b->bm_count) return;
    for (int i = a->bm_sel; i + 1 < b->bm_count; i++) b->bm[i] = b->bm[i + 1];
    b->bm_count--;
    if (a->bm_sel >= b->bm_count) a->bm_sel = b->bm_count > 0 ? b->bm_count - 1 : 0;
    state_touch(a);
    toast(a, T(a, L_BOOKMARK_REMOVED));
    a->dirty = 1;
}

static const char *theme_name(EgApp *a, int t)
{
    static const int ids[3] = {L_THEME_PAPER, L_THEME_SEPIA, L_THEME_NIGHT};
    return T(a, ids[eg_clampi(t, 0, 2)]);
}

static const char *size_name(EgApp *a, int s)
{
    if (s == 12) return T(a, L_SIZE_SMALL);
    if (s == 19) return T(a, L_SIZE_LARGE);
    if (s == 24) return T(a, L_SIZE_XLARGE);
    return T(a, L_SIZE_MEDIUM);
}

static const char *margin_name(EgApp *a, int m)
{
    if (m == 12) return T(a, L_MARGIN_NARROW);
    if (m == 26) return T(a, L_MARGIN_WIDE);
    return T(a, L_MARGIN_MEDIUM);
}

static void draw_menu_overlay(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(15, 1, 0);
    EgStyle item = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_dim(&a->g);
    int px = 38, py = 30, pw = 244, ph = 182;
    draw_panel(&a->g, px, py, pw, ph, th);
    draw_text_center(&a->g, px + pw / 2, py + 22, T(a, L_MENU), &h, th->accent);
    eg_gfx_hline(&a->g, px + 16, py + 31, pw - 32, th->line);
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = py + 42 + i * 24;
        if (i == a->menu_sel) {
            eg_gfx_round_rect(&a->g, px + 12, y - 3, pw - 24, 21, 3, th->accent, 1, 0);
            eg_gfx_draw_text(&a->g, px + 22, y + 12, T(a, menu_ids[i]), &item, th->panel);
        } else {
            eg_gfx_draw_text(&a->g, px + 22, y + 12, T(a, menu_ids[i]), &item, th->panel_ink);
        }
    }
    draw_footer_hint(&a->g, px + 12, py + ph - 10, pw - 24, T(a, L_SELECT_CLOSE), th, &small);
}

static void draw_toc_overlay(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(15, 1, 0);
    EgStyle item = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_dim(&a->g);
    int total = a->epub.toc_count;
    int rows = total < 8 ? total : 8;
    if (rows < 1) rows = 1;
    int ph = 44 + rows * 18 + 18;
    int px = 10, py = (EG_H - ph) / 2, pw = 300;
    draw_panel(&a->g, px, py, pw, ph, th);
    draw_text_center(&a->g, px + pw / 2, py + 21, T(a, L_CONTENTS), &h, th->accent);
    eg_gfx_hline(&a->g, px + 14, py + 30, pw - 28, th->line);
    int max_scroll = total - rows;
    if (max_scroll < 0) max_scroll = 0;
    if (a->toc_scroll > max_scroll) a->toc_scroll = max_scroll;
    if (a->toc_scroll < 0) a->toc_scroll = 0;
    if (total == 0) {
        draw_text_center(&a->g, px + pw / 2, py + 90,
                         T(a, L_NO_TOC), &item, th->panel_dim);
    } else {
        for (int r = 0; r < rows; r++) {
            int idx = a->toc_scroll + r;
            if (idx >= total) break;
            EgTocEntry *e = &a->epub.toc[idx];
            int y = py + 40 + r * 18;
            int indent = px + 18 + (e->level > 0 ? (e->level > 2 ? 24 : e->level * 12) : 0);
            char tbuf[256];
            const char *ttl = toc_title_text(a, idx, tbuf, sizeof(tbuf));
            if (idx == a->toc_sel) {
                eg_gfx_fill(&a->g, px + 10, y - 2, pw - 20, 17, th->accent);
                draw_clipped(&a->g, indent, y + 11, pw - (indent - px) - 24,
                             ttl, &item, th->panel);
            } else {
                uint16_t col = (e->chapter < 0) ? th->panel_dim : th->panel_ink;
                if (e->chapter == a->chapter.index)
                    eg_gfx_fill(&a->g, px + 10, y + 2, 2, 13, th->accent);
                draw_clipped(&a->g, indent, y + 11, pw - (indent - px) - 24,
                             ttl, &item, col);
            }
        }
    }
    draw_footer_hint(&a->g, px + 12, py + ph - 10, pw - 24,
                     T(a, L_TOC_HINT), th, &small);
}

static void draw_settings_overlay(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(15, 1, 0);
    EgStyle item = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_dim(&a->g);
    int px = 14, py = 14, pw = 292, ph = 212;
    draw_panel(&a->g, px, py, pw, ph, th);
    draw_text_center(&a->g, px + pw / 2, py + 22, T(a, L_SETTINGS), &h, th->accent);
    eg_gfx_hline(&a->g, px + 16, py + 31, pw - 32, th->line);
    const char *labels[5] = {T(a, L_THEME), T(a, L_TEXT_SIZE), T(a, L_JUSTIFY),
                             T(a, L_MARGINS), T(a, L_LANGUAGE)};
    char values[5][48];
    snprintf(values[0], sizeof(values[0]), "%s", theme_name(a, a->theme));
    snprintf(values[1], sizeof(values[1]), "%s", size_name(a, a->font_size));
    snprintf(values[2], sizeof(values[2]), "%s", a->justify ? T(a, L_YES) : T(a, L_NO));
    snprintf(values[3], sizeof(values[3]), "%s", margin_name(a, a->margin));
    snprintf(values[4], sizeof(values[4]), "%s", eg_language_name(a->language));
    for (int i = 0; i < 5; i++) {
        int y = py + 38 + i * 30;
        if (i == a->set_sel) {
            eg_gfx_round_rect(&a->g, px + 10, y - 3, pw - 20, 25, 3, th->accent, 1, 0);
            eg_gfx_draw_text(&a->g, px + 20, y + 14, labels[i], &item, th->panel);
        } else {
            eg_gfx_draw_text(&a->g, px + 20, y + 14, labels[i], &item, th->panel_ink);
        }
        char val[64];
        snprintf(val, sizeof(val), "< %s >", values[i]);
        int vw = eg_gfx_text_width(val, &item);
        eg_gfx_draw_text(&a->g, px + pw - 22 - vw, y + 14, val, &item,
                         i == a->set_sel ? th->panel : th->panel_dim);
    }
    draw_footer_hint(&a->g, px + 12, py + ph - 10, pw - 24,
                     T(a, L_SETTINGS_HINT), th, &small);
}

static void draw_bookmarks_overlay(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(15, 1, 0);
    EgStyle item = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_dim(&a->g);
    EgBook *b = (a->book_index >= 0 && a->book_index < a->book_count) ?
                &a->books[a->book_index] : NULL;
    int bm_total = b ? b->bm_count : 0;
    int rows = bm_total < 7 ? bm_total : 7;
    if (rows < 1) rows = 1;
    int ph = 44 + rows * 19 + 18;
    int px = 14, py = (EG_H - ph) / 2, pw = 292;
    draw_panel(&a->g, px, py, pw, ph, th);
    draw_text_center(&a->g, px + pw / 2, py + 22, T(a, L_BOOKMARKS), &h, th->accent);
    eg_gfx_hline(&a->g, px + 14, py + 31, pw - 28, th->line);
    if (!b || b->bm_count == 0) {
        draw_text_center(&a->g, px + pw / 2, py + 55,
                         T(a, L_NO_BOOKMARKS), &item, th->panel_dim);
    } else {
        int max_scroll = b->bm_count - rows;
        if (max_scroll < 0) max_scroll = 0;
        if (a->bm_scroll > max_scroll) a->bm_scroll = max_scroll;
        if (a->bm_scroll < 0) a->bm_scroll = 0;
        if (a->bm_sel < 0) a->bm_sel = 0;
        if (a->bm_sel >= b->bm_count) a->bm_sel = b->bm_count - 1;
        if (a->bm_sel < a->bm_scroll) a->bm_scroll = a->bm_sel;
        if (a->bm_sel >= a->bm_scroll + rows) a->bm_scroll = a->bm_sel - rows + 1;
        for (int r = 0; r < rows; r++) {
            int idx = a->bm_scroll + r;
            if (idx >= b->bm_count) break;
            int y = py + 42 + r * 19;
            char line[128];
            snprintf(line, sizeof(line), T(a, L_BM_ROW_FMT),
                     b->bm[idx].chapter + 1, b->bm[idx].page + 1);
            if (idx == a->bm_sel) {
                eg_gfx_fill(&a->g, px + 10, y - 2, pw - 20, 18, th->accent);
                eg_gfx_draw_text(&a->g, px + 18, y + 12, line, &item, th->panel);
            } else {
                eg_gfx_draw_text(&a->g, px + 18, y + 12, line, &item, th->panel_ink);
            }
        }
    }
    draw_footer_hint(&a->g, px + 12, py + ph - 10, pw - 24,
                     T(a, L_BM_HINT), th, &small);
}

static void draw_info_overlay(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(15, 1, 0);
    EgStyle text = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_dim(&a->g);
    int px = 20, py = 22, pw = 280, ph = 196;
    draw_panel(&a->g, px, py, pw, ph, th);
    draw_text_center(&a->g, px + pw / 2, py + 22, T(a, L_BOOK_INFO), &h, th->accent);
    eg_gfx_hline(&a->g, px + 16, py + 31, pw - 32, th->line);
    EgBook *b = (a->book_index >= 0 && a->book_index < a->book_count) ?
                &a->books[a->book_index] : NULL;
    int y = py + 48;
    char buf[320];
    if (b) {
        draw_text_center(&a->g, px + pw / 2, y, b->title, &h, th->panel_ink);
        y += 22;
        draw_text_center(&a->g, px + pw / 2, y, display_author(a, b), &text, th->panel_dim);
        y += 24;
        snprintf(buf, sizeof(buf), T(a, L_FILE_FMT), b->filename);
        draw_clipped(&a->g, px + 18, y, pw - 36, buf, &text, th->panel_ink);
        y += 19;
        snprintf(buf, sizeof(buf), T(a, L_CHAPTERS_BM_FMT),
                 a->epub.spine_count, b->bm_count);
        draw_clipped(&a->g, px + 18, y, pw - 36, buf, &text, th->panel_ink);
        y += 19;
        snprintf(buf, sizeof(buf), T(a, L_PROGRESS_PAGE_FMT),
                 b->percent / 10, a->page + 1,
                 a->chapter.page_count > 0 ? a->chapter.page_count : 1);
        draw_clipped(&a->g, px + 18, y, pw - 36, buf, &text, th->panel_ink);
        y += 19;
        char chtitle[256];
        chapter_title_text(a, chtitle, sizeof(chtitle));
        snprintf(buf, sizeof(buf), T(a, L_CURRENT_CHAPTER_FMT), chtitle[0] ? chtitle : "-");
        draw_clipped(&a->g, px + 18, y, pw - 36, buf, &small, th->panel_dim);
    }
    snprintf(buf, sizeof(buf), T(a, L_INFO_FOOTER_FMT), APP_VERSION);
    draw_text_center(&a->g, px + pw / 2, py + ph - 10, buf, &small, th->panel_dim);
}

static void draw_help_overlay(EgApp *a)
{
    const AppTheme *th = app_theme(a);
    EgStyle h = style_make(15, 1, 0);
    EgStyle text = style_make(12, 0, 0);
    EgStyle small = style_make(12, 0, 0);
    draw_dim(&a->g);
    int px = 14, py = 18, pw = 292, ph = 204;
    draw_panel(&a->g, px, py, pw, ph, th);
    draw_text_center(&a->g, px + pw / 2, py + 22, T(a, L_HELP), &h, th->accent);
    eg_gfx_hline(&a->g, px + 14, py + 31, pw - 28, th->line);
    const int ids[] = {
        L_LIBRARY, L_HELP_LIB_NAV, L_HELP_LIB_KEYS, L_HELP_LIB_START,
        L_HELP_READER, L_HELP_READER_NAV, L_HELP_READER_KEYS,
        L_HELP_READER_KEYS2, L_HELP_READER_KEYS3
    };
    int y = py + 48;
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        const char *line = T(a, ids[i]);
        if (!line[0]) { y += 4; continue; }
        if (i == 0 || i == 4) draw_text_center(&a->g, px + pw / 2, y, line, &h, th->accent);
        else draw_text_center(&a->g, px + pw / 2, y, line, &text, th->panel_ink);
        y += 16;
    }
    draw_footer_hint(&a->g, px + 12, py + ph - 10, pw - 24,
                     T(a, L_HELP_CLOSE), th, &small);
}

static void draw_overlay(EgApp *a)
{
    switch (a->overlay) {
        case OV_MENU: draw_menu_overlay(a); break;
        case OV_TOC: draw_toc_overlay(a); break;
        case OV_SETTINGS: draw_settings_overlay(a); break;
        case OV_BOOKMARKS: draw_bookmarks_overlay(a); break;
        case OV_INFO: draw_info_overlay(a); break;
        case OV_HELP: draw_help_overlay(a); break;
        default: break;
    }
}

static void draw_toast(EgApp *a)
{
    if (a->toast_timer <= 0 || !a->toast[0]) return;
    const AppTheme *th = app_theme(a);
    EgStyle s = style_make(12, 0, 0);
    int tw = eg_gfx_text_width(a->toast, &s);
    int w = tw + 22;
    if (w > EG_W - 16) w = EG_W - 16;
    int x = (EG_W - w) / 2;
    int y = 190;
    eg_gfx_blend(&a->g, x + 2, y + 2, w, 24, 0x0000, 70);
    eg_gfx_round_rect(&a->g, x, y, w, 24, 4, th->panel, 1, th->accent);
    draw_clipped(&a->g, x + 11, y + 16, w - 22, a->toast, &s, th->panel_ink);
}

/* ------------------------------------------------------------------ input */

static uint16_t compute_pressed(EgApp *a, uint16_t pad)
{
    uint16_t pressed = pad & (uint16_t)~a->prev_pad;
    uint16_t dirs[4] = {EG_PAD_UP, EG_PAD_DOWN, EG_PAD_LEFT, EG_PAD_RIGHT};
    for (int i = 0; i < 4; i++) {
        if (pad & dirs[i]) {
            a->hold[i]++;
            if (a->hold[i] == 20 || (a->hold[i] > 20 && ((a->hold[i] - 20) % 6) == 0))
                pressed |= dirs[i];
        } else {
            a->hold[i] = 0;
        }
    }
    a->prev_pad = pad;
    return pressed;
}

static void reader_settings_input(EgApp *a, uint16_t pressed);

static void open_settings_overlay(EgApp *a)
{
    clear_toast(a);
    a->overlay = OV_SETTINGS;
    a->set_sel = 0;
    a->needs_reflow = 0;
    a->dirty = 1;
}

static void library_input(EgApp *a, uint16_t pressed)
{
    if (a->overlay == OV_SETTINGS) {
        reader_settings_input(a, pressed);
        return;
    }
    if (a->overlay == OV_HELP) {
        if (pressed & (EG_PAD_A | EG_PAD_B | EG_PAD_START)) close_overlay(a);
        return;
    }
    if (a->book_count > 0) {
        if (pressed & EG_PAD_LEFT) {
            if (a->selected > 0) a->selected--;
        } else if (pressed & EG_PAD_RIGHT) {
            if (a->selected + 1 < a->book_count) a->selected++;
        } else if (pressed & EG_PAD_UP) {
            if (a->selected >= BOOKS_PER_ROW) a->selected -= BOOKS_PER_ROW;
        } else if (pressed & EG_PAD_DOWN) {
            if (a->selected + BOOKS_PER_ROW < a->book_count) a->selected += BOOKS_PER_ROW;
        } else if (pressed & EG_PAD_L) {
            if (a->selected >= BOOKS_VISIBLE) a->selected -= BOOKS_VISIBLE;
        } else if (pressed & EG_PAD_R) {
            if (a->selected + BOOKS_VISIBLE < a->book_count) a->selected += BOOKS_VISIBLE;
        }
        a->shelf_page = a->selected / BOOKS_VISIBLE;
        if (pressed & (EG_PAD_LEFT | EG_PAD_RIGHT | EG_PAD_UP | EG_PAD_DOWN | EG_PAD_L | EG_PAD_R)) {
            a->marquee_frame = 0;
            a->dirty = 1;
        }
        if (pressed & EG_PAD_A) {
            open_book(a);
            return;
        }
        if (pressed & EG_PAD_Y) {
            a->sort_mode = (a->sort_mode + 1) % 2;
            sort_books(a);
            a->selected = 0;
            a->shelf_page = 0;
            a->marquee_frame = 0;
            state_touch(a);
            toast(a, T(a, a->sort_mode ? L_SORT_AUTHOR_MSG : L_SORT_TITLE_MSG));
        }
    }
    if (pressed & EG_PAD_START) {
        a->overlay = OV_HELP;
        a->dirty = 1;
    } else if (pressed & EG_PAD_SELECT) {
        open_settings_overlay(a);
    }
}

static void reader_menu_input(EgApp *a, uint16_t pressed)
{
    if (pressed & EG_PAD_UP) a->menu_sel = (a->menu_sel + MENU_COUNT - 1) % MENU_COUNT;
    else if (pressed & EG_PAD_DOWN) a->menu_sel = (a->menu_sel + 1) % MENU_COUNT;
    else if (pressed & EG_PAD_A) menu_action(a);
    else if (pressed & (EG_PAD_B | EG_PAD_START)) close_overlay(a);
    a->dirty = 1;
}

static void reader_toc_input(EgApp *a, uint16_t pressed)
{
    int total = a->epub.toc_count;
    int rows = 8;
    if (pressed & EG_PAD_UP) {
        if (a->toc_sel > 0) a->toc_sel--;
    } else if (pressed & EG_PAD_DOWN) {
        if (a->toc_sel + 1 < total) a->toc_sel++;
    } else if (pressed & EG_PAD_L) {
        a->toc_sel = eg_maxi(0, a->toc_sel - rows);
    } else if (pressed & EG_PAD_R) {
        a->toc_sel = eg_mini(total > 0 ? total - 1 : 0, a->toc_sel + rows);
    } else if (pressed & EG_PAD_A) {
        toc_jump(a);
        return;
    } else if (pressed & (EG_PAD_B | EG_PAD_START)) {
        close_overlay(a);
        return;
    }
    if (a->toc_sel < a->toc_scroll) a->toc_scroll = a->toc_sel;
    if (a->toc_sel >= a->toc_scroll + rows) a->toc_scroll = a->toc_sel - rows + 1;
    a->dirty = 1;
}

static void reader_settings_input(EgApp *a, uint16_t pressed)
{
    if (pressed & EG_PAD_UP) a->set_sel = (a->set_sel + 4) % 5;
    else if (pressed & EG_PAD_DOWN) a->set_sel = (a->set_sel + 1) % 5;
    else if (pressed & EG_PAD_LEFT) settings_adjust(a, -1);
    else if (pressed & EG_PAD_RIGHT) settings_adjust(a, 1);
    else if (pressed & (EG_PAD_A | EG_PAD_B | EG_PAD_START)) close_overlay(a);
    a->dirty = 1;
}

static void reader_bookmarks_input(EgApp *a, uint16_t pressed)
{
    EgBook *b = (a->book_index >= 0 && a->book_index < a->book_count) ?
                &a->books[a->book_index] : NULL;
    int total = b ? b->bm_count : 0;
    int rows = 7;
    if (pressed & EG_PAD_UP) {
        if (a->bm_sel > 0) a->bm_sel--;
    } else if (pressed & EG_PAD_DOWN) {
        if (a->bm_sel + 1 < total) a->bm_sel++;
    } else if (pressed & EG_PAD_A) {
        bookmark_jump(a);
        return;
    } else if (pressed & EG_PAD_X) {
        bookmark_delete(a);
    } else if (pressed & (EG_PAD_B | EG_PAD_START)) {
        close_overlay(a);
        return;
    }
    if (a->bm_sel < a->bm_scroll) a->bm_scroll = a->bm_sel;
    if (a->bm_sel >= a->bm_scroll + rows) a->bm_scroll = a->bm_sel - rows + 1;
    a->dirty = 1;
}

static void reader_scroll(EgApp *a, int dir)
{
    if (!a->chapter_loaded) return;
    if (dir > 0) {
        if (a->scroll + READER_SCROLL_STEP >= READER_CONTENT_H) {
            if (a->page + 1 < a->chapter.page_count) {
                a->page++;
                a->scroll = 0;
                update_percent(a);
                state_touch(a);
                a->dirty = 1;
            } else if (a->epub_open && a->chapter.index + 1 < a->epub.spine_count) {
                if (load_chapter(a, a->chapter.index + 1, 0, NULL)) {
                    a->scroll_cooldown = 10;
                    state_touch(a);
                    a->dirty = 1;
                }
            } else {
                a->scroll = READER_CONTENT_H - READER_SCROLL_STEP;
                a->scroll_stopped = 1;
                toast(a, T(a, L_END_OF_BOOK));
            }
        } else {
            a->scroll += READER_SCROLL_STEP;
            a->dirty = 1;
        }
        return;
    }

    if (a->scroll > 0) {
        a->scroll -= READER_SCROLL_STEP;
        if (a->scroll < 0) a->scroll = 0;
        a->dirty = 1;
        return;
    }
    if (a->page > 0) {
        a->page--;
        a->scroll = READER_CONTENT_H - READER_SCROLL_STEP;
        update_percent(a);
        state_touch(a);
        a->dirty = 1;
        return;
    }
    if (a->epub_open && a->chapter.index > 0) {
        int prev = a->chapter.index - 1;
        if (load_chapter(a, prev, 0, NULL)) {
            a->page = a->chapter.page_count > 0 ? a->chapter.page_count - 1 : 0;
            a->scroll = 0;
            a->scroll_cooldown = 10;
            update_percent(a);
            state_touch(a);
            a->dirty = 1;
        }
        return;
    }
    a->scroll_stopped = -1;
    toast(a, T(a, L_START_OF_BOOK));
}

static void reader_scroll_held(EgApp *a, uint16_t pad)
{
    if (a->scroll_cooldown > 0) {
        a->scroll_cooldown--;
        return;
    }
    int up = (pad & EG_PAD_UP) != 0;
    int down = (pad & EG_PAD_DOWN) != 0;
    if (!up && !down) {
        a->scroll_stopped = 0;
        return;
    }
    if (up && down) return;
    int dir = down ? 1 : -1;
    if (a->scroll_stopped == dir) return;
    reader_scroll(a, dir);
}

static void reader_input(EgApp *a, uint16_t pressed)
{
    if (a->overlay == OV_MENU) { reader_menu_input(a, pressed); return; }
    if (a->overlay == OV_TOC) { reader_toc_input(a, pressed); return; }
    if (a->overlay == OV_SETTINGS) { reader_settings_input(a, pressed); return; }
    if (a->overlay == OV_BOOKMARKS) { reader_bookmarks_input(a, pressed); return; }
    if (a->overlay == OV_INFO || a->overlay == OV_HELP) {
        if (pressed & (EG_PAD_A | EG_PAD_B | EG_PAD_START)) close_overlay(a);
        return;
    }
    if (pressed & EG_PAD_B) { close_reader(a); return; }
    if (pressed & EG_PAD_A) { a->overlay = OV_MENU; a->menu_sel = 0; a->dirty = 1; return; }
    if (pressed & EG_PAD_Y) { open_toc_overlay(a); return; }
    if (pressed & EG_PAD_X) { toggle_bookmark(a); return; }
    if (pressed & EG_PAD_START) {
        open_settings_overlay(a);
        return;
    }
    if (pressed & EG_PAD_SELECT) {
        a->theme = (a->theme + 1) % 3;
        a->g.dark_theme = app_theme(a)->dark;
        state_touch(a);
        char msg[96];
        snprintf(msg, sizeof(msg), T(a, L_THEME_FMT), theme_name(a, a->theme));
        toast(a, msg);
        a->dirty = 1;
        return;
    }
    if (pressed & EG_PAD_L) { prev_chapter(a); return; }
    if (pressed & EG_PAD_R) { next_chapter(a); return; }
    if (pressed & EG_PAD_RIGHT) { next_page(a); return; }
    if (pressed & EG_PAD_LEFT) { prev_page(a); return; }
}

/* --------------------------------------------------------------- lifecycle */

static void update_status(EgApp *a)
{
    const char *view = a->view == VIEW_READER ? "reader" :
                       a->view == VIEW_LIBRARY ? "library" : "scan";
    snprintf(a->status, sizeof(a->status),
             "view=%s books=%d sel=%d chapter=%d page=%d scroll=%d overlay=%d scan=%d/%d",
             view, a->book_count, a->selected,
             a->chapter_loaded ? a->chapter.index : -1, a->page, a->scroll,
             a->overlay, a->file_index, a->file_count);
}

EgApp *eg_app_create(const char *content_path, const char *save_dir)
{
    EgApp *a = eg_calloc(1, sizeof(EgApp));
    if (!a) return NULL;
    eg_gfx_init(&a->g, a->fb, EG_W, EG_H);
    a->book_index = -1;
    a->selected = 0;
    a->overlay = OV_NONE;
    a->dirty = 1;

    char base[EG_MAX_PATH];
    if (content_path && *content_path) {
        if (eg_path_is_dir(content_path)) {
            snprintf(base, sizeof(base), "%s", content_path);
        } else {
            eg_path_dirname(content_path, base, sizeof(base));
        }
    } else {
        snprintf(base, sizeof(base), ".");
    }
    snprintf(a->base_dir, sizeof(a->base_dir), "%s", base);
    char books_sub[EG_MAX_PATH];
    eg_path_join(books_sub, sizeof(books_sub), base, "books");
    if (eg_path_is_dir(books_sub)) {
        snprintf(a->books_dir, sizeof(a->books_dir), "%s", books_sub);
    } else {
        char sibling[EG_MAX_PATH];
        eg_path_join(sibling, sizeof(sibling), base, "epub/books");
        if (eg_path_is_dir(sibling))
            snprintf(a->books_dir, sizeof(a->books_dir), "%s", sibling);
#ifdef SF2000
        else if (eg_path_is_dir("/mnt/sda1/ROMS/epub/books"))
            snprintf(a->books_dir, sizeof(a->books_dir), "/mnt/sda1/ROMS/epub/books");
#endif
        else
            snprintf(a->books_dir, sizeof(a->books_dir), "%s", base);
    }

    if (save_dir && *save_dir) snprintf(a->save_dir, sizeof(a->save_dir), "%s", save_dir);
    else eg_path_join(a->save_dir, sizeof(a->save_dir), base, ".state");
    eg_mkdir_p(a->save_dir);
    eg_path_join(a->state_path, sizeof(a->state_path), a->save_dir, "reader.cfg");
    eg_set_log_dir(a->save_dir);
    eg_log("app: base=%s books=%s save=%s\n", a->base_dir, a->books_dir, a->save_dir);

    state_load(a);
    list_epubs(a);
    a->scanning = a->file_count > 0;
    a->scan_state = a->scanning ? SCAN_INIT : SCAN_DONE;
    a->view = a->scanning ? VIEW_SCAN : VIEW_LIBRARY;
    if (!a->scanning) eg_log("library: no EPUB files in %s\n", a->books_dir);
    update_status(a);
    return a;
}

void eg_app_destroy(EgApp *a)
{
    if (!a) return;
    eg_app_save(a);
    if (a->epub_open) {
        eg_epub_close(&a->epub);
        a->epub_open = 0;
    }
    if (a->chapter_loaded) {
        eg_chapter_free(&a->chapter);
        a->chapter_loaded = 0;
    }
    books_free(a);
    free_files(a);
    free(a);
}

uint16_t *eg_app_framebuffer(EgApp *a)
{
    return a ? a->fb : NULL;
}

const char *eg_app_status(EgApp *a)
{
    if (!a) return "";
    update_status(a);
    return a->status;
}

void eg_app_run(EgApp *a, uint16_t pad)
{
    if (!a) return;
    a->frame++;
    if (a->scanning) {
        scan_step(a);
        a->prev_pad = pad;
        a->dirty = 1;
    } else {
        uint16_t pressed = compute_pressed(a, pad);
        if (pressed) {
            if (a->view == VIEW_LIBRARY) library_input(a, pressed);
            else if (a->view == VIEW_READER) reader_input(a, pressed);
            update_status(a);
        }
        if (a->view == VIEW_READER && a->overlay == OV_NONE)
            reader_scroll_held(a, pad);
    }
    if (a->toast_timer > 0) {
        a->toast_timer--;
        if (a->toast_timer == 0) a->dirty = 1;
    }
    if (a->state_dirty) {
        if (a->save_timer > 0) a->save_timer--;
        if (a->save_timer <= 0) eg_app_save(a);
    }
    if (!a->dirty && !a->scanning && a->view == VIEW_LIBRARY &&
        a->overlay == OV_NONE && a->toast_timer == 0) {
        if (selected_title_overflow(a) > 0) {
            a->marquee_frame++;
            redraw_selected_title(a);
        }
    }
    if (!a->dirty) return;
    if (a->view == VIEW_SCAN) {
        draw_scan(a);
    } else if (a->view == VIEW_LIBRARY) {
        draw_library(a);
    } else {
        draw_reader(a);
    }
    if (a->overlay != OV_NONE) draw_overlay(a);
    draw_toast(a);
    a->dirty = 0;
    update_status(a);
}
