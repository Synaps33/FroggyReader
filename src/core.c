#include "libretro.h"
#include "app.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static retro_environment_t s_environ;
static retro_video_refresh_t s_video;
static retro_audio_sample_t s_audio;
static retro_audio_sample_batch_t s_audio_batch;
static retro_input_poll_t s_input_poll;
static retro_input_state_t s_input_state;
static EgApp *s_app;
static int s_loaded;
static char s_content_path[EG_MAX_PATH];
static char s_save_dir[EG_MAX_PATH];

void retro_set_environment(retro_environment_t cb)
{
    s_environ = cb;
    if (cb) {
        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
        cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }
}

void retro_set_video_refresh(retro_video_refresh_t cb) { s_video = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { s_audio = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_audio_batch = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { s_input_poll = cb; }
void retro_set_input_state(retro_input_state_t cb) { s_input_state = cb; }

void retro_init(void)
{
    eg_log("core: init\n");
}

void retro_deinit(void)
{
    if (s_app) {
        eg_app_destroy(s_app);
        s_app = NULL;
    }
    s_loaded = 0;
    eg_log("core: deinit\n");
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "EPUB Reader";
    info->library_version = "1.0-sf2000";
    info->valid_extensions = "epub|gba|sf2k|gb3k";
    info->need_fullpath = true;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = EG_W;
    info->geometry.base_height = EG_H;
    info->geometry.max_width = EG_W;
    info->geometry.max_height = EG_H;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0f;
    info->timing.sample_rate = 22050.0f;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

void retro_reset(void)
{
    if (s_app) {
        eg_app_save(s_app);
        eg_app_destroy(s_app);
    }
    s_app = eg_app_create(s_content_path, s_save_dir[0] ? s_save_dir : NULL);
}

static uint16_t poll_pad(void)
{
    if (s_input_poll) s_input_poll();
    if (!s_input_state) return 0;
    uint16_t pad = 0;
    for (int id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; id++) {
        if (s_input_state(0, RETRO_DEVICE_JOYPAD, 0, id))
            pad |= (uint16_t)(1u << id);
    }
    return pad;
}

bool retro_load_game(const struct retro_game_info *info)
{
    const char *content = (info && info->path && info->path[0]) ? info->path : ".";
    snprintf(s_content_path, sizeof(s_content_path), "%s", content);
    s_save_dir[0] = 0;
    if (s_environ) {
        const char *save = NULL;
        if (s_environ(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save) && save && *save) {
            char base[EG_MAX_PATH];
            eg_path_normalize(base, sizeof(base), "", save);
            snprintf(s_save_dir, sizeof(s_save_dir), "%.*s/epub_reader",
                     (int)sizeof(s_save_dir) - 14, base);
        }
    }
    eg_log("core: load_game path=%s save=%s\n", s_content_path,
           s_save_dir[0] ? s_save_dir : "(default)");
    s_app = eg_app_create(s_content_path, s_save_dir[0] ? s_save_dir : NULL);
    s_loaded = s_app != NULL;
    return s_loaded;
}

const char *eg_core_status(void)
{
    return s_app ? eg_app_status(s_app) : "";
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    (void)type; (void)info; (void)num;
    return false;
}

void retro_unload_game(void)
{
    if (s_app) {
        eg_app_destroy(s_app);
        s_app = NULL;
    }
    s_loaded = 0;
    eg_log("core: unload_game\n");
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void retro_run(void)
{
    if (!s_app) return;
    uint16_t pad = poll_pad();
    eg_app_run(s_app, pad);
    if (s_video) {
        uint16_t *fb = eg_app_framebuffer(s_app);
        s_video(fb, EG_W, EG_H, EG_W * sizeof(uint16_t));
    }
    (void)s_audio;
    (void)s_audio_batch;
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index; (void)enabled; (void)code;
}
