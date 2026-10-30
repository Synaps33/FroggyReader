#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef SF2000
extern int fs_sync(const char *path);
#endif

static char g_log_dir[EG_MAX_PATH];

void *eg_malloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) eg_log("OUT OF MEMORY (%u bytes)\n", (unsigned)n);
    return p;
}

void *eg_calloc(size_t n, size_t sz)
{
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) eg_log("OUT OF MEMORY (calloc)\n");
    return p;
}

void *eg_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) eg_log("OUT OF MEMORY (realloc %u)\n", (unsigned)n);
    return q;
}

char *eg_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = eg_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *eg_strndup(const char *s, int n)
{
    if (!s) return NULL;
    if (n < 0) n = 0;
    char *p = eg_malloc((size_t)n + 1);
    if (p) {
        memcpy(p, s, (size_t)n);
        p[n] = 0;
    }
    return p;
}

void eg_str_init(EgStr *s)
{
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

void eg_str_free(EgStr *s)
{
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

void eg_str_clear(EgStr *s)
{
    s->len = 0;
    if (s->data) s->data[0] = 0;
}

int eg_str_reserve(EgStr *s, int extra)
{
    int need = s->len + extra + 1;
    if (need <= s->cap) return 1;
    int cap = s->cap ? s->cap : 64;
    while (cap < need) cap += cap / 2;
    char *p = eg_realloc(s->data, (size_t)cap);
    if (!p) return 0;
    s->data = p;
    s->cap = cap;
    return 1;
}

int eg_str_append(EgStr *s, const char *text, int len)
{
    if (len <= 0) {
        if (!s->data && !eg_str_reserve(s, 0)) return 0;
        return 1;
    }
    if (!eg_str_reserve(s, len)) return 0;
    memcpy(s->data + s->len, text, (size_t)len);
    s->len += len;
    s->data[s->len] = 0;
    return 1;
}

int eg_str_append_cstr(EgStr *s, const char *text)
{
    return eg_str_append(s, text, (int)strlen(text));
}

int eg_str_append_char(EgStr *s, char c)
{
    return eg_str_append(s, &c, 1);
}

int eg_str_appendf(EgStr *s, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return 0;
    }
    if (!eg_str_reserve(s, n)) {
        va_end(ap2);
        return 0;
    }
    vsnprintf(s->data + s->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    s->len += n;
    return 1;
}

char *eg_str_steal(EgStr *s)
{
    char *p = s->data;
    if (!p) p = eg_strdup("");
    s->data = NULL;
    s->len = s->cap = 0;
    return p;
}

int eg_utf8_decode(const char **pp, uint32_t *out)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint32_t cp;
    int n;
    if (!p || !*p) return 0;
    if (p[0] < 0x80) {
        cp = p[0];
        n = 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        n = 3;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        n = 4;
    } else {
        *pp += 1;
        *out = 0xFFFD;
        return 1;
    }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *pp += i;
            *out = 0xFFFD;
            return i;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *pp += n;
    *out = cp;
    return n;
}

int eg_utf8_decode_at(const char *s, int len, int pos, uint32_t *cp, int *next)
{
    if (pos < 0 || pos >= len) {
        *cp = 0;
        *next = pos;
        return 0;
    }
    const char *p = s + pos;
    int n = eg_utf8_decode(&p, cp);
    *next = (int)(p - s);
    return n;
}

int eg_utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int eg_utf8_len(const char *s)
{
    int n = 0;
    while (s && *s) {
        const char *p = s;
        uint32_t cp;
        eg_utf8_decode(&p, &cp);
        s = (const char *)p;
        n++;
    }
    return n;
}

int eg_utf8_is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
           cp == 0xA0 || cp == 0x2000 || cp == 0x2001 || cp == 0x2002 ||
           cp == 0x2003 || cp == 0x2004 || cp == 0x2005 || cp == 0x2006 ||
           cp == 0x2007 || cp == 0x2008 || cp == 0x2009 || cp == 0x200A ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

int eg_utf8_is_break_after(uint32_t cp)
{
    return cp == '-' || cp == 0x2010 || cp == 0x2011 || cp == 0x2012 ||
           cp == 0x00AD || cp == 0x2044 || cp == '/' || cp == 0x2013 ||
           cp == 0x2014;
}

static uint32_t lower_latin(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 32;
    if (cp >= 0x0100 && cp <= 0x017F) {
        if (cp == 0x0130) return 'i';
        if (cp == 0x0131) return cp;
        if ((cp & 1) == 0) return cp + 1;
    }
    if (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 32;
    if (cp >= 0x0400 && cp <= 0x040F) return cp + 80;
    return cp;
}

static uint32_t upper_latin(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) return cp - 32;
    if (cp >= 0x0100 && cp <= 0x017F) {
        if (cp == 0x0131) return 'I';
        if ((cp & 1) == 1) return cp - 1;
    }
    if (cp >= 0x03B1 && cp <= 0x03C9 && cp != 0x03C2) return cp - 32;
    if (cp >= 0x0430 && cp <= 0x044F) return cp - 32;
    if (cp >= 0x0450 && cp <= 0x045F) return cp - 80;
    return cp;
}

uint32_t eg_utf8_tolower(uint32_t cp) { return lower_latin(cp); }
uint32_t eg_utf8_toupper(uint32_t cp) { return upper_latin(cp); }

int eg_stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int eg_strnieq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
    return 1;
}

