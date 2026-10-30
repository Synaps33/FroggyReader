#include "epub.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "miniz.h"

#ifndef MZ_ZIP_FLAG_CASE_SENSITIVE
#define MZ_ZIP_FLAG_CASE_SENSITIVE 0
#endif

static void free_manifest(EgEpub *ep)
{
    for (int i = 0; i < ep->manifest_count; i++) {
        free(ep->manifest[i].id);
        free(ep->manifest[i].href);
        free(ep->manifest[i].full_path);
        free(ep->manifest[i].media_type);
        free(ep->manifest[i].properties);
    }
    free(ep->manifest);
    ep->manifest = NULL;
    ep->manifest_count = 0;
}

static void free_spine(EgEpub *ep)
{
    for (int i = 0; i < ep->spine_count; i++) free(ep->spine[i].full_path);
    free(ep->spine);
    ep->spine = NULL;
    ep->spine_count = 0;
}

static void free_toc(EgEpub *ep)
{
    for (int i = 0; i < ep->toc_count; i++) {
        free(ep->toc[i].title);
        free(ep->toc[i].href);
        free(ep->toc[i].fragment);
    }
    free(ep->toc);
    ep->toc = NULL;
    ep->toc_count = 0;
}

static char *split_fragment(const char *href, char **frag_out)
{
    const char *hash = href ? strchr(href, '#') : NULL;
    int n = hash ? (int)(hash - href) : (int)(href ? strlen(href) : 0);
    char *base = eg_strndup(href ? href : "", n);
    if (frag_out) {
        if (hash) {
            char *f = eg_strdup(hash + 1);
            eg_url_decode(f);
            *frag_out = f;
        } else {
            *frag_out = NULL;
        }
    }
    return base;
}

static const EgManifestItem *manifest_by_id(const EgEpub *ep, const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < ep->manifest_count; i++)
        if (ep->manifest[i].id && strcmp(ep->manifest[i].id, id) == 0)
            return &ep->manifest[i];
    return NULL;
}

static int add_toc(EgEpub *ep, const char *title, const char *href,
                   const char *base_dir, int level)
{
    if (!href) return 0;
    char *frag = NULL;
    char *rel = split_fragment(href, &frag);
    char full[EG_MAX_PATH];
    eg_path_normalize(full, sizeof(full), base_dir, rel);
    free(rel);
    EgTocEntry *t = eg_realloc(ep->toc, sizeof(EgTocEntry) * (size_t)(ep->toc_count + 1));
    if (!t) { free(frag); return 0; }
    ep->toc = t;
    EgTocEntry *e = &ep->toc[ep->toc_count];
    memset(e, 0, sizeof(*e));
    e->title = eg_strdup(title && *title ? title : "");
    e->href = eg_strdup(full);
    e->fragment = frag;
    e->level = level;
    e->chapter = eg_epub_chapter_index(ep, full);
    ep->toc_count++;
    return 1;
}

int eg_epub_read(EgEpub *ep, const char *full_path, uint8_t **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;
    if (!ep || !ep->is_open || !full_path) return 0;
    mz_zip_archive *zip = (mz_zip_archive *)ep->zip;
    int idx = mz_zip_reader_locate_file(zip, full_path, NULL, 0);
    if (idx < 0) {
        /* EPUB authors occasionally use different case than the OPF. */
        mz_uint n = mz_zip_reader_get_num_files(zip);
        for (mz_uint i = 0; i < n; i++) {
            char name[EG_MAX_PATH];
            if (mz_zip_reader_get_filename(zip, i, name, sizeof(name)) > 0 &&
                eg_stricmp(name, full_path) == 0) {
                idx = (int)i;
                break;
            }
        }
    }
    if (idx < 0) return 0;
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(zip, (mz_uint)idx, &st)) return 0;
    if (st.m_is_encrypted) {
        ep->has_encryption = 1;
        return 0;
    }
    *out = (uint8_t *)mz_zip_reader_extract_to_heap(zip, (mz_uint)idx, out_size, 0);
    return *out != NULL;
}

