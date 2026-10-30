#include "css.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------------------------------------------------------- helpers */

static int is_alpha(int c) { return isalpha((unsigned char)c) || c == '-' || c == '_'; }
static int is_ident(int c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; }

static char *dupn(const char *s, int n)
{
    char *p = eg_malloc((size_t)n + 1);
    memcpy(p, s, (size_t)n);
    p[n] = 0;
    return p;
}

static void skip_ws(const char *s, int len, int *i)
{
    while (*i < len && isspace((unsigned char)s[*i])) (*i)++;
}

static int find_char(const char *s, int len, int start, char ch)
{
    int bracket = 0, paren = 0;
    char quote = 0;
    for (int i = start; i < len; i++) {
        char c = s[i];
        if (quote) {
            if (c == '\\') i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '[') bracket++;
        else if (c == ']') bracket--;
        else if (c == '(') paren++;
        else if (c == ')') paren--;
        else if (c == ch && bracket <= 0 && paren <= 0) return i;
    }
    return -1;
}

static int find_matching_brace(const char *s, int len, int open)
{
    int depth = 0;
    char quote = 0;
    for (int i = open; i < len; i++) {
        char c = s[i];
        if (quote) {
            if (c == '\\') i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            if (--depth == 0) return i;
        }
    }
    return -1;
}

static char *strip_comments(const char *src, int len)
{
    char *out = eg_malloc((size_t)len + 1);
    int w = 0;
    for (int i = 0; i < len; i++) {
        if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) i++;
            i++;
            continue;
        }
        out[w++] = src[i];
    }
    out[w] = 0;
    return out;
}

/* -------------------------------------------------------------- selectors */

static void selector_free(EgCssSelector *sel)
{
    for (int i = 0; i < sel->count; i++) {
        EgCssPart *p = &sel->parts[i];
        free(p->tag);
        free(p->id);
        for (int c = 0; c < p->class_count; c++) free(p->classes[c]);
        free(p->attr_name);
        free(p->attr_value);
    }
    memset(sel, 0, sizeof(*sel));
}

static int parse_attr_selector(const char *s, int len, int *i, EgCssPart *part)
{
    int start = *i;
    while (*i < len && s[*i] != ']' && s[*i] != '=' && s[*i] != '~' && s[*i] != '|') (*i)++;
    if (*i >= len) return 0;
    int n = *i - start;
    while (n > 0 && isspace((unsigned char)s[start + n - 1])) n--;
    if (n <= 0) return 0;
    char *name = dupn(s + start, n);
    char *colon = strrchr(name, ':');
    if (colon) memmove(name, colon + 1, strlen(colon + 1) + 1);
    for (char *q = name; *q; q++) *q = (char)tolower((unsigned char)*q);
    part->attr_name = name;
    if (*i < len && s[*i] == ']') { (*i)++; return 1; }
    int contains = 0;
    if (s[*i] == '~' && *i + 1 < len && s[*i + 1] == '=') { contains = 1; *i += 2; }
    else if (s[*i] == '=') { *i += 1; }
    else if ((s[*i] == '|' || s[*i] == '^' || s[*i] == '$' || s[*i] == '*') && *i + 1 < len && s[*i + 1] == '=') {
        /* These variants are rare in books; approximate with contains/exact. */
        contains = (s[*i] != '|');
        *i += 2;
    } else {
        while (*i < len && s[*i] != ']') (*i)++;
        if (*i < len) (*i)++;
        return 1;
    }
    skip_ws(s, len, i);
    char quote = 0;
    if (*i < len && (s[*i] == '"' || s[*i] == '\'')) quote = s[(*i)++];
    int vstart = *i;
    while (*i < len && ((quote && s[*i] != quote) || (!quote && s[*i] != ']'))) (*i)++;
    int vn = *i - vstart;
    if (quote && *i < len) (*i)++;
    if (!quote) while (vn > 0 && isspace((unsigned char)s[vstart + vn - 1])) vn--;
    part->attr_value = dupn(s + vstart, vn);
    part->attr_contains = contains;
    while (*i < len && s[*i] != ']') (*i)++;
    if (*i < len) (*i)++;
    return 1;
}

static int parse_selector(const char *s, int len, EgCssSelector *out)
{
    memset(out, 0, sizeof(*out));
    out->valid = 1;
    int i = 0;
    int pending_comb = ' ';
    while (i < len) {
        int had_space = 0;
        while (i < len && isspace((unsigned char)s[i])) { had_space = 1; i++; }
        if (i >= len) break;
        if (s[i] == '>') { pending_comb = '>'; i++; skip_ws(s, len, &i); }
        else if (had_space && out->count > 0) pending_comb = ' ';
        if (s[i] == '+' || s[i] == '~') { out->valid = 0; return 0; }
        if (out->count >= 8) { out->valid = 0; return 0; }
        EgCssPart *p = &out->parts[out->count];
        memset(p, 0, sizeof(*p));
        p->comb = pending_comb;
        pending_comb = ' ';
        /* compound selector end */
        int j = i;
        int bracket = 0, paren = 0;
        while (j < len) {
            char c = s[j];
            if (c == '[') bracket++;
            else if (c == ']') bracket--;
            else if (c == '(') paren++;
            else if (c == ')') paren--;
            if (bracket <= 0 && paren <= 0 && (isspace((unsigned char)c) || c == '>')) break;
            j++;
        }
        int end = j;
        int k = i;
        if (k < end && s[k] == '*') { k++; }
        else if (k < end && is_alpha(s[k])) {
            int st = k;
            while (k < end && is_ident(s[k])) k++;
            char *tag = dupn(s + st, k - st);
            char *colon = strrchr(tag, ':');
            if (colon) memmove(tag, colon + 1, strlen(colon + 1) + 1);
            for (char *q = tag; *q; q++) *q = (char)tolower((unsigned char)*q);
            p->tag = tag;
        }
        while (k < end) {
            if (s[k] == '.') {
                int st = ++k;
                while (k < end && is_ident(s[k])) k++;
                char *cls = dupn(s + st, k - st);
                if (p->class_count < 4) p->classes[p->class_count++] = cls;
                else free(cls);
            } else if (s[k] == '#') {
                int st = ++k;
                while (k < end && is_ident(s[k])) k++;
                free(p->id);
                p->id = dupn(s + st, k - st);
            } else if (s[k] == '[') {
                k++;
                if (!parse_attr_selector(s, end, &k, p)) out->valid = 0;
            } else if (s[k] == ':') {
                k++;
                if (k < end && s[k] == ':') { out->valid = 0; while (k < end && is_ident(s[k])) k++; }
                else {
                    int st = k;
                    while (k < end && is_ident(s[k])) k++;
                    char name[32];
                    int n = k - st;
                    if (n > 31) n = 31;
                    memcpy(name, s + st, (size_t)n);
                    name[n] = 0;
                    if (strcmp(name, "first-child") == 0) p->pseudo_first = 1;
                    else if (strcmp(name, "last-child") == 0) p->pseudo_last = 1;
                    else if (strcmp(name, "only-child") == 0) p->pseudo_only = 1;
                    else if (strcmp(name, "nth-child") == 0) {
                        if (k < end && s[k] == '(') {
                            int cs = ++k;
                            while (k < end && s[k] != ')') k++;
                            char *arg = dupn(s + cs, k - cs);
                            char *a = eg_trim(arg);
                            if (strcmp(a, "odd") == 0) p->nth_child = -1;
                            else if (strcmp(a, "even") == 0) p->nth_child = -2;
                            else p->nth_child = atoi(a);
                            free(arg);
                            if (k < end) k++;
                        }
                    } else if (strcmp(name, "link") == 0 || strcmp(name, "visited") == 0 ||
                               strcmp(name, "hover") == 0 || strcmp(name, "active") == 0 ||
                               strcmp(name, "focus") == 0) {
                        /* Ignore UI pseudo-classes: the rule still largely applies. */
                    } else {
                        out->valid = 0;
                    }
                }
            } else {
                k++;
            }
        }
        int spec = (p->id ? 100 : 0) + p->class_count * 10 +
                   ((p->attr_name ? 1 : 0) + (p->pseudo_first || p->pseudo_last || p->pseudo_only || p->nth_child)) * 10 +
                   (p->tag ? 1 : 0);
        out->specificity += spec;
        out->count++;
        i = end;
    }
    if (out->count == 0) out->valid = 0;
    return out->valid;
}

