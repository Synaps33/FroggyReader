#include "image.h"

#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

int eg_image_info(const uint8_t *data, size_t size, int *w, int *h)
{
    int comp = 0;
    if (!data || size < 8) return 0;
    if (!stbi_info_from_memory(data, (int)size, w, h, &comp)) return 0;
    return *w > 0 && *h > 0;
}

static uint8_t *decode_rgba(const uint8_t *data, size_t size, int *w, int *h)
{
    int comp = 0;
    stbi_uc *p = stbi_load_from_memory(data, (int)size, w, h, &comp, 4);
    return (uint8_t *)p;
}

static void sample_area(const uint8_t *src, int sw, int sh, int x, int y,
                        int dw, int dh, int *or_, int *og, int *ob, int *oa)
{
    int sx0 = x * sw / dw;
    int sx1 = (x + 1) * sw / dw;
    int sy0 = y * sh / dh;
    int sy1 = (y + 1) * sh / dh;
    if (sx1 <= sx0) sx1 = sx0 + 1;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    int w = sx1 - sx0, h = sy1 - sy0;
    int stepx = w > 4 ? (w + 3) / 4 : 1;
    int stepy = h > 4 ? (h + 3) / 4 : 1;
    int r = 0, g = 0, b = 0, a = 0, n = 0;
    for (int sy = sy0; sy < sy1; sy += stepy) {
        const uint8_t *row = src + ((size_t)sy * sw) * 4;
        for (int sx = sx0; sx < sx1; sx += stepx) {
            const uint8_t *p = row + (size_t)sx * 4;
            r += p[0]; g += p[1]; b += p[2]; a += p[3];
            n++;
        }
    }
    if (n <= 0) n = 1;
    *or_ = r / n; *og = g / n; *ob = b / n; *oa = a / n;
}

static int scale_rgba_to565(const uint8_t *src, int sw, int sh,
                            int dw, int dh, uint16_t bg, EgImage *out)
{
    if (dw <= 0 || dh <= 0) return 0;
    uint16_t *px = eg_malloc(sizeof(uint16_t) * (size_t)dw * (size_t)dh);
    if (!px) return 0;
    int br = (bg >> 11) & 31, bgg = (bg >> 5) & 63, bb = bg & 31;
    br = (br << 3) | (br >> 2);
    bgg = (bgg << 2) | (bgg >> 4);
    bb = (bb << 3) | (bb >> 2);
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            int r, g, b, a;
            sample_area(src, sw, sh, x, y, dw, dh, &r, &g, &b, &a);
            if (a < 255) {
                r = (r * a + br * (255 - a)) / 255;
                g = (g * a + bgg * (255 - a)) / 255;
                b = (b * a + bb * (255 - a)) / 255;
            }
            px[(size_t)y * dw + x] = eg_rgb565(r, g, b);
        }
    }
    out->w = dw;
    out->h = dh;
    out->pixels = px;
    return 1;
}

int eg_image_decode_to(const uint8_t *data, size_t size, int out_w, int out_h,
                       uint16_t bg, EgImage *out)
{
    memset(out, 0, sizeof(*out));
    if (out_w <= 0 || out_h <= 0) return 0;
    int sw = 0, sh = 0;
    uint8_t *rgba = decode_rgba(data, size, &sw, &sh);
    if (!rgba) return 0;
    int ok = scale_rgba_to565(rgba, sw, sh, out_w, out_h, bg, out);
    stbi_image_free(rgba);
    return ok;
}

int eg_image_scale(const EgImage *src, int dst_w, int dst_h, EgImage *out)
{
    memset(out, 0, sizeof(*out));
    if (!src || !src->pixels || dst_w <= 0 || dst_h <= 0) return 0;
    uint16_t *px = eg_malloc(sizeof(uint16_t) * (size_t)dst_w * (size_t)dst_h);
    if (!px) return 0;
    for (int y = 0; y < dst_h; y++) {
        int sy0 = y * src->h / dst_h;
        int sy1 = (y + 1) * src->h / dst_h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = x * src->w / dst_w;
            int sx1 = (x + 1) * src->w / dst_w;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t r = 0, g = 0, b = 0;
            int n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                for (int sx = sx0; sx < sx1; sx++) {
                    uint16_t p = src->pixels[(size_t)sy * src->w + sx];
                    r += (p >> 11) & 31;
                    g += (p >> 5) & 63;
                    b += p & 31;
                    n++;
                }
            }
            if (!n) n = 1;
            r = (r + n / 2) / n;
            g = (g + n / 2) / n;
            b = (b + n / 2) / n;
            px[(size_t)y * dst_w + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
    out->w = dst_w;
    out->h = dst_h;
    out->pixels = px;
    return 1;
}

int eg_image_load(const uint8_t *data, size_t size, int max_w, int max_h,
                  uint16_t background, EgImage *out)
{
    memset(out, 0, sizeof(*out));
    int sw = 0, sh = 0;
    if (!eg_image_info(data, size, &sw, &sh)) return 0;
    if (max_w <= 0) max_w = EG_W;
    if (max_h <= 0) max_h = EG_H;
    int scale = 1024;
    if (sw > max_w) {
        int s = max_w * 1024 / sw;
        if (s < scale) scale = s;
    }
    if (sh > max_h) {
        int s = max_h * 1024 / sh;
        if (s < scale) scale = s;
    }
    if (scale == 1024) {
        /* Small images are also allowed to grow to a useful size only when
         * the caller gave a constrained box; otherwise keep them natural. */
        int dw = sw, dh = sh;
        if (sw < max_w && sh < max_h) {
            /* leave natural size */
        }
        return eg_image_decode_to(data, size, dw, dh, background, out);
    }
    int dw = sw * scale / 1024;
    int dh = sh * scale / 1024;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    return eg_image_decode_to(data, size, dw, dh, background, out);
}

int eg_image_load_cover(const uint8_t *data, size_t size, int box_w, int box_h,
                        uint16_t background, EgImage *out)
{
    memset(out, 0, sizeof(*out));
    if (box_w <= 0 || box_h <= 0) return 0;
    int sw = 0, sh = 0;
    uint8_t *rgba = decode_rgba(data, size, &sw, &sh);
    if (!rgba) return 0;
    /* cover-crop to the box aspect ratio, then scale to exactly the box */
    int crop_w = sw, crop_h = sh;
    if ((long)sw * box_h > (long)sh * box_w) {
        crop_w = sh * box_w / box_h;
        if (crop_w < 1) crop_w = 1;
    } else {
        crop_h = sw * box_h / box_w;
        if (crop_h < 1) crop_h = 1;
    }
    int ox = (sw - crop_w) / 2;
    int oy = (sh - crop_h) / 2;
    uint8_t *cropped = eg_malloc((size_t)crop_w * (size_t)crop_h * 4);
    if (!cropped) { stbi_image_free(rgba); return 0; }
    for (int y = 0; y < crop_h; y++) {
        memcpy(cropped + (size_t)y * crop_w * 4,
               rgba + ((size_t)(y + oy) * sw + ox) * 4,
               (size_t)crop_w * 4);
    }
    stbi_image_free(rgba);
    int ok = scale_rgba_to565(cropped, crop_w, crop_h, box_w, box_h, background, out);
    free(cropped);
    return ok;
}

void eg_image_free(EgImage *img)
{
    if (!img) return;
    free(img->pixels);
    img->pixels = NULL;
    img->w = img->h = 0;
}