int eg_epub_read_rel(EgEpub *ep, const char *base_dir, const char *href,
                     uint8_t **out, size_t *out_size)
{
    char path[EG_MAX_PATH];
    eg_path_normalize(path, sizeof(path), base_dir, href);
    return eg_epub_read(ep, path, out, out_size);
}

int eg_epub_chapter_index(const EgEpub *ep, const char *full_path)
{
    if (!ep || !full_path) return -1;
    for (int i = 0; i < ep->spine_count; i++)
        if (ep->spine[i].full_path && strcmp(ep->spine[i].full_path, full_path) == 0)
            return i;
    return -1;
}

const char *eg_epub_chapter_href(const EgEpub *ep, int chapter)
{
    if (!ep || chapter < 0 || chapter >= ep->spine_count) return NULL;
    return ep->spine[chapter].full_path;
}

const char *eg_epub_chapter_title(const EgEpub *ep, int chapter)
{
    if (!ep) return NULL;
    for (int i = 0; i < ep->toc_count; i++)
        if (ep->toc[i].chapter == chapter && ep->toc[i].title && ep->toc[i].title[0])
            return ep->toc[i].title;
    return NULL;
}

int eg_epub_toc_chapter(const EgEpub *ep, int toc_index)
{
    if (!ep || toc_index < 0 || toc_index >= ep->toc_count) return -1;
    return ep->toc[toc_index].chapter;
}

int eg_epub_find_toc_for_chapter(const EgEpub *ep, int chapter)
{
    if (!ep) return -1;
    for (int i = 0; i < ep->toc_count; i++)
        if (ep->toc[i].chapter == chapter) return i;
    return -1;
}

static void parse_metadata(EgEpub *ep, const EgXmlNode *package)
{
    const EgXmlNode *meta = eg_xml_child(package, "metadata");
    if (!meta) return;
    for (const EgXmlNode *c = meta->children; c; c = c->next) {
        if (!c->tag) continue;
        char *text = eg_xml_text(c);
        char *trimmed = eg_trim(text);
        if (strcmp(c->tag, "title") == 0 && !ep->title[0])
            snprintf(ep->title, sizeof(ep->title), "%s", trimmed);
        else if (strcmp(c->tag, "creator") == 0 && !ep->author[0])
            snprintf(ep->author, sizeof(ep->author), "%s", trimmed);
        else if (strcmp(c->tag, "language") == 0 && !ep->language[0])
            snprintf(ep->language, sizeof(ep->language), "%s", trimmed);
        else if (strcmp(c->tag, "identifier") == 0 && !ep->identifier[0])
            snprintf(ep->identifier, sizeof(ep->identifier), "%s", trimmed);
        free(text);
    }
}

static int parse_manifest(EgEpub *ep, const EgXmlNode *package)
{
    const EgXmlNode *man = eg_xml_child(package, "manifest");
    if (!man) return 0;
    for (const EgXmlNode *item = man->children; item; item = item->next) {
        if (!item->tag || strcmp(item->tag, "item") != 0) continue;
        const char *id = eg_xml_attr(item, "id");
        const char *href = eg_xml_attr(item, "href");
        if (!href) continue;
        EgManifestItem *m = eg_realloc(ep->manifest,
                                       sizeof(EgManifestItem) * (size_t)(ep->manifest_count + 1));
        if (!m) return 0;
        ep->manifest = m;
        EgManifestItem *mi = &ep->manifest[ep->manifest_count++];
        memset(mi, 0, sizeof(*mi));
        mi->id = eg_strdup(id ? id : "");
        mi->href = eg_strdup(href);
        char full[EG_MAX_PATH];
        eg_path_normalize(full, sizeof(full), ep->root, href);
        mi->full_path = eg_strdup(full);
        mi->media_type = eg_strdup(eg_xml_attr(item, "media-type"));
        mi->properties = eg_strdup(eg_xml_attr(item, "properties"));
    }
    return ep->manifest_count > 0;
}