static int node_is_element(const EgXmlNode *n)
{
    return n && n->tag;
}

static int class_contains(const EgXmlNode *node, const char *cls)
{
    const char *classes = eg_xml_attr(node, "class");
    if (!classes) return 0;
    const char *p = classes;
    size_t n = strlen(cls);
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *e = p;
        while (*e && !isspace((unsigned char)*e)) e++;
        if ((size_t)(e - p) == n && memcmp(p, cls, n) == 0) return 1;
        p = e;
    }
    return 0;
}

static int element_index(const EgXmlNode *node)
{
    if (!node || !node->parent) return 0;
    int idx = 0;
    for (const EgXmlNode *c = node->parent->children; c; c = c->next) {
        if (c->tag) idx++;
        if (c == node) return idx;
    }
    return idx;
}

static int element_sibling_count(const EgXmlNode *node)
{
    int n = 0;
    if (!node || !node->parent) return 0;
    for (const EgXmlNode *c = node->parent->children; c; c = c->next)
        if (c->tag) n++;
    return n;
}

static int simple_matches(const EgCssPart *p, const EgXmlNode *node)
{
    if (!node_is_element(node)) return 0;
    if (p->tag && strcmp(p->tag, node->tag) != 0) return 0;
    if (p->id) {
        const char *id = eg_xml_attr(node, "id");
        if (!id || strcmp(id, p->id) != 0) return 0;
    }
    for (int i = 0; i < p->class_count; i++)
        if (!class_contains(node, p->classes[i])) return 0;
    if (p->attr_name) {
        const char *v = eg_xml_attr(node, p->attr_name);
        if (!v) return 0;
        if (p->attr_value) {
            if (p->attr_contains) {
                if (!class_contains(node, p->attr_value) &&
                    strstr(v, p->attr_value) == NULL) return 0;
            } else if (strcmp(v, p->attr_value) != 0) return 0;
        }
    }
    int idx = element_index(node);
    int cnt = element_sibling_count(node);
    if (p->pseudo_first && idx != 1) return 0;
    if (p->pseudo_last && idx != cnt) return 0;
    if (p->pseudo_only && cnt != 1) return 0;
    if (p->nth_child) {
        if (p->nth_child == -1) { if ((idx & 1) == 0) return 0; }
        else if (p->nth_child == -2) { if (idx & 1) return 0; }
        else if (idx != p->nth_child) return 0;
    }
    return 1;
}

static int selector_matches(const EgCssSelector *sel, const EgXmlNode *node)
{
    if (!sel->valid || !node_is_element(node)) return 0;
    int si = sel->count - 1;
    if (!simple_matches(&sel->parts[si], node)) return 0;
    const EgXmlNode *cur = node;
    for (int i = si - 1; i >= 0; i--) {
        int comb = sel->parts[i + 1].comb;
        if (comb == '>') {
            cur = cur->parent;
            if (!simple_matches(&sel->parts[i], cur)) return 0;
        } else {
            cur = cur->parent;
            while (cur && !simple_matches(&sel->parts[i], cur)) cur = cur->parent;
            if (!cur) return 0;
        }
    }
    return 1;
}

/* --------------------------------------------------------------- values */

static int parse_num100(const char *s, const char **end)
{
    int sign = 1;
    int whole = 0, frac = 0, frac_digits = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) { whole = whole * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s) && frac_digits < 2) {
            frac = frac * 10 + (*s - '0');
            frac_digits++;
            s++;
        }
        while (isdigit((unsigned char)*s)) s++;
    }
    while (frac_digits < 2) { frac *= 10; frac_digits++; }
    if (end) *end = s;
    return sign * (whole * 100 + frac);
}

static int parse_int(const char *s, const char **end)
{
    int v = parse_num100(s, end);
    return v >= 0 ? (v + 50) / 100 : -((-v + 50) / 100);
}

static char *next_token(const char **sp)
{
    const char *s = *sp;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) { *sp = s; return NULL; }
    int bracket = 0, paren = 0;
    char quote = 0;
    const char *start = s;
    while (*s) {
        char c = *s;
        if (quote) {
            if (c == '\\' && s[1]) s++;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') quote = c;
        else if (c == '(') paren++;
        else if (c == ')') paren--;
        else if (c == ',') { }
        else if (c == '[') bracket++;
        else if (c == ']') bracket--;
        else if (isspace((unsigned char)c) && bracket <= 0 && paren <= 0) break;
        s++;
    }
    *sp = s;
    return dupn(start, (int)(s - start));
}

static int is_color_token(const char *t)
{
    if (!t) return 0;
    if (t[0] == '#') return 1;
    if (eg_stricmp(t, "transparent") == 0) return 1;
    if (strncmp(t, "rgb(", 4) == 0 || strncmp(t, "rgba(", 5) == 0) return 1;
    return 0;
}

