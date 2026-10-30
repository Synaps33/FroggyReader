#ifndef EG_LAYOUT_H
#define EG_LAYOUT_H

#include "epub.h"
#include "css.h"
#include "image.h"

enum {
    EG_CMD_TEXT = 1,
    EG_CMD_IMAGE,
    EG_CMD_RECT,
    EG_CMD_LINE
};

typedef struct {
    int type;
    int x, y;
    int w, h;
    uint16_t color;
    EgStyle style;
    char *text;
    EgImage *image;
} EgCmd;

typedef struct {
    EgCmd *cmds;
    int count, cap;
} EgPage;

typedef struct {
    char *name;
    int page;
    int y;
} EgAnchor;

typedef struct {
    char *href;
    EgImage image;
} EgChapterImage;

typedef struct {
    int index;
    char *title;
    int is_cover;
    int auto_title;
    EgPage *pages;
    int page_count, cap;
    EgAnchor *anchors;
    int anchor_count, anchor_cap;
    EgChapterImage *images;
    int image_count, image_cap;
} EgChapter;

typedef struct {
    int font_size;
    int justify;
    int margin_lr;
    int content_top;
    int content_bottom;
} EgLayoutSettings;

int eg_layout_chapter(EgEpub *ep, int chapter_index,
                      const EgLayoutSettings *settings, EgChapter *out);
void eg_chapter_free(EgChapter *ch);
int eg_chapter_find_anchor(const EgChapter *ch, const char *fragment);
int eg_chapter_page_count(const EgChapter *ch);
int eg_chapter_cmd_count(const EgChapter *ch);

#endif