static int parse_spine(EgEpub *ep, const EgXmlNode *package)
{
    const EgXmlNode *spine = eg_xml_child(package, "spine");
    if (!spine) return 0;
    for (const EgXmlNode *ref = spine->children; ref; ref = ref->next) {
        if (!ref->tag || strcmp(ref->tag, "itemref") != 0) continue;
        const char *idref = eg_xml_attr(ref, "idref");
        const EgManifestItem *item = manifest_by_id(ep, idref);
        if (!item) continue;
        EgSpineItem *s = eg_realloc(ep->spine,
                                    sizeof(EgSpineItem) * (size_t)(ep->spine_count + 1));
        if (!s) return 0;
        ep->spine = s;
        ep->spine[ep->spine_count].full_path = eg_strdup(item->full_path);
        ep->spine_count++;
    }
    return ep->spine_count > 0;
}

static void resolve_cover_item(EgEpub *ep, const EgXmlNode *package)
{
    const EgXmlNode *meta = eg_xml_child(package, "metadata");
    if (meta) {
        for (const EgXmlNode *c = meta->children; c; c = c->next) {
            if (!c->tag || strcmp(c->tag, "meta") != 0) continue;
            const char *name = eg_xml_attr(c, "name");
            const char *content = eg_xml_attr(c, "content");
            if (name && content && strcmp(name, "cover") == 0) {
                const EgManifestItem *item = manifest_by_id(ep, content);
                if (item) {
                    snprintf(ep->cover_path, sizeof(ep->cover_path), "%s", item->full_path);
                    return;
                }
            }
        }
    }
    for (int i = 0; i < ep->manifest_count; i++) {
        if (ep->manifest[i].properties && strstr(ep->manifest[i].properties, "cover-image")) {
            snprintf(ep->cover_path, sizeof(ep->cover_path), "%s", ep->manifest[i].full_path);
            return;
        }
    }
    const EgXmlNode *guide = eg_xml_child(package, "guide");
    if (guide) {
        for (const EgXmlNode *r = guide->children; r; r = r->next) {
            if (!r->tag || strcmp(r->tag, "reference") != 0) continue;
            const char *type = eg_xml_attr(r, "type");
            const char *href = eg_xml_attr(r, "href");
            if (!type || !href || eg_stricmp(type, "cover") != 0) continue;
            char page[EG_MAX_PATH];
            eg_path_normalize(page, sizeof(page), ep->root, href);
            uint8_t *data = NULL;
            size_t size = 0;
            if (!eg_epub_read(ep, page, &data, &size)) continue;
            EgXmlNode *doc = eg_xml_parse((const char *)data, (int)size);
            free(data);
            if (doc) {
                const EgXmlNode *img = eg_xml_find(doc, "img");
                if (img) {
                    const char *src = eg_xml_attr(img, "src");
                    if (src) {
                        char dir[EG_MAX_PATH];
                        eg_path_dirname(page, dir, sizeof(dir));
                        eg_path_normalize(ep->cover_path, sizeof(ep->cover_path), dir, src);
                    }
                }
                eg_xml_free(doc);
            }
            if (ep->cover_path[0]) return;
        }
    }
    /* Last resort: first manifest image. */
    for (int i = 0; i < ep->manifest_count; i++) {
        const char *mt = ep->manifest[i].media_type;
        if (mt && strncmp(mt, "image/", 6) == 0 &&
            !strstr(ep->manifest[i].href, "logo")) {
            snprintf(ep->cover_path, sizeof(ep->cover_path), "%s", ep->manifest[i].full_path);
            return;
        }
    }
}

static void walk_nav_list(EgEpub *ep, const EgXmlNode *list,
                          const char *base_dir, int level)
{
    if (!list) return;
    for (const EgXmlNode *li = list->children; li; li = li->next) {
        if (!li->tag || strcmp(li->tag, "li") != 0) continue;
        const EgXmlNode *link = NULL;
        for (const EgXmlNode *c = li->children; c; c = c->next) {
            if (c->tag && (strcmp(c->tag, "a") == 0 || strcmp(c->tag, "span") == 0)) {
                link = c;
                break;
            }
        }
        if (link) {
            char *title = eg_xml_text(link);
            char *t = eg_trim(title);
            const char *href = eg_xml_attr(link, "href");
            if (t[0] || href)
                add_toc(ep, t, href, base_dir, level);
            free(title);
        }
        for (const EgXmlNode *c = li->children; c; c = c->next) {
            if (c->tag && (strcmp(c->tag, "ol") == 0 || strcmp(c->tag, "ul") == 0))
                walk_nav_list(ep, c, base_dir, level + 1);
        }
    }
}

