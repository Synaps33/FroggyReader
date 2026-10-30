#include "xml.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

typedef struct {
    const char *data;
    int len;
    int pos;
} Parser;

static void lower_ascii(char *s)
{
    for (; s && *s; s++) *s = (char)tolower((unsigned char)*s);
}

static EgXmlNode *node_new(char *tag)
{
    EgXmlNode *n = eg_calloc(1, sizeof(EgXmlNode));
    if (!n) { free(tag); return NULL; }
    n->tag = tag;
    return n;
}

static void node_add_child(EgXmlNode *parent, EgXmlNode *child)
{
    child->parent = parent;
    if (parent->last_child) parent->last_child->next = child;
    else parent->children = child;
    parent->last_child = child;
}

static void node_add_text(EgXmlNode *parent, char *text)
{
    if (!text || !*text) { free(text); return; }
    EgXmlNode *n = node_new(NULL);
    if (!n) { free(text); return; }
    n->text = text;
    node_add_child(parent, n);
}

static int is_name_char(int c)
{
    return isalnum((unsigned char)c) || c == ':' || c == '-' || c == '_' || c == '.';
}

static char *parse_name(Parser *p)
{
    int start = p->pos;
    while (p->pos < p->len && is_name_char(p->data[p->pos])) p->pos++;
    if (p->pos == start) return NULL;
    char *name = eg_strndup(p->data + start, p->pos - start);
    if (name) {
        char *colon = strrchr(name, ':');
        if (colon && colon[1]) {
            memmove(name, colon + 1, strlen(colon + 1) + 1);
        }
        lower_ascii(name);
    }
    return name;
}

static int parse_attrs(Parser *p, EgXmlNode *node)
{
    while (p->pos < p->len) {
        while (p->pos < p->len && isspace((unsigned char)p->data[p->pos])) p->pos++;
        if (p->pos >= p->len) return 0;
        if (p->data[p->pos] == '>' || p->data[p->pos] == '/') return 1;
        char *name = parse_name(p);
        if (!name) { p->pos++; continue; }
        while (p->pos < p->len && isspace((unsigned char)p->data[p->pos])) p->pos++;
        char *value = eg_strdup("");
        if (p->pos < p->len && p->data[p->pos] == '=') {
            p->pos++;
            while (p->pos < p->len && isspace((unsigned char)p->data[p->pos])) p->pos++;
            if (p->pos < p->len && (p->data[p->pos] == '"' || p->data[p->pos] == '\'')) {
                char quote = p->data[p->pos++];
                int start = p->pos;
                while (p->pos < p->len && p->data[p->pos] != quote) p->pos++;
                EgStr sv;
                eg_str_init(&sv);
                eg_decode_entities(p->data + start, p->pos - start, &sv);
                free(value);
                value = eg_str_steal(&sv);
                if (p->pos < p->len) p->pos++;
            } else {
                int start = p->pos;
                while (p->pos < p->len && !isspace((unsigned char)p->data[p->pos]) &&
                       p->data[p->pos] != '>' && p->data[p->pos] != '/') p->pos++;
                EgStr sv;
                eg_str_init(&sv);
                eg_decode_entities(p->data + start, p->pos - start, &sv);
                free(value);
                value = eg_str_steal(&sv);
            }
        }
        EgXmlAttr *na = eg_realloc(node->attrs, sizeof(EgXmlAttr) * (size_t)(node->attr_count + 1));
        if (!na) { free(name); free(value); return 0; }
        node->attrs = na;
        node->attrs[node->attr_count].name = name;
        node->attrs[node->attr_count].value = value;
        node->attr_count++;
    }
    return 0;
}

