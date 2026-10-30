#ifndef EG_XML_H
#define EG_XML_H

#include "util.h"

typedef struct EgXmlAttr {
    char *name;
    char *value;
} EgXmlAttr;

typedef struct EgXmlNode {
    char *tag;                  /* NULL for a text node */
    char *text;                 /* text for text nodes */
    EgXmlAttr *attrs;
    int attr_count;
    struct EgXmlNode *parent;
    struct EgXmlNode *children;
    struct EgXmlNode *last_child;
    struct EgXmlNode *next;
} EgXmlNode;

EgXmlNode *eg_xml_parse(const char *data, int len);
void eg_xml_free(EgXmlNode *node);
const char *eg_xml_attr(const EgXmlNode *node, const char *name);
int eg_xml_has_attr(const EgXmlNode *node, const char *name);
const EgXmlNode *eg_xml_child(const EgXmlNode *node, const char *tag);
EgXmlNode *eg_xml_child_mut(EgXmlNode *node, const char *tag);
const EgXmlNode *eg_xml_find(const EgXmlNode *node, const char *tag);
const EgXmlNode *eg_xml_find_attr(const EgXmlNode *node, const char *tag,
                                  const char *attr, const char *value,
                                  int contains);
void eg_xml_text_append(const EgXmlNode *node, EgStr *out);
char *eg_xml_text(const EgXmlNode *node);
int eg_xml_is_text(const EgXmlNode *node);

#endif