static uint16_t parse_color(const char *value)
{
    if (!value) return 0;
    while (isspace((unsigned char)*value)) value++;
    if (*value == '#') {
        value++;
        int n = (int)strlen(value);
        unsigned r = 0, g = 0, b = 0;
        if (n >= 6) {
            r = (unsigned)strtol((char[]){value[0], value[1], 0}, NULL, 16);
            g = (unsigned)strtol((char[]){value[2], value[3], 0}, NULL, 16);
            b = (unsigned)strtol((char[]){value[4], value[5], 0}, NULL, 16);
        } else if (n >= 3) {
            r = (unsigned)strtol((char[]){value[0], value[0], 0}, NULL, 16);
            g = (unsigned)strtol((char[]){value[1], value[1], 0}, NULL, 16);
            b = (unsigned)strtol((char[]){value[2], value[2], 0}, NULL, 16);
        }
        return eg_rgb565((int)r, (int)g, (int)b);
    }
    if (strncmp(value, "rgb(", 4) == 0 || strncmp(value, "rgba(", 5) == 0) {
        const char *p = strchr(value, '(');
        if (!p) return 0;
        p++;
        int c[3] = {0, 0, 0};
        for (int i = 0; i < 3 && *p; i++) {
            while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
            c[i] = parse_int(p, &p);
            while (*p && *p != ',' && *p != ')') p++;
        }
        return eg_rgb565(c[0], c[1], c[2]);
    }
    struct { const char *n; int r, g, b; } tab[] = {
        {"black",0,0,0}, {"white",255,255,255}, {"red",255,0,0},
        {"lime",0,255,0}, {"green",0,128,0}, {"blue",0,0,255},
        {"yellow",255,255,0}, {"orange",255,165,0}, {"purple",128,0,128},
        {"gray",128,128,128}, {"grey",128,128,128}, {"silver",192,192,192},
        {"maroon",128,0,0}, {"olive",128,128,0}, {"navy",0,0,128},
        {"teal",0,128,128}, {"aqua",0,255,255}, {"cyan",0,255,255},
        {"fuchsia",255,0,255}, {"magenta",255,0,255}, {"brown",165,42,42},
        {"pink",255,192,203}, {"gold",255,215,0}, {"beige",245,245,220},
        {"ivory",255,255,240}, {"khaki",240,230,140}, {"crimson",220,20,60},
        {"indigo",75,0,130}, {"violet",238,130,238}, {"salmon",250,128,114},
        {"tan",210,180,140}, {"wheat",245,222,179}, {"seashell",255,245,238},
        {"linen",250,240,230}, {"whitesmoke",245,245,245}, {"lightgray",211,211,211},
        {"lightgrey",211,211,211}, {"darkgray",169,169,169}, {"darkgrey",169,169,169},
        {NULL,0,0,0}
    };
    char name[32];
    int n = 0;
    while (value[n] && isalpha((unsigned char)value[n]) && n < 31) { name[n] = (char)tolower((unsigned char)value[n]); n++; }
    name[n] = 0;
    for (int i = 0; tab[i].n; i++)
        if (strcmp(name, tab[i].n) == 0) return eg_rgb565(tab[i].r, tab[i].g, tab[i].b);
    return 0;
}

static int parse_length(const char *value, int font_size, int *out, int *pct_out)
{
    if (!value) return 0;
    while (isspace((unsigned char)*value)) value++;
    if (eg_stricmp(value, "auto") == 0) { *out = -1; if (pct_out) *pct_out = -1; return 1; }
    const char *end = value;
    int v100 = parse_num100(value, &end);
    int sign = v100 < 0 ? -1 : 1;
    if (sign < 0) v100 = -v100;
    while (*end && isspace((unsigned char)*end)) end++;
    int v;
    int pct = 0;
    if (!*end) { v = (v100 + 50) / 100; if (pct_out) *pct_out = -1; *out = v * sign; return 1; }
    if (*end == '%') { pct = (v100 + 50) / 100; v = font_size * v100 / 10000; }
    else if (strncmp(end, "px", 2) == 0) v = (v100 + 50) / 100;
    else if (strncmp(end, "pt", 2) == 0) v = (v100 * 4 + 150) / 300;
    else if (strncmp(end, "pc", 2) == 0) v = v100 * 16 / 100;
    else if (strncmp(end, "em", 2) == 0) v = font_size * v100 / 100;
    else if (strncmp(end, "rem", 3) == 0) v = font_size * v100 / 100;
    else if (strncmp(end, "mm", 2) == 0) v = (v100 * 378 + 5000) / 10000;
    else if (strncmp(end, "cm", 2) == 0) v = (v100 * 378 + 500) / 1000;
    else if (*end == 'e' && end[1] == 'x') v = font_size * v100 / 200;
    else if (*end == 'v' && end[1] == 'w') v = EG_W * v100 / 10000;
    else if (*end == 'v' && end[1] == 'h') v = EG_H * v100 / 10000;
    else v = (v100 + 50) / 100;
    *out = v * sign;
    if (pct_out) *pct_out = pct * sign;
    return 1;
}

int eg_css_parse_length(const char *value, int font_size, int *out)
{
    int pct = 0;
    return parse_length(value, font_size, out, &pct);
}

int eg_css_nearest_font_size(int px)
{
    static const int sizes[] = {12, 15, 19, 24};
    int best = 12, diff = 100000;
    for (int i = 0; i < 4; i++) {
        int d = px > sizes[i] ? px - sizes[i] : sizes[i] - px;
        if (d < diff) { diff = d; best = sizes[i]; }
    }
    return best;
}

/* -------------------------------------------------------------- style core */

void eg_style_default(EgStyle *s)
{
    memset(s, 0, sizeof(*s));
    s->font_size = 15;
    s->line_height_normal = 1;
    s->line_height = 19;
    s->align = EG_ALIGN_LEFT;
    s->display = EG_DISPLAY_INLINE;
    s->visible = 1;
    s->list_style = EG_LIST_DISC;
}

static int inherited_line_height(const EgStyle *parent)
{
    if (!parent) return 19;
    if (parent->line_height_normal) return (parent->font_size * 13 + 5) / 10;
    return parent->line_height;
}

