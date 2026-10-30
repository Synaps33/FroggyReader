#ifndef EG_EPUB_H
#define EG_EPUB_H

#include "util.h"
#include "xml.h"

typedef struct {
    char *id;
    char *href;         /* relative to OPF */
    char *full_path;    /* path inside the ZIP */
    char *media_type;
    char *properties;
} EgManifestItem;

typedef struct {
    char *full_path;
} EgSpineItem;

typedef struct {
    char *title;
    char *href;         /* full path inside ZIP */
    char *fragment;
    int level;
    int chapter;
} EgTocEntry;

typedef struct {
    void *zip;          /* mz_zip_archive */
    char path[EG_MAX_PATH];
    char root[EG_MAX_PATH];
    char opf_path[EG_MAX_PATH];
    char title[256];
    char author[160];
    char language[24];
    char identifier[128];
    EgManifestItem *manifest;
    int manifest_count;
    EgSpineItem *spine;
    int spine_count;
    EgTocEntry *toc;
    int toc_count;
    char cover_path[EG_MAX_PATH];
    int has_encryption;
    int is_open;
} EgEpub;

int eg_epub_open(EgEpub *ep, const char *path);
void eg_epub_close(EgEpub *ep);
int eg_epub_read(EgEpub *ep, const char *full_path, uint8_t **out, size_t *out_size);
int eg_epub_read_rel(EgEpub *ep, const char *base_dir, const char *href,
                     uint8_t **out, size_t *out_size);
const char *eg_epub_chapter_title(const EgEpub *ep, int chapter);
int eg_epub_chapter_index(const EgEpub *ep, const char *full_path);
int eg_epub_toc_chapter(const EgEpub *ep, int toc_index);
int eg_epub_find_toc_for_chapter(const EgEpub *ep, int chapter);
int eg_epub_load_cover(EgEpub *ep, uint8_t **out, size_t *out_size);
const char *eg_epub_chapter_href(const EgEpub *ep, int chapter);

#endif