int eg_has_suffix_ci(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;
    size_t a = strlen(s), b = strlen(suffix);
    if (b > a) return 0;
    return eg_stricmp(s + a - b, suffix) == 0;
}

int eg_atoi_len(const char *s, int len)
{
    int v = 0, sign = 1;
    int i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    if (i < len && (s[i] == '-' || s[i] == '+')) {
        if (s[i] == '-') sign = -1;
        i++;
    }
    while (i < len && isdigit((unsigned char)s[i])) {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    return v * sign;
}

uint32_t eg_hash32(const char *s)
{
    uint32_t h = 2166136261u;
    while (s && *s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

char *eg_trim(char *s)
{
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void append_cp(EgStr *out, uint32_t cp)
{
    char buf[4];
    int n = eg_utf8_encode(cp, buf);
    eg_str_append(out, buf, n);
}

static uint32_t entity_cp(const char *name, int len)
{
    struct { const char *n; uint32_t cp; } tab[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0xA0}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026},
        {"laquo", 0xAB}, {"raquo", 0xBB}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"bull", 0x2022}, {"middot", 0xB7},
        {"times", 0xD7}, {"divide", 0xF7}, {"copy", 0xA9}, {"reg", 0xAE},
        {"trade", 0x2122}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5},
        {"cent", 0xA2}, {"deg", 0xB0}, {"plusmn", 0xB1}, {"frac12", 0xBD},
        {"frac14", 0xBC}, {"frac34", 0xBE}, {"sup1", 0xB9}, {"sup2", 0xB2},
        {"sup3", 0xB3}, {"micro", 0xB5}, {"para", 0xB6}, {"sect", 0xA7},
        {"szlig", 0xDF}, {"agrave", 0xE0}, {"aacute", 0xE1}, {"eacute", 0xE9},
        {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"ccedil", 0xE7},
        {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Iacute", 0xCD},
        {"Oacute", 0xD3}, {"Uacute", 0xDA}, {"Ccedil", 0xC7},
        {"larr", 0x2190}, {"uarr", 0x2191}, {"rarr", 0x2192}, {"darr", 0x2193},
        {"minus", 0x2212}, {"le", 0x2264}, {"ge", 0x2265}, {"ne", 0x2260},
        {"shy", 0x00AD}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009},
        {NULL, 0}
    };
    for (int i = 0; tab[i].n; i++) {
        int n = (int)strlen(tab[i].n);
        if (n == len && memcmp(tab[i].n, name, (size_t)len) == 0)
            return tab[i].cp;
    }
    return 0;
}