void eg_style_child(const EgXmlNode *node, const EgStyle *parent, EgStyle *s)
{
    if (parent) *s = *parent;
    else eg_style_default(s);

    /* Non-inherited defaults. */
    s->has_bg = 0;
    s->bg = 0;
    s->margin_t = s->margin_r = s->margin_b = s->margin_l = 0;
    s->padding_t = s->padding_r = s->padding_b = s->padding_l = 0;
    s->border_t = s->border_r = s->border_b = s->border_l = 0;
    s->border_color = 0;
    s->display = node_is_element(node) ? EG_DISPLAY_INLINE : EG_DISPLAY_INLINE;
    s->page_break_before = s->page_break_after = 0;
    s->avoid_break = 0;
    s->keep_with_next = 0;
    s->underline = 0;
    s->strike = 0;
    s->vertical_align = 0;
    s->width_px = s->width_pct = -1;
    s->height_px = s->height_pct = -1;
    s->max_width_px = s->max_width_pct = -1;
    s->visible = parent ? parent->visible : 1;
    s->line_height = inherited_line_height(parent);
    if (parent) s->line_height_normal = parent->line_height_normal;
    else s->line_height_normal = 1;

    if (!node || !node->tag) return;
    const char *tag = node->tag;
    if (strcmp(tag, "body") == 0 || strcmp(tag, "html") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->align = EG_ALIGN_LEFT;
    } else if (strcmp(tag, "p") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->margin_b = 5;
        s->text_indent = 17;
        s->align = EG_ALIGN_JUSTIFY;
    } else if (strcmp(tag, "h1") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->font_size = 24;
        s->bold = 1;
        s->align = EG_ALIGN_CENTER;
        s->margin_t = 8;
        s->margin_b = 10;
        s->text_indent = 0;
        s->line_height_normal = 1;
        s->line_height = 30;
        s->keep_with_next = 1;
    } else if (strcmp(tag, "h2") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->font_size = 19;
        s->bold = 1;
        s->margin_t = 8;
        s->margin_b = 4;
        s->text_indent = 0;
        s->line_height = 24;
        s->keep_with_next = 1;
    } else if (strcmp(tag, "h3") == 0 || strcmp(tag, "h4") == 0 || strcmp(tag, "h5") == 0 || strcmp(tag, "h6") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->font_size = 15;
        s->bold = 1;
        if (strcmp(tag, "h4") == 0) s->italic = 1;
        s->margin_t = 6;
        s->margin_b = 3;
        s->text_indent = 0;
        s->line_height = 19;
        s->keep_with_next = 1;
    } else if (strcmp(tag, "blockquote") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->margin_t = 7;
        s->margin_b = 7;
        s->margin_l = 18;
        s->margin_r = 18;
        s->padding_l = 8;
        s->border_l = 2;
        s->border_color = eg_rgb565(145, 125, 95);
        s->text_indent = 0;
    } else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->margin_t = 5;
        s->margin_b = 5;
        s->padding_l = 20;
        s->text_indent = 0;
        if (strcmp(tag, "ol") == 0) s->list_style = EG_LIST_DECIMAL;
        else s->list_style = EG_LIST_DISC;
    } else if (strcmp(tag, "li") == 0) {
        s->display = EG_DISPLAY_LIST_ITEM;
        s->margin_t = 1;
        s->margin_b = 1;
        s->text_indent = 0;
    } else if (strcmp(tag, "pre") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->font_size = 12;
        s->white_space = EG_WS_PRE;
        s->margin_t = 5;
        s->margin_b = 5;
        s->margin_l = 6;
        s->margin_r = 6;
        s->line_height = 16;
        s->text_indent = 0;
    } else if (strcmp(tag, "code") == 0 || strcmp(tag, "tt") == 0 ||
               strcmp(tag, "kbd") == 0 || strcmp(tag, "samp") == 0) {
        s->font_size = 12;
    } else if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) {
        s->bold = 1;
    } else if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0 || strcmp(tag, "cite") == 0 ||
               strcmp(tag, "dfn") == 0) {
        s->italic = 1;
    } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
        s->underline = 1;
    } else if (strcmp(tag, "s") == 0 || strcmp(tag, "strike") == 0 || strcmp(tag, "del") == 0) {
        s->strike = 1;
    } else if (strcmp(tag, "sup") == 0 || strcmp(tag, "sub") == 0) {
        s->font_size = 12;
        s->vertical_align = strcmp(tag, "sup") == 0 ? 1 : -1;
        s->line_height = 16;
    } else if (strcmp(tag, "small") == 0) {
        s->font_size = 12;
    } else if (strcmp(tag, "big") == 0) {
        s->font_size = 19;
    } else if (strcmp(tag, "center") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->align = EG_ALIGN_CENTER;
        s->text_indent = 0;
    } else if (strcmp(tag, "figure") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->margin_t = 7;
        s->margin_b = 7;
        s->align = EG_ALIGN_CENTER;
        s->text_indent = 0;
    } else if (strcmp(tag, "figcaption") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->font_size = 12;
        s->align = EG_ALIGN_CENTER;
        s->italic = 1;
        s->text_indent = 0;
        s->margin_t = 2;
        s->margin_b = 4;
    } else if (strcmp(tag, "hr") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->margin_t = 9;
        s->margin_b = 9;
        s->margin_l = 16;
        s->margin_r = 16;
        s->border_t = 1;
        s->border_color = eg_rgb565(160, 140, 105);
    } else if (strcmp(tag, "table") == 0) {
        s->display = EG_DISPLAY_TABLE;
        s->font_size = 12;
        s->margin_t = 6;
        s->margin_b = 6;
        s->text_indent = 0;
        s->line_height = 16;
    } else if (strcmp(tag, "tr") == 0) {
        s->display = EG_DISPLAY_TABLE_ROW;
        s->line_height = 16;
    } else if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
        s->display = EG_DISPLAY_TABLE_CELL;
        s->border_t = s->border_r = s->border_b = s->border_l = 1;
        s->border_color = eg_rgb565(150, 130, 100);
        s->padding_t = s->padding_b = 1;
        s->padding_l = s->padding_r = 3;
        s->text_indent = 0;
        if (strcmp(tag, "th") == 0) s->bold = 1;
    } else if (strcmp(tag, "img") == 0) {
        s->display = EG_DISPLAY_BLOCK;
        s->align = EG_ALIGN_CENTER;
        s->text_indent = 0;
    } else if (strcmp(tag, "br") == 0) {
        s->display = EG_DISPLAY_INLINE;
    } else if (strcmp(tag, "a") == 0) {
        s->underline = 0;
    } else if (strcmp(tag, "nav") == 0 || strcmp(tag, "svg") == 0) {
        s->display = EG_DISPLAY_NONE;
    }
}

static void apply_border_side(EgStyle *s, int side, const char *value)
{
    int w = 1;
    uint16_t col = s->border_color;
    const char *p = value;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *e = p;
        while (*e && !isspace((unsigned char)*e)) e++;
        char tok[64];
        int n = (int)(e - p);
        if (n > 63) n = 63;
        memcpy(tok, p, (size_t)n);
        tok[n] = 0;
        if (isdigit((unsigned char)tok[0])) w = atoi(tok);
        else if (strcmp(tok, "thin") == 0) w = 1;
        else if (strcmp(tok, "medium") == 0) w = 2;
        else if (strcmp(tok, "thick") == 0) w = 3;
        else if (strcmp(tok, "solid") == 0 || strcmp(tok, "none") == 0 ||
                 strcmp(tok, "dotted") == 0 || strcmp(tok, "dashed") == 0) {
            if (strcmp(tok, "none") == 0) w = 0;
        } else if (is_color_token(tok) || isalpha((unsigned char)tok[0])) {
            col = parse_color(tok);
        }
        p = e;
    }
    if (side == 0 || side == -1) s->border_t = w;
    if (side == 1 || side == -1) s->border_r = w;
    if (side == 2 || side == -1) s->border_b = w;
    if (side == 3 || side == -1) s->border_l = w;
    s->border_color = col;
}