static int parse_nav_toc(EgEpub *ep)
{
    const EgManifestItem *nav_item = NULL;
    for (int i = 0; i < ep->manifest_count; i++) {
        if (ep->manifest[i].properties && strstr(ep->manifest[i].properties, "nav")) {
            nav_item = &ep->manifest[i];
            break;
        }
    }
    if (!nav_item) return 0;
    uint8_t *data = NULL;
    size_t size = 0;
    if (!eg_epub_read(ep, nav_item->full_path, &data, &size)) return 0;
    EgXmlNode *doc = eg_xml_parse((const char *)data, (int)size);
    free(data);
    if (!doc) return 0;
    const EgXmlNode *nav = NULL;
    /* Prefer nav[type=toc], but tolerate missing namespace attributes. */
    const EgXmlNode *body = eg_xml_find(doc, "body");
    if (body) {
        for (const EgXmlNode *c = body->children; c; c = c->next) {
            if (c->tag && strcmp(c->tag, "nav") == 0) {
                const char *type = eg_xml_attr(c, "type");
                if (type && strstr(type, "toc")) { nav = c; break; }
                if (!nav) nav = c;
            }
        }
    }
    if (!nav) nav = eg_xml_find(doc, "nav");
    if (!nav) { eg_xml_free(doc); return 0; }
    const EgXmlNode *list = eg_xml_child(nav, "ol");
    if (!list) list = eg_xml_child(nav, "ul");
    if (!list) list = eg_xml_find(nav, "ol");
    char dir[EG_MAX_PATH];
    eg_path_dirname(nav_item->full_path, dir, sizeof(dir));
    if (list) walk_nav_list(ep, list, dir, 0);
    eg_xml_free(doc);
    return ep->toc_count > 0;
}

static void walk_ncx_points(EgEpub *ep, const EgXmlNode *parent,
                            const char *base_dir, int level)
{
    for (const EgXmlNode *np = parent->children; np; np = np->next) {
        if (!np->tag || strcmp(np->tag, "navpoint") != 0) continue;
        const EgXmlNode *label = eg_xml_child(np, "navlabel");
        const EgXmlNode *content = eg_xml_child(np, "content");
        const EgXmlNode *text = label ? eg_xml_child(label, "text") : NULL;
        if (content) {
            const char *src = eg_xml_attr(content, "src");
            char *title = text ? eg_xml_text(text) : NULL;
            char *t = title ? eg_trim(title) : NULL;
            add_toc(ep, t && *t ? t : "Bez tytułu", src, base_dir, level);
            free(title);
        }
        walk_ncx_points(ep, np, base_dir, level + 1);
    }
}

static int parse_ncx_toc(EgEpub *ep, const EgXmlNode *package)
{
    const EgXmlNode *spine = eg_xml_child(package, "spine");
    const char *toc_id = spine ? eg_xml_attr(spine, "toc") : NULL;
    const EgManifestItem *item = manifest_by_id(ep, toc_id);
    if (!item) {
        for (int i = 0; i < ep->manifest_count; i++) {
            if (ep->manifest[i].href && eg_has_suffix_ci(ep->manifest[i].href, ".ncx")) {
                item = &ep->manifest[i];
                break;
            }
        }
    }
    if (!item) return 0;
    uint8_t *data = NULL;
    size_t size = 0;
    if (!eg_epub_read(ep, item->full_path, &data, &size)) return 0;
    EgXmlNode *doc = eg_xml_parse((const char *)data, (int)size);
    free(data);
    if (!doc) return 0;
    const EgXmlNode *map = eg_xml_find(doc, "navmap");
    char dir[EG_MAX_PATH];
    eg_path_dirname(item->full_path, dir, sizeof(dir));
    if (map) walk_ncx_points(ep, map, dir, 0);
    eg_xml_free(doc);
    return ep->toc_count > 0;
}