int eg_decode_entities(const char *src, int len, EgStr *out)
{
    int i = 0;
    if (!src) return 0;
    while (i < len) {
        if (src[i] != '&') {
            int start = i;
            while (i < len && src[i] != '&') i++;
            if (!eg_str_append(out, src + start, i - start)) return 0;
            continue;
        }
        int semi = -1;
        for (int j = i + 1; j < len && j < i + 16; j++) {
            if (src[j] == ';') { semi = j; break; }
            if (!isalnum((unsigned char)src[j]) && src[j] != '#') break;
        }
        if (semi < 0) {
            if (!eg_str_append_char(out, src[i++])) return 0;
            continue;
        }
        int n = semi - i - 1;
        if (n > 1 && src[i + 1] == '#') {
            uint32_t cp = 0;
            int hex = (n > 2 && (src[i + 2] == 'x' || src[i + 2] == 'X'));
            int p = i + 2 + hex;
            while (p < semi) {
                int d = src[p];
                if (isdigit(d)) d -= '0';
                else if (hex && d >= 'a' && d <= 'f') d = d - 'a' + 10;
                else if (hex && d >= 'A' && d <= 'F') d = d - 'A' + 10;
                else break;
                cp = cp * (hex ? 16 : 10) + (uint32_t)d;
                p++;
            }
            if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
            append_cp(out, cp);
        } else {
            uint32_t cp = entity_cp(src + i + 1, n);
            if (cp) append_cp(out, cp);
            else if (!eg_str_append(out, src + i, semi - i + 1)) return 0;
        }
        i = semi + 1;
    }
    return 1;
}

void eg_set_log_dir(const char *dir)
{
    snprintf(g_log_dir, sizeof(g_log_dir), "%s", dir ? dir : "");
}

void eg_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
    fwrite(line, 1, (size_t)n, stderr);
    fflush(stderr);
#ifdef SF2000
    const char *paths[2] = {
        "/mnt/sda1/ROMS/epub/epub.log",
        "/mnt/sda1/system/logs/epub.log"
    };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "a");
        if (f) {
            fwrite(line, 1, (size_t)n, f);
            fclose(f);
            fs_sync(paths[i]);
        }
    }
#else
    if (g_log_dir[0]) {
        char path[EG_MAX_PATH + 16];
        snprintf(path, sizeof(path), "%s/epub.log", g_log_dir);
        FILE *f = fopen(path, "a");
        if (f) {
            fwrite(line, 1, (size_t)n, f);
            fclose(f);
        }
    }
#endif
}

char *eg_path_dirname(const char *path, char *out, size_t out_size)
{
    if (!path) path = "";
    const char *slash = strrchr(path, '/');
    if (!slash) snprintf(out, out_size, ".");
    else if (slash == path) snprintf(out, out_size, "/");
    else {
        size_t n = (size_t)(slash - path);
        if (n >= out_size) n = out_size - 1;
        memcpy(out, path, n);
        out[n] = 0;
    }
    return out;
}

int eg_path_join(char *out, size_t out_size, const char *dir, const char *name)
{
    if (!name) name = "";
    if (name[0] == '/' || !dir || !dir[0] || (dir[0] == '.' && dir[1] == 0))
        snprintf(out, out_size, "%s", name);
    else
        snprintf(out, out_size, "%s/%s", dir, name);
    return (int)strlen(out);
}