static void apply_length_prop(EgStyle *s, const char *value, int which)
{
    int fonts = s->font_size;
    int v = 0, pct = 0;
    parse_length(value, fonts, &v, &pct);
    if (v < 0) return;
    switch (which) {
        case 0: s->margin_t = v; break;
        case 1: s->margin_r = v; break;
        case 2: s->margin_b = v; break;
        case 3: s->margin_l = v; break;
        case 4: s->padding_t = v; break;
        case 5: s->padding_r = v; break;
        case 6: s->padding_b = v; break;
        case 7: s->padding_l = v; break;
        case 8: s->text_indent = v; break;
        case 9: s->margin_t = s->margin_r = s->margin_b = s->margin_l = v; break;
        case 10: s->padding_t = s->padding_r = s->padding_b = s->padding_l = v; break;
        default: break;
    }
}

static void apply_shorthand4(EgStyle *s, const char *value, int kind)
{
    const char *p = value;
    int vals[4] = {0, 0, 0, 0};
    int n = 0;
    int auto_any = 0;
    while (n < 4) {
        char *tok = next_token(&p);
        if (!tok) break;
        if (eg_stricmp(tok, "auto") == 0) { vals[n] = 0; auto_any = 1; }
        else {
            parse_length(tok, s->font_size, &vals[n], NULL);
        }
        free(tok);
        n++;
    }
    if (n == 0) return;
    int t = vals[0];
    int r = n > 1 ? vals[1] : t;
    int b = n > 2 ? vals[2] : t;
    int l = n > 3 ? vals[3] : r;
    if (kind == 0) {
        s->margin_t = t; s->margin_r = r; s->margin_b = b; s->margin_l = l;
        if (auto_any) s->display = s->display; /* centering is handled by layout for images */
    } else {
        s->padding_t = t; s->padding_r = r; s->padding_b = b; s->padding_l = l;
    }
}

static void apply_line_height(EgStyle *s, const char *value)
{
    if (!value) return;
    if (eg_stricmp(value, "normal") == 0) {
        s->line_height_normal = 1;
        s->line_height = (s->font_size * 13 + 5) / 10;
        return;
    }
    int v = 0;
    const char *p = value;
    int num100 = parse_num100(p, &p);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '%') { s->line_height = s->font_size * num100 / 10000; }
    else if (!*p) { s->line_height = s->font_size * num100 / 100; }
    else {
        int pct = 0;
        parse_length(value, s->font_size, &v, &pct);
        s->line_height = v;
    }
    if (s->line_height < 10) s->line_height = 10;
    s->line_height_normal = 0;
}

static void apply_font(EgStyle *s, const char *value)
{
    const char *p = value;
    while (*p) {
        char *tok = next_token(&p);
        if (!tok) break;
        if (eg_stricmp(tok, "italic") == 0 || eg_stricmp(tok, "oblique") == 0) s->italic = 1;
        else if (eg_stricmp(tok, "bold") == 0 || eg_stricmp(tok, "bolder") == 0) s->bold = 1;
        else if (eg_stricmp(tok, "normal") == 0) { }
        else if (isdigit((unsigned char)tok[0])) {
            int w = atoi(tok);
            if (w >= 600) s->bold = 1;
        } else if (strchr(tok, '/')) {
            char *slash = strchr(tok, '/');
            *slash = 0;
            int v = 0;
            parse_length(tok, s->font_size, &v, NULL);
            s->font_size = eg_css_nearest_font_size(v);
            apply_line_height(s, slash + 1);
        } else if (tok[0] == '\'' || tok[0] == '"' || eg_stricmp(tok, "serif") == 0 ||
                   eg_stricmp(tok, "sans-serif") == 0 || eg_stricmp(tok, "monospace") == 0) {
            /* All content uses the embedded serif face. */
        } else {
            int v = 0;
            int pct = 0;
            if (parse_length(tok, s->font_size, &v, &pct) && v > 0) {
                s->font_size = eg_css_nearest_font_size(v);
            }
        }
        free(tok);
    }
}

