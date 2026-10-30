#ifndef EG_APP_H
#define EG_APP_H

#include <stdint.h>

typedef struct EgApp EgApp;

EgApp *eg_app_create(const char *content_path, const char *save_dir);
void eg_app_destroy(EgApp *app);
void eg_app_run(EgApp *app, uint16_t pad);
uint16_t *eg_app_framebuffer(EgApp *app);
const char *eg_app_status(EgApp *app);
const char *eg_core_status(void);
void eg_app_save(EgApp *app);

#define EG_PAD_B      (1u << 0)
#define EG_PAD_Y      (1u << 1)
#define EG_PAD_SELECT (1u << 2)
#define EG_PAD_START  (1u << 3)
#define EG_PAD_UP     (1u << 4)
#define EG_PAD_DOWN   (1u << 5)
#define EG_PAD_LEFT   (1u << 6)
#define EG_PAD_RIGHT  (1u << 7)
#define EG_PAD_A      (1u << 8)
#define EG_PAD_X      (1u << 9)
#define EG_PAD_L      (1u << 10)
#define EG_PAD_R      (1u << 11)

#endif