int eg_epub_load_cover(EgEpub *ep, uint8_t **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;
    if (!ep || !ep->cover_path[0]) return 0;
    return eg_epub_read(ep, ep->cover_path, out, out_size);
}

int eg_epub_open(EgEpub *ep, const char *path)
{
    memset(ep, 0, sizeof(*ep));
    snprintf(ep->path, sizeof(ep->path), "%s", path ? path : "");
    mz_zip_archive *zip = (mz_zip_archive *)eg_calloc(1, sizeof(mz_zip_archive));
    if (!zip) return 0;
    if (!mz_zip_reader_init_file(zip, path, 0)) {
        eg_log("epub: cannot open zip %s\n", path ? path : "(null)");
        free(zip);
        return 0;
    }
    ep->zip = zip;
    ep->is_open = 1;

    /* DRM / obfuscation marker. */
    uint8_t *enc = NULL;
    size_t enc_size = 0;
    if (eg_epub_read(ep, "META-INF/encryption.xml", &enc, &enc_size)) {
        ep->has_encryption = 1;
        free(enc);
    }

    uint8_t *container = NULL;
    size_t container_size = 0;
    if (!eg_epub_read(ep, "META-INF/container.xml", &container, &container_size)) {
        eg_log("epub: no container.xml in %s\n", path ? path : "");
        eg_epub_close(ep);
        return 0;
    }
    EgXmlNode *cdoc = eg_xml_parse((const char *)container, (int)container_size);
    free(container);
    if (!cdoc) { eg_epub_close(ep); return 0; }
    const EgXmlNode *rf = eg_xml_find(cdoc, "rootfile");
    const char *fullpath = rf ? eg_xml_attr(rf, "full-path") : NULL;
    if (!fullpath || !*fullpath) {
        eg_xml_free(cdoc);
        eg_epub_close(ep);
        return 0;
    }
    snprintf(ep->opf_path, sizeof(ep->opf_path), "%s", fullpath);
    eg_xml_free(cdoc);
    eg_path_dirname(ep->opf_path, ep->root, sizeof(ep->root));

    uint8_t *opf = NULL;
    size_t opf_size = 0;
    if (!eg_epub_read(ep, ep->opf_path, &opf, &opf_size)) {
        eg_epub_close(ep);
        return 0;
    }
    EgXmlNode *doc = eg_xml_parse((const char *)opf, (int)opf_size);
    free(opf);
    if (!doc) { eg_epub_close(ep); return 0; }
    const EgXmlNode *package = eg_xml_child(doc, "package");
    if (!package) package = eg_xml_find(doc, "package");
    if (!package || !parse_manifest(ep, package) || !parse_spine(ep, package)) {
        eg_xml_free(doc);
        eg_epub_close(ep);
        return 0;
    }
    parse_metadata(ep, package);
    resolve_cover_item(ep, package);
    if (!parse_nav_toc(ep)) {
        free_toc(ep);
        parse_ncx_toc(ep, package);
    }
    if (ep->toc_count == 0) {
        for (int i = 0; i < ep->spine_count; i++) {
            const char *href = ep->spine[i].full_path ? ep->spine[i].full_path : "";
            add_toc(ep, "", href, "", 0);
        }
    }
    eg_xml_free(doc);

    if (!ep->title[0]) {
        const char *base = strrchr(path ? path : "", '/');
        base = base ? base + 1 : (path ? path : "EPUB");
        snprintf(ep->title, sizeof(ep->title), "%s", base);
        char *dot = strrchr(ep->title, '.');
        if (dot) *dot = 0;
    }
    if (!ep->author[0]) snprintf(ep->author, sizeof(ep->author), "Autor nieznany");
    return 1;
}

void eg_epub_close(EgEpub *ep)
{
    if (!ep) return;
    if (ep->zip) {
        mz_zip_archive *zip = (mz_zip_archive *)ep->zip;
        mz_zip_reader_end(zip);
        free(zip);
        ep->zip = NULL;
    }
    free_manifest(ep);
    free_spine(ep);
    free_toc(ep);
    ep->is_open = 0;
}