static void apply_prop(EgStyle *s, const char *prop, const char *value)
{
    if (!prop || !value) return;
    if (strcmp(prop, "color") == 0) {
        s->color = parse_color(value);
        s->color_set = 1;
    } else if (strcmp(prop, "background-color") == 0) {
        if (eg_stricmp(value, "transparent") == 0) {
            s->has_bg = 0;
        } else {
            s->bg = parse_color(value);
            s->has_bg = 1;
        }
    } else if (strcmp(prop, "font-size") == 0) {
        int v = 0, pct = 0;
        if (eg_stricmp(value, "small") == 0) v = 12;
        else if (eg_stricmp(value, "medium") == 0) v = 15;
        else if (eg_stricmp(value, "large") == 0) v = 19;
        else if (eg_stricmp(value, "x-large") == 0) v = 24;
        else if (eg_stricmp(value, "xx-large") == 0) v = 24;
        else {
            parse_length(value, s->font_size, &v, &pct);
        }
        if (v > 0) {
            s->font_size = eg_css_nearest_font_size(v);
            if (s->line_height_normal) s->line_height = (s->font_size * 13 + 5) / 10;
        }
    } else if (strcmp(prop, "font-weight") == 0) {
        if (eg_stricmp(value, "bold") == 0 || eg_stricmp(value, "bolder") == 0) s->bold = 1;
        else if (eg_stricmp(value, "normal") == 0 || eg_stricmp(value, "lighter") == 0) s->bold = 0;
        else if (atoi(value) >= 600) s->bold = 1;
        else if (atoi(value) > 0) s->bold = 0;
    } else if (strcmp(prop, "font-style") == 0) {
        s->italic = eg_stricmp(value, "italic") == 0 || eg_stricmp(value, "oblique") == 0;
    } else if (strcmp(prop, "font") == 0) {
        apply_font(s, value);
    } else if (strcmp(prop, "text-align") == 0) {
        if (eg_stricmp(value, "center") == 0) s->align = EG_ALIGN_CENTER;
        else if (eg_stricmp(value, "right") == 0 || eg_stricmp(value, "end") == 0) s->align = EG_ALIGN_RIGHT;
        else if (eg_stricmp(value, "justify") == 0) s->align = EG_ALIGN_JUSTIFY;
        else s->align = EG_ALIGN_LEFT;
    } else if (strcmp(prop, "text-indent") == 0) {
        apply_length_prop(s, value, 8);
    } else if (strcmp(prop, "line-height") == 0) {
        apply_line_height(s, value);
    } else if (strcmp(prop, "margin") == 0) {
        apply_shorthand4(s, value, 0);
    } else if (strcmp(prop, "margin-top") == 0) apply_length_prop(s, value, 0);
    else if (strcmp(prop, "margin-right") == 0) apply_length_prop(s, value, 1);
    else if (strcmp(prop, "margin-bottom") == 0) apply_length_prop(s, value, 2);
    else if (strcmp(prop, "margin-left") == 0) apply_length_prop(s, value, 3);
    else if (strcmp(prop, "padding") == 0) apply_shorthand4(s, value, 1);
    else if (strcmp(prop, "padding-top") == 0) apply_length_prop(s, value, 4);
    else if (strcmp(prop, "padding-right") == 0) apply_length_prop(s, value, 5);
    else if (strcmp(prop, "padding-bottom") == 0) apply_length_prop(s, value, 6);
    else if (strcmp(prop, "padding-left") == 0) apply_length_prop(s, value, 7);
    else if (strcmp(prop, "border") == 0 || strcmp(prop, "border-top") == 0 ||
             strcmp(prop, "border-right") == 0 || strcmp(prop, "border-bottom") == 0 ||
             strcmp(prop, "border-left") == 0) {
        int side = strcmp(prop, "border") == 0 ? -1 :
                   (strcmp(prop, "border-top") == 0 ? 0 :
                    (strcmp(prop, "border-right") == 0 ? 1 :
                     (strcmp(prop, "border-bottom") == 0 ? 2 : 3)));
        apply_border_side(s, side, value);
    } else if (strcmp(prop, "border-color") == 0) {
        s->border_color = parse_color(value);
    } else if (strcmp(prop, "border-width") == 0) {
        int w = 1;
        parse_length(value, s->font_size, &w, NULL);
        s->border_t = s->border_r = s->border_b = s->border_l = w;
    } else if (strcmp(prop, "width") == 0) {
        int v = 0, pct = 0;
        parse_length(value, s->font_size, &v, &pct);
        if (pct) s->width_pct = pct;
        else s->width_px = v;
    } else if (strcmp(prop, "height") == 0) {
        int v = 0, pct = 0;
        parse_length(value, s->font_size, &v, &pct);
        if (pct) s->height_pct = pct;
        else s->height_px = v;
    } else if (strcmp(prop, "max-width") == 0) {
        int v = 0, pct = 0;
        parse_length(value, s->font_size, &v, &pct);
        if (pct) s->max_width_pct = pct;
        else s->max_width_px = v;
    } else if (strcmp(prop, "display") == 0) {
        if (eg_stricmp(value, "none") == 0) s->display = EG_DISPLAY_NONE;
        else if (eg_stricmp(value, "block") == 0) s->display = EG_DISPLAY_BLOCK;
        else if (eg_stricmp(value, "inline") == 0) s->display = EG_DISPLAY_INLINE;
        else if (strcmp(value, "list-item") == 0) s->display = EG_DISPLAY_LIST_ITEM;
        else if (eg_stricmp(value, "table") == 0) s->display = EG_DISPLAY_TABLE;
        else if (eg_stricmp(value, "table-row") == 0) s->display = EG_DISPLAY_TABLE_ROW;
        else if (eg_stricmp(value, "table-cell") == 0) s->display = EG_DISPLAY_TABLE_CELL;
    } else if (strcmp(prop, "page-break-before") == 0 || strcmp(prop, "break-before") == 0) {
        if (eg_stricmp(value, "always") == 0 || eg_stricmp(value, "page") == 0 ||
            eg_stricmp(value, "left") == 0 || eg_stricmp(value, "right") == 0)
            s->page_break_before = 1;
    } else if (strcmp(prop, "page-break-after") == 0 || strcmp(prop, "break-after") == 0) {
        if (eg_stricmp(value, "always") == 0 || eg_stricmp(value, "page") == 0 ||
            eg_stricmp(value, "left") == 0 || eg_stricmp(value, "right") == 0)
            s->page_break_after = 1;
    } else if (strcmp(prop, "page-break-inside") == 0 || strcmp(prop, "break-inside") == 0) {
        if (eg_stricmp(value, "avoid") == 0) s->avoid_break = 1;
    } else if (strcmp(prop, "white-space") == 0) {
        if (eg_stricmp(value, "pre") == 0 || eg_stricmp(value, "pre-wrap") == 0) s->white_space = EG_WS_PRE;
        else if (eg_stricmp(value, "nowrap") == 0) s->white_space = EG_WS_NOWRAP;
        else s->white_space = EG_WS_NORMAL;
    } else if (strcmp(prop, "text-transform") == 0) {
        if (eg_stricmp(value, "uppercase") == 0) s->text_transform = 1;
        else if (eg_stricmp(value, "lowercase") == 0) s->text_transform = 2;
        else if (eg_stricmp(value, "capitalize") == 0) s->text_transform = 3;
        else s->text_transform = 0;
    } else if (strcmp(prop, "letter-spacing") == 0) {
        if (eg_stricmp(value, "normal") == 0) s->letter_spacing = 0;
        else parse_length(value, s->font_size, &s->letter_spacing, NULL);
    } else if (strcmp(prop, "word-spacing") == 0) {
        if (eg_stricmp(value, "normal") == 0) s->word_spacing = 0;
        else parse_length(value, s->font_size, &s->word_spacing, NULL);
    } else if (strcmp(prop, "text-decoration") == 0 || strcmp(prop, "text-decoration-line") == 0) {
        char low[96];
        int n = snprintf(low, sizeof(low), "%s", value);
        for (int i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)low[i]);
        if (strstr(low, "none")) { s->underline = s->strike = 0; }
        if (strstr(low, "underline")) s->underline = 1;
        if (strstr(low, "line-through")) s->strike = 1;
    } else if (strcmp(prop, "vertical-align") == 0) {
        if (eg_stricmp(value, "super") == 0) s->vertical_align = 1;
        else if (eg_stricmp(value, "sub") == 0) s->vertical_align = -1;
        else s->vertical_align = 0;
    } else if (strcmp(prop, "list-style-type") == 0 || strcmp(prop, "list-style") == 0) {
        if (strstr(value, "none")) s->list_style = EG_LIST_NONE;
        else if (strstr(value, "square")) s->list_style = EG_LIST_SQUARE;
        else if (strstr(value, "circle")) s->list_style = EG_LIST_CIRCLE;
        else if (strstr(value, "disc")) s->list_style = EG_LIST_DISC;
        else if (strstr(value, "lower-alpha")) s->list_style = EG_LIST_LOWER_ALPHA;
        else if (strstr(value, "upper-alpha")) s->list_style = EG_LIST_UPPER_ALPHA;
        else if (strstr(value, "lower-roman")) s->list_style = EG_LIST_LOWER_ROMAN;
        else if (strstr(value, "upper-roman")) s->list_style = EG_LIST_UPPER_ROMAN;
        else if (strstr(value, "decimal")) s->list_style = EG_LIST_DECIMAL;
    } else if (strcmp(prop, "visibility") == 0) {
        if (eg_stricmp(value, "hidden") == 0 || eg_stricmp(value, "collapse") == 0)
            s->visible = 0;
        else s->visible = 1;
    } else if (strcmp(prop, "background") == 0) {
        const char *p = value;
        while (*p) {
            char *tok = next_token(&p);
            if (!tok) break;
            if (is_color_token(tok) || isalpha((unsigned char)tok[0])) {
                if (eg_stricmp(tok, "transparent") != 0 && eg_stricmp(tok, "none") != 0) {
                    uint16_t col = parse_color(tok);
                    if (col) { s->bg = col; s->has_bg = 1; }
                }
            }
            free(tok);
        }
    }
}

