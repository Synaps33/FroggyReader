#ifndef EG_UTIL_H
#define EG_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EG_W 320
#define EG_H 240
#define EG_FP 1024

#ifndef EG_MAX_PATH
#define EG_MAX_PATH 512
#endif

typedef struct {
    char *data;
    int len;
    int cap;
} EgStr;

void eg_str_init(EgStr *s);
void eg_str_free(EgStr *s);
void eg_str_clear(EgStr *s);
int eg_str_reserve(EgStr *s, int extra);
int eg_str_append(EgStr *s, const char *text, int len);
int eg_str_append_cstr(EgStr *s, const char *text);
int eg_str_append_char(EgStr *s, char c);
int eg_str_appendf(EgStr *s, const char *fmt, ...);
char *eg_str_steal(EgStr *s);

void *eg_malloc(size_t n);
void *eg_calloc(size_t n, size_t sz);
void *eg_realloc(void *p, size_t n);
char *eg_strdup(const char *s);
char *eg_strndup(const char *s, int n);

int eg_utf8_decode(const char **pp, uint32_t *out);
int eg_utf8_decode_at(const char *s, int len, int pos, uint32_t *cp, int *next);
int eg_utf8_encode(uint32_t cp, char out[4]);
int eg_utf8_len(const char *s);
int eg_utf8_is_space(uint32_t cp);
int eg_utf8_is_break_after(uint32_t cp);
uint32_t eg_utf8_tolower(uint32_t cp);
uint32_t eg_utf8_toupper(uint32_t cp);

int eg_stricmp(const char *a, const char *b);
int eg_strnieq(const char *a, const char *b, int n);
int eg_has_suffix_ci(const char *s, const char *suffix);
int eg_atoi_len(const char *s, int len);
uint32_t eg_hash32(const char *s);

char *eg_trim(char *s);
int eg_decode_entities(const char *src, int len, EgStr *out);

void eg_log(const char *fmt, ...);
void eg_set_log_dir(const char *dir);

char *eg_path_dirname(const char *path, char *out, size_t out_size);
int eg_path_join(char *out, size_t out_size, const char *dir, const char *name);
int eg_path_normalize(char *out, size_t out_size, const char *base_dir, const char *href);
int eg_url_decode(char *s);
int eg_path_is_dir(const char *path);
int eg_path_exists(const char *path);
long eg_file_size(const char *path);
uint8_t *eg_read_file(const char *path, size_t *out_size);
int eg_write_file(const char *path, const void *data, size_t size);
int eg_mkdir_p(const char *path);

uint16_t eg_rgb565(int r, int g, int b);
uint16_t eg_blend565(uint16_t dst, uint16_t src, int alpha);
int eg_clampi(int v, int lo, int hi);
int eg_mini(int a, int b);
int eg_maxi(int a, int b);

#endif