int eg_path_normalize(char *out, size_t out_size, const char *base_dir, const char *href)
{
    char tmp[EG_MAX_PATH * 2];
    char combined[EG_MAX_PATH * 4];
    if (!href) href = "";
    const char *frag = strchr(href, '#');
    int hlen = frag ? (int)(frag - href) : (int)strlen(href);
    const char *q = strchr(href, '?');
    if (q && (!frag || q < frag)) hlen = (int)(q - href);
    if (hlen >= (int)sizeof(tmp)) hlen = (int)sizeof(tmp) - 1;
    memcpy(tmp, href, (size_t)hlen);
    tmp[hlen] = 0;
    eg_url_decode(tmp);
    if (tmp[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", tmp + 1);
    } else if (base_dir && base_dir[0]) {
        if (strcmp(base_dir, ".") == 0)
            snprintf(combined, sizeof(combined), "%s", tmp);
        else
            snprintf(combined, sizeof(combined), "%s/%s", base_dir, tmp);
    } else {
        snprintf(combined, sizeof(combined), "%s", tmp);
    }
    /* collapse . and .. components */
    char parts[64][128];
    int count = 0;
    char *p = combined;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *e = strchr(p, '/');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n == 1 && p[0] == '.') {
            /* skip */
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (count > 0) count--;
        } else if (n > 0) {
            if (n > 127) n = 127;
            memcpy(parts[count], p, (size_t)n);
            parts[count][n] = 0;
            count++;
            if (count >= 64) break;
        }
        if (!e) break;
        p = e + 1;
    }
    out[0] = 0;
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        int n = snprintf(out + used, used < out_size ? out_size - used : 0, "%s%s",
                         i ? "/" : "", parts[i]);
        if (n < 0) break;
        used += (size_t)n;
        if (used >= out_size) break;
    }
    if (used == 0) snprintf(out, out_size, "%s", combined[0] ? combined : "");
    return (int)strlen(out);
}

int eg_url_decode(char *s)
{
    char *w = s, *r = s;
    while (*r) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            int hi = r[1], lo = r[2];
            hi = isdigit(hi) ? hi - '0' : (tolower(hi) - 'a' + 10);
            lo = isdigit(lo) ? lo - '0' : (tolower(lo) - 'a' + 10);
            *w++ = (char)((hi << 4) | lo);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
    return (int)(w - s);
}

int eg_path_is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#ifdef S_ISDIR
    return S_ISDIR(st.st_mode);
#else
    return (st.st_mode & S_IFMT) == S_IFDIR;
#endif
}

int eg_path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

long eg_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

uint8_t *eg_read_file(const char *path, size_t *out_size)
{
    size_t cap = 4096, len = 0;
    uint8_t *buf = eg_malloc(cap);
    if (!buf) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { free(buf); return NULL; }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            uint8_t *nb = eg_realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) break;
    }
    fclose(f);
    if (out_size) *out_size = len;
    if (len == 0) {
        uint8_t *p = eg_realloc(buf, 1);
        if (p) buf = p;
    }
    return buf;
}

int eg_write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = 1;
    if (size && fwrite(data, 1, size, f) != size) ok = 0;
    if (fclose(f) != 0) ok = 0;
#ifdef SF2000
    if (ok) fs_sync(path);
#endif
    return ok;
}

int eg_mkdir_p(const char *path)
{
    char tmp[EG_MAX_PATH];
    if (!path || !*path) return 0;
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') tmp[--n] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!eg_path_exists(tmp)) {
#ifdef SF2000
                if (mkdir(tmp, 0755) != 0) { *p = '/'; return 0; }
#else
                if (mkdir(tmp, 0755) != 0) { *p = '/'; return 0; }
#endif
            }
            *p = '/';
        }
    }
    if (!eg_path_exists(tmp)) {
        if (mkdir(tmp, 0755) != 0) return 0;
    }
    return 1;
}

uint16_t eg_rgb565(int r, int g, int b)
{
    r = eg_clampi(r, 0, 255);
    g = eg_clampi(g, 0, 255);
    b = eg_clampi(b, 0, 255);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t eg_blend565(uint16_t dst, uint16_t src, int alpha)
{
    if (alpha <= 0) return dst;
    if (alpha >= 255) return src;
    int dr = (dst >> 11) & 31, dg = (dst >> 5) & 63, db = dst & 31;
    int sr = (src >> 11) & 31, sg = (src >> 5) & 63, sb = src & 31;
    int r = (dr * (255 - alpha) + sr * alpha) / 255;
    int g = (dg * (255 - alpha) + sg * alpha) / 255;
    int b = (db * (255 - alpha) + sb * alpha) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

int eg_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int eg_mini(int a, int b) { return a < b ? a : b; }
int eg_maxi(int a, int b) { return a > b ? a : b; }