typedef struct {
    const EgCssDecl *decl;
    int important;
    int specificity;
    int order;
} Cand;

static int cand_cmp(const void *a, const void *b)
{
    const Cand *ca = (const Cand *)a;
    const Cand *cb = (const Cand *)b;
    if (ca->important != cb->important) return ca->important - cb->important;
    if (ca->specificity != cb->specificity) return ca->specificity - cb->specificity;
    return ca->order - cb->order;
}

static int parse_inline_decls(const char *style, EgCssDecl **out, int *count, int base_order)
{
    *out = NULL;
    *count = 0;
    if (!style) return 0;
    int len = (int)strlen(style);
    int i = 0;
    while (i < len) {
        while (i < len && (isspace((unsigned char)style[i]) || style[i] == ';')) i++;
        if (i >= len) break;
        int start = i;
        while (i < len && style[i] != ':' && style[i] != ';') i++;
        if (i >= len || style[i] != ':') break;
        int pn = i - start;
        while (pn > 0 && isspace((unsigned char)style[start + pn - 1])) pn--;
        char *prop = dupn(style + start, pn);
        i++;
        while (i < len && isspace((unsigned char)style[i])) i++;
        int vstart = i;
        int bracket = 0, paren = 0;
        char quote = 0;
        while (i < len) {
            char c = style[i];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') quote = c;
            else if (c == '(') paren++;
            else if (c == ')') paren--;
            else if (c == '[') bracket++;
            else if (c == ']') bracket--;
            else if (c == ';' && bracket <= 0 && paren <= 0) break;
            i++;
        }
        int vn = i - vstart;
        while (vn > 0 && isspace((unsigned char)style[vstart + vn - 1])) vn--;
        char *val = dupn(style + vstart, vn);
        int important = 0;
        char *imp = NULL;
        for (char *q = val; *q; q++) {
            if (*q == '!' && eg_strnieq(q, "!important", 10)) { imp = q; break; }
        }
        if (imp) {
            *imp = 0;
            important = 1;
            char *e = val + strlen(val);
            while (e > val && isspace((unsigned char)e[-1])) *--e = 0;
        }
        EgCssDecl *nd = eg_realloc(*out, sizeof(EgCssDecl) * (size_t)(*count + 1));
        if (!nd) { free(prop); free(val); break; }
        *out = nd;
        EgCssDecl *d = &(*out)[(*count)++];
        d->prop = prop;
        d->value = val;
        d->important = important;
        d->order = base_order++;
        (void)start;
    }
    return *count;
}

static void free_decls(EgCssDecl *d, int n)
{
    for (int i = 0; i < n; i++) { free(d[i].prop); free(d[i].value); }
    free(d);
}

void eg_css_apply_inline(EgStyle *style, const char *decls)
{
    if (!decls || !*decls) return;
    EgCssDecl *d = NULL;
    int n = 0;
    parse_inline_decls(decls, &d, &n, 10000);
    for (int i = 0; i < n; i++) {
        char *prop = eg_trim(d[i].prop);
        apply_prop(style, prop, d[i].value);
    }
    free_decls(d, n);
}

void eg_css_compute(const EgStylesheet *sheet, const EgXmlNode *node,
                    const EgStyle *parent, EgStyle *out, int root_font_size)
{
    eg_style_child(node, parent, out);
    if (!out->font_size) out->font_size = root_font_size;

    Cand *cands = NULL;
    int cand_count = 0, cand_cap = 0;
    if (sheet && node && node->tag) {
        for (int r = 0; r < sheet->rule_count; r++) {
            EgCssRule *rule = &sheet->rules[r];
            if (!rule->selector.valid) continue;
            if (!selector_matches(&rule->selector, node)) continue;
            for (int d = 0; d < rule->decl_count; d++) {
                if (cand_count == cand_cap) {
                    cand_cap = cand_cap ? cand_cap * 2 : 32;
                    Cand *nc = eg_realloc(cands, sizeof(Cand) * (size_t)cand_cap);
                    if (!nc) break;
                    cands = nc;
                }
                cands[cand_count].decl = &rule->decls[d];
                cands[cand_count].important = rule->decls[d].important;
                cands[cand_count].specificity = rule->selector.specificity;
                cands[cand_count].order = rule->decls[d].order;
                cand_count++;
            }
        }
    }
    EgCssDecl *inline_decls = NULL;
    int inline_count = 0;
    if (node) {
        const char *style = eg_xml_attr(node, "style");
        if (style && *style)
            parse_inline_decls(style, &inline_decls, &inline_count, 200000);
    }
    int total = cand_count + inline_count;
    if (total > 0) {
        Cand *all = eg_malloc(sizeof(Cand) * (size_t)total);
        if (all) {
            memcpy(all, cands, sizeof(Cand) * (size_t)cand_count);
            for (int i = 0; i < inline_count; i++) {
                all[cand_count + i].decl = &inline_decls[i];
                all[cand_count + i].important = inline_decls[i].important;
                all[cand_count + i].specificity = 1000;
                all[cand_count + i].order = inline_decls[i].order;
            }
            qsort(all, (size_t)total, sizeof(Cand), cand_cmp);
            for (int i = 0; i < total; i++) {
                if (!all[i].decl->prop[0]) continue;
                apply_prop(out, all[i].decl->prop, all[i].decl->value);
            }
            free(all);
        }
    }
    free(cands);
    free_decls(inline_decls, inline_count);

    if (out->font_size <= 0) out->font_size = root_font_size;
    if (out->line_height_normal)
        out->line_height = (out->font_size * 13 + 5) / 10;
    if (out->line_height < out->font_size) out->line_height = out->font_size + 2;
    if (out->text_indent < -out->font_size * 4) out->text_indent = 0;
}

/* --------------------------------------------------------------- parsing */

static int add_rule(EgStylesheet *sheet, const char *sel, int sel_len,
                    EgCssDecl *decls, int decl_count)
{
    EgCssSelector s;
    if (!parse_selector(sel, sel_len, &s)) { selector_free(&s); return 0; }
    if (sheet->rule_count == sheet->rule_cap) {
        sheet->rule_cap = sheet->rule_cap ? sheet->rule_cap * 2 : 32;
        EgCssRule *nr = eg_realloc(sheet->rules, sizeof(EgCssRule) * (size_t)sheet->rule_cap);
        if (!nr) { selector_free(&s); return 0; }
        sheet->rules = nr;
    }
    EgCssRule *r = &sheet->rules[sheet->rule_count++];
    memset(r, 0, sizeof(*r));
    r->selector = s;
    r->decl_count = decl_count;
    if (decl_count) {
        r->decls = eg_malloc(sizeof(EgCssDecl) * (size_t)decl_count);
        for (int i = 0; i < decl_count; i++) {
            r->decls[i].prop = eg_strdup(decls[i].prop);
            r->decls[i].value = eg_strdup(decls[i].value);
            r->decls[i].important = decls[i].important;
            r->decls[i].order = decls[i].order;
        }
    }
    return 1;
}

