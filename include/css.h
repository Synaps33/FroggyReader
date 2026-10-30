#ifndef EG_CSS_H
#define EG_CSS_H

#include "xml.h"

enum {
    EG_DISPLAY_INLINE = 0,
    EG_DISPLAY_BLOCK,
    EG_DISPLAY_LIST_ITEM,
    EG_DISPLAY_TABLE,
    EG_DISPLAY_TABLE_ROW,
    EG_DISPLAY_TABLE_CELL,
    EG_DISPLAY_NONE
};

enum {
    EG_ALIGN_LEFT = 0,
    EG_ALIGN_CENTER,
    EG_ALIGN_RIGHT,
    EG_ALIGN_JUSTIFY
};

enum {
    EG_WS_NORMAL = 0,
    EG_WS_PRE,
    EG_WS_NOWRAP
};

enum {
    EG_LIST_NONE = 0,
    EG_LIST_DISC,
    EG_LIST_CIRCLE,
    EG_LIST_SQUARE,
    EG_LIST_DECIMAL,
    EG_LIST_LOWER_ALPHA,
    EG_LIST_UPPER_ALPHA,
    EG_LIST_LOWER_ROMAN,
    EG_LIST_UPPER_ROMAN
};

typedef struct {
    int font_size;
    int bold;
    int italic;
    int sans;
    int color_set;
    uint16_t color;
    int has_bg;
    uint16_t bg;
    int align;
    int text_indent;
    int margin_t, margin_r, margin_b, margin_l;
    int padding_t, padding_r, padding_b, padding_l;
    int border_t, border_r, border_b, border_l;
    uint16_t border_color;
    int line_height;
    int line_height_normal;
    int display;
    int page_break_before;
    int page_break_after;
    int avoid_break;
    int keep_with_next;
    int white_space;
    int text_transform;
    int letter_spacing;
    int word_spacing;
    int underline;
    int strike;
    int vertical_align;       /* -1 sub, +1 sup */
    int list_style;
    int width_px, width_pct;
    int height_px, height_pct;
    int max_width_px, max_width_pct;
    int visible;
    int italic_synth_ok;
} EgStyle;

typedef struct EgCssDecl {
    char *prop;
    char *value;
    int important;
    int order;
} EgCssDecl;

typedef struct {
    int count;
    int specificity;
    int valid;
    char *tag;
    char *id;
    char *classes[4];
    int class_count;
    char *attr_name;
    char *attr_value;
    int attr_contains;
    int pseudo_first;
    int pseudo_last;
    int pseudo_only;
    int nth_child;             /* 0 = none */
    int comb;                  /* ' ' descendant, '>' child */
} EgCssPart;

typedef struct {
    EgCssPart parts[8];
    int count;
    int specificity;
    int valid;
} EgCssSelector;

typedef struct {
    EgCssSelector selector;
    EgCssDecl *decls;
    int decl_count;
} EgCssRule;

typedef struct {
    EgCssRule *rules;
    int rule_count;
    int rule_cap;
} EgStylesheet;

void eg_css_init(EgStylesheet *sheet);
void eg_css_free(EgStylesheet *sheet);
int eg_css_parse(EgStylesheet *sheet, const char *css, int len);

void eg_style_default(EgStyle *s);
void eg_style_child(const EgXmlNode *node, const EgStyle *parent, EgStyle *out);
void eg_css_compute(const EgStylesheet *sheet, const EgXmlNode *node,
                    const EgStyle *parent, EgStyle *out, int root_font_size);
void eg_css_apply_inline(EgStyle *style, const char *decls);
int eg_css_nearest_font_size(int px);
int eg_css_parse_length(const char *value, int font_size, int *out);

#endif
