#ifndef EG_IMAGE_H
#define EG_IMAGE_H

#include "util.h"

typedef struct {
    int w, h;
    uint16_t *pixels;
} EgImage;

int eg_image_info(const uint8_t *data, size_t size, int *w, int *h);
int eg_image_decode_to(const uint8_t *data, size_t size, int out_w, int out_h,
                       uint16_t bg, EgImage *out);
int eg_image_load(const uint8_t *data, size_t size, int max_w, int max_h,
                  uint16_t background, EgImage *out);
int eg_image_scale(const EgImage *src, int dst_w, int dst_h, EgImage *out);
void eg_image_free(EgImage *img);
int eg_image_load_cover(const uint8_t *data, size_t size, int box_w, int box_h,
                        uint16_t background, EgImage *out);

#endif