static int parse_decl_block(const char *s, int len, EgCssDecl **out, int *count)
{
    *out = NULL;
    *count = 0;
    int i = 0;
    while (i < len) {
        while (i < len && (isspace((unsigned char)s[i]) || s[i] == ';')) i++;
        if (i >= len) break;
        int pstart = i;
        int paren = 0;
        while (i < len) {
            char c = s[i];
            if (c == '(') paren++;
            else if (c == ')') paren--;
            else if (c == ':' && paren <= 0) break;
            else if (c == ';' && paren <= 0) { i = len; break; }
            i++;
        }
        if (i >= len || s[i] != ':') break;
        int pn = i - pstart;
        while (pn > 0 && isspace((unsigned char)s[pstart + pn - 1])) pn--;
        if (pn <= 0) { i++; continue; }
        char *prop = dupn(s + pstart, pn);
        for (char *q = prop; *q; q++) *q = (char)tolower((unsigned char)*q);
        i++;
        while (i < len && isspace((unsigned char)s[i])) i++;
        int vstart = i;
        int bracket = 0;
        char quote = 0;
        while (i < len) {
            char c = s[i];
            if (quote) {
                if (c == '\\' && i + 1 < len) i++;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') quote = c;
            else if (c == '(') paren++;
            else if (c == ')') paren--;
            else if (c == '[') bracket++;
            else if (c == ']') bracket--;
            else if (c == ';' && bracket <= 0 && paren <= 0) break;
            i++;
        }
        int vn = i - vstart;
        while (vn > 0 && isspace((unsigned char)s[vstart + vn - 1])) vn--;
        char *val = dupn(s + vstart, vn);
        int important = 0;
        char *imp = strstr(val, "!important");
        if (!imp) imp = strstr(val, "! important");
        if (imp) {
            important = 1;
            *imp = 0;
            char *e = val + strlen(val);
            while (e > val && isspace((unsigned char)e[-1])) *--e = 0;
        }
        EgCssDecl *nd = eg_realloc(*out, sizeof(EgCssDecl) * (size_t)(*count + 1));
        if (!nd) { free(prop); free(val); break; }
        *out = nd;
        EgCssDecl *d = &(*out)[(*count)++];
        d->prop = prop;
        d->value = val;
        d->important = important;
        d->order = *count;
        if (i < len && s[i] == ';') i++;
    }
    return *count;
}

static void parse_rules(EgStylesheet *sheet, const char *css, int len, int *order, int allow);

static void skip_at_rule(EgStylesheet *sheet, const char *css, int len, int *ip, int *order, int allow)
{
    int i = *ip;
    int start = i;
    while (i < len && !isspace((unsigned char)css[i]) && css[i] != '{' && css[i] != ';') i++;
    char atname[32];
    int n = i - start;
    if (n > 31) n = 31;
    memcpy(atname, css + start, (size_t)n);
    atname[n] = 0;
    for (char *q = atname; *q; q++) *q = (char)tolower((unsigned char)*q);
    while (i < len && isspace((unsigned char)css[i])) i++;
    int pre_start = i;
    int brace = find_char(css, len, i, '{');
    int semi = find_char(css, len, i, ';');
    if (atname[0] == '@' && strcmp(atname, "@media") == 0 && brace >= 0) {
        int pend = brace;
        char media[128];
        int mn = pend - pre_start;
        if (mn > 127) mn = 127;
        memcpy(media, css + pre_start, (size_t)mn);
        media[mn] = 0;
        for (char *q = media; *q; q++) *q = (char)tolower((unsigned char)*q);
        int inner_ok = allow;
        if (strstr(media, "print") && !strstr(media, "screen") && !strstr(media, "all")) inner_ok = 0;
        int close = find_matching_brace(css, len, brace);
        if (close < 0) close = len;
        if (inner_ok) parse_rules(sheet, css + brace + 1, close - brace - 1, order, allow);
        *ip = close + 1;
        return;
    }
    if (brace >= 0 && (semi < 0 || brace < semi)) {
        int close = find_matching_brace(css, len, brace);
        if (close < 0) close = len;
        *ip = close + 1;
        return;
    }
    *ip = semi >= 0 ? semi + 1 : len;
    (void)sheet;
}

static void parse_rules(EgStylesheet *sheet, const char *css, int len, int *order, int allow)
{
    int i = 0;
    while (i < len) {
        while (i < len && isspace((unsigned char)css[i])) i++;
        if (i >= len) break;
        if (css[i] == '@') { skip_at_rule(sheet, css, len, &i, order, allow); continue; }
        int brace = find_char(css, len, i, '{');
        if (brace < 0) break;
        int close = find_matching_brace(css, len, brace);
        if (close < 0) close = len;
        int sel_start = i;
        int sel_end = brace;
        while (sel_end > sel_start && isspace((unsigned char)css[sel_end - 1])) sel_end--;
        EgCssDecl *decls = NULL;
        int decl_count = 0;
        parse_decl_block(css + brace + 1, close - brace - 1, &decls, &decl_count);
        if (decl_count > 0) {
            /* split selectors on commas outside attribute brackets */
            int s = sel_start;
            int depth = 0;
            char quote = 0;
            for (int k = sel_start; k <= sel_end; k++) {
                char c = k < sel_end ? css[k] : ',';
                if (quote) {
                    if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') { quote = c; continue; }
                if (c == '[') depth++;
                else if (c == ']') depth--;
                else if (c == ',' && depth <= 0) {
                    int a = s, b = k;
                    while (a < b && isspace((unsigned char)css[a])) a++;
                    while (b > a && isspace((unsigned char)css[b - 1])) b--;
                    if (b > a) {
                        EgCssDecl *copy = eg_malloc(sizeof(EgCssDecl) * (size_t)decl_count);
                        for (int q = 0; q < decl_count; q++) {
                            copy[q] = decls[q];
                            copy[q].order = (*order)++;
                        }
                        if (add_rule(sheet, css + a, b - a, copy, decl_count)) {
                            /* add_rule duplicated strings/orders already */
                        }
                        free(copy);
                    }
                    s = k + 1;
                }
            }
        }
        free_decls(decls, decl_count);
        i = close + 1;
    }
}

void eg_css_init(EgStylesheet *sheet)
{
    memset(sheet, 0, sizeof(*sheet));
}

void eg_css_free(EgStylesheet *sheet)
{
    for (int i = 0; i < sheet->rule_count; i++) {
        selector_free(&sheet->rules[i].selector);
        free_decls(sheet->rules[i].decls, sheet->rules[i].decl_count);
    }
    free(sheet->rules);
    memset(sheet, 0, sizeof(*sheet));
}

int eg_css_parse(EgStylesheet *sheet, const char *css, int len)
{
    if (!css || len <= 0) return 0;
    char *clean = strip_comments(css, len);
    int order = 1;
    parse_rules(sheet, clean, (int)strlen(clean), &order, 1);
    free(clean);
    return sheet->rule_count;
}