static int is_void_tag(const char *tag)
{
    static const char *voids[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    for (int i = 0; voids[i]; i++)
        if (strcmp(voids[i], tag) == 0) return 1;
    return 0;
}

static char *read_text(Parser *p)
{
    int start = p->pos;
    while (p->pos < p->len && p->data[p->pos] != '<') p->pos++;
    EgStr out;
    eg_str_init(&out);
    eg_decode_entities(p->data + start, p->pos - start, &out);
    return eg_str_steal(&out);
}

static void skip_until(Parser *p, const char *ending)
{
    int n = (int)strlen(ending);
    while (p->pos < p->len) {
        if (p->pos + n <= p->len && memcmp(p->data + p->pos, ending, (size_t)n) == 0) {
            p->pos += n;
            return;
        }
        p->pos++;
    }
}

EgXmlNode *eg_xml_parse(const char *data, int len)
{
    Parser p;
    p.data = data ? data : "";
    p.len = len;
    p.pos = 0;
    EgXmlNode *root = node_new(eg_strdup("#document"));
    if (!root) return NULL;
    EgXmlNode *stack[128];
    int depth = 0;
    stack[depth++] = root;

    while (p.pos < p.len) {
        if (p.data[p.pos] != '<') {
            char *text = read_text(&p);
            node_add_text(stack[depth - 1], text);
            continue;
        }
        if (p.pos + 3 < p.len && memcmp(p.data + p.pos, "<!--", 4) == 0) {
            p.pos += 4;
            skip_until(&p, "-->");
            continue;
        }
        if (p.pos + 8 < p.len && memcmp(p.data + p.pos, "<![CDATA[", 9) == 0) {
            p.pos += 9;
            int start = p.pos;
            while (p.pos + 2 < p.len && memcmp(p.data + p.pos, "]]>", 3) != 0) p.pos++;
            char *text = eg_strndup(p.data + start, p.pos - start);
            node_add_text(stack[depth - 1], text);
            if (p.pos + 2 < p.len) p.pos += 3;
            continue;
        }
        if (p.pos + 1 < p.len && p.data[p.pos + 1] == '?') {
            p.pos += 2;
            skip_until(&p, "?>");
            continue;
        }
        if (p.pos + 1 < p.len && p.data[p.pos + 1] == '!') {
            p.pos += 2;
            int bracket = 0;
            while (p.pos < p.len) {
                if (p.data[p.pos] == '[') bracket++;
                else if (p.data[p.pos] == ']') bracket--;
                else if (p.data[p.pos] == '>' && bracket <= 0) { p.pos++; break; }
                p.pos++;
            }
            continue;
        }
        if (p.pos + 1 < p.len && p.data[p.pos + 1] == '/') {
            p.pos += 2;
            char *tag = parse_name(&p);
            skip_until(&p, ">");
            if (tag) {
                for (int i = depth - 1; i > 0; i--) {
                    if (stack[i]->tag && strcmp(stack[i]->tag, tag) == 0) {
                        depth = i;
                        break;
                    }
                }
                free(tag);
            }
            continue;
        }
        p.pos++;
        char *tag = parse_name(&p);
        if (!tag) {
            char *text = eg_strdup("<");
            node_add_text(stack[depth - 1], text);
            continue;
        }
        EgXmlNode *node = node_new(tag);
        if (!node) continue;
        if (!parse_attrs(&p, node)) {
            /* malformed; skip this tag */
            while (p.pos < p.len && p.data[p.pos] != '>') p.pos++;
            if (p.pos < p.len) p.pos++;
            eg_xml_free(node);
            continue;
        }
        int self_close = 0;
        while (p.pos < p.len && isspace((unsigned char)p.data[p.pos])) p.pos++;
        if (p.pos < p.len && p.data[p.pos] == '/') { self_close = 1; p.pos++; }
        if (p.pos < p.len && p.data[p.pos] == '>') p.pos++;
        node_add_child(stack[depth - 1], node);
        if (!self_close && !is_void_tag(node->tag)) {
            if (depth < 128) stack[depth++] = node;
            else {
                /* Too deep: close it immediately. */
            }
        }
    }
    return root;
}

void eg_xml_free(EgXmlNode *node)
{
    while (node) {
        EgXmlNode *next = node->next;
        eg_xml_free(node->children);
        free(node->tag);
        free(node->text);
        for (int i = 0; i < node->attr_count; i++) {
            free(node->attrs[i].name);
            free(node->attrs[i].value);
        }
        free(node->attrs);
        free(node);
        node = next;
    }
}

const char *eg_xml_attr(const EgXmlNode *node, const char *name)
{
    if (!node) return NULL;
    for (int i = 0; i < node->attr_count; i++)
        if (strcmp(node->attrs[i].name, name) == 0) return node->attrs[i].value;
    return NULL;
}

int eg_xml_has_attr(const EgXmlNode *node, const char *name)
{
    return eg_xml_attr(node, name) != NULL;
}

const EgXmlNode *eg_xml_child(const EgXmlNode *node, const char *tag)
{
    if (!node) return NULL;
    for (const EgXmlNode *c = node->children; c; c = c->next)
        if (c->tag && strcmp(c->tag, tag) == 0) return c;
    return NULL;
}

EgXmlNode *eg_xml_child_mut(EgXmlNode *node, const char *tag)
{
    return (EgXmlNode *)eg_xml_child(node, tag);
}

const EgXmlNode *eg_xml_find(const EgXmlNode *node, const char *tag)
{
    if (!node) return NULL;
    for (const EgXmlNode *c = node->children; c; c = c->next) {
        if (c->tag && strcmp(c->tag, tag) == 0) return c;
        const EgXmlNode *r = eg_xml_find(c, tag);
        if (r) return r;
    }
    return NULL;
}

const EgXmlNode *eg_xml_find_attr(const EgXmlNode *node, const char *tag,
                                  const char *attr, const char *value,
                                  int contains)
{
    if (!node) return NULL;
    for (const EgXmlNode *c = node->children; c; c = c->next) {
        if (c->tag && (!tag || strcmp(c->tag, tag) == 0)) {
            const char *v = eg_xml_attr(c, attr);
            if (v && ((contains && strstr(v, value)) || (!contains && strcmp(v, value) == 0)))
                return c;
        }
        const EgXmlNode *r = eg_xml_find_attr(c, tag, attr, value, contains);
        if (r) return r;
    }
    return NULL;
}

void eg_xml_text_append(const EgXmlNode *node, EgStr *out)
{
    if (!node) return;
    if (!node->tag) {
        if (node->text) eg_str_append_cstr(out, node->text);
        return;
    }
    for (const EgXmlNode *c = node->children; c; c = c->next)
        eg_xml_text_append(c, out);
}

char *eg_xml_text(const EgXmlNode *node)
{
    EgStr s;
    eg_str_init(&s);
    eg_xml_text_append(node, &s);
    return eg_str_steal(&s);
}

int eg_xml_is_text(const EgXmlNode *node)
{
    return node && node->tag == NULL;
}
