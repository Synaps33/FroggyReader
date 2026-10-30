#include "libretro.h"
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

#define W 320
#define H 240

static uint16_t g_fb[W * H];
static uint16_t g_pad = 0;
static int g_video_calls = 0;
static char g_prefix[512] = "screenshots/frame";
static char g_save_dir[512];

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    const uint8_t *src = (const uint8_t *)data;
    for (unsigned y = 0; y < height && y < H; y++) {
        unsigned cw = width < W ? width : W;
        memcpy(&g_fb[y * W], src + y * pitch, cw * 2);
    }
    g_video_calls++;
}

static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *data, size_t frames) { (void)data; return frames; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)index;
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    return (g_pad & (1u << id)) ? 1 : 0;
}

static bool environ_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format fmt = *(enum retro_pixel_format *)data;
        return fmt == RETRO_PIXEL_FORMAT_RGB565;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) {
        if (!g_save_dir[0]) return false;
        *(const char **)data = g_save_dir;
        return true;
    }
    return false;
}

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = g_fb[i];
        fputc((int)(((p >> 11) & 31) * 255 / 31), f);
        fputc((int)(((p >> 5) & 63) * 255 / 63), f);
        fputc((int)((p & 31) * 255 / 31), f);
    }
    fclose(f);
}

static uint16_t script_pad(const char *scenario, int frame)
{
    uint16_t p = 0;
    if (strcmp(scenario, "library") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 18) p |= EG_PAD_RIGHT;
        if (frame == 22) p |= EG_PAD_DOWN;
        if (frame == 26) p |= EG_PAD_RIGHT;
    } else if (strcmp(scenario, "read") == 0 || strcmp(scenario, "mips") == 0) {
        if (frame == 14) p |= EG_PAD_Y;           /* sort by author */
        if (frame == 16) p |= EG_PAD_A;           /* open Sekret... */
        if (frame == 20) p |= EG_PAD_RIGHT;       /* cover -> chapter 1 */
        if (frame == 24) p |= EG_PAD_RIGHT;       /* next page */
        if (frame == 28) p |= EG_PAD_Y;           /* TOC */
        if (frame == 31) p |= EG_PAD_DOWN;
        if (frame == 34) p |= EG_PAD_DOWN;
        if (frame == 37) p |= EG_PAD_A;           /* jump to chapter 3 */
        if (frame == 41) p |= EG_PAD_RIGHT;       /* image page */
        if (frame == 45) p |= EG_PAD_A;           /* menu */
        if (frame == 48) p |= EG_PAD_DOWN;
        if (frame == 51) p |= EG_PAD_DOWN;
        if (frame == 54) p |= EG_PAD_A;           /* settings */
        if (frame == 57) p |= EG_PAD_RIGHT;       /* theme -> sepia */
        if (frame == 60) p |= EG_PAD_A;           /* apply */
        if (frame == 64) p |= EG_PAD_SELECT;      /* theme -> night */
        if (frame == 70) p |= EG_PAD_B;           /* back to library */
    } else if (strcmp(scenario, "night") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 24) p |= EG_PAD_SELECT;
        if (frame == 27) p |= EG_PAD_SELECT;
    } else if (strcmp(scenario, "toc") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 23) p |= EG_PAD_Y;
        if (frame == 26) p |= EG_PAD_DOWN;
        if (frame == 29) p |= EG_PAD_DOWN;
    } else if (strcmp(scenario, "images") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 23) p |= EG_PAD_Y;
        if (frame == 26) p |= EG_PAD_DOWN;
        if (frame == 29) p |= EG_PAD_DOWN;
        if (frame == 31) p |= EG_PAD_DOWN;
        if (frame == 32) p |= EG_PAD_A;
        if (frame == 36) p |= EG_PAD_RIGHT;
        if (frame == 40) p |= EG_PAD_RIGHT;
        if (frame == 44) p |= EG_PAD_RIGHT;
    } else if (strcmp(scenario, "bookmarks") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 24) p |= EG_PAD_X;
        if (frame == 28) p |= EG_PAD_A;
        if (frame == 31) p |= EG_PAD_DOWN;
        if (frame == 34) p |= EG_PAD_A;
    } else if (strcmp(scenario, "info") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 24) p |= EG_PAD_A;
        if (frame == 27) p |= EG_PAD_DOWN;
        if (frame == 30) p |= EG_PAD_DOWN;
        if (frame == 33) p |= EG_PAD_DOWN;
        if (frame == 36) p |= EG_PAD_A;
    } else if (strcmp(scenario, "nocover") == 0) {
        if (frame == 14) p |= EG_PAD_A;
        if (frame == 18) p |= EG_PAD_Y;
    } else if (strcmp(scenario, "scroll") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame >= 24 && frame <= 200) p |= EG_PAD_DOWN;
        if (frame >= 225 && frame <= 285) p |= EG_PAD_UP;
    } else if (strcmp(scenario, "table") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 23) p |= EG_PAD_Y;
        if (frame == 26) p |= EG_PAD_DOWN;
        if (frame == 29) p |= EG_PAD_DOWN;
        if (frame == 32) p |= EG_PAD_DOWN;
        if (frame == 35) p |= EG_PAD_DOWN;
        if (frame == 38) p |= EG_PAD_DOWN;
        if (frame == 41) p |= EG_PAD_A;
        if (frame == 45) p |= EG_PAD_RIGHT;
    } else if (strcmp(scenario, "marquee") == 0) {
        if (frame == 14) p |= EG_PAD_RIGHT;
    } else if (strcmp(scenario, "lang") == 0) {
        if (frame == 14) p |= EG_PAD_SELECT;
        if (frame == 17 || frame == 20 || frame == 23 || frame == 26) p |= EG_PAD_DOWN;
        if (frame == 29) p |= EG_PAD_RIGHT;
        if (frame == 32) p |= EG_PAD_A;
        if (frame == 38) p |= EG_PAD_SELECT;
        if (frame == 41 || frame == 44 || frame == 47 || frame == 50) p |= EG_PAD_DOWN;
        if (frame == 53) p |= EG_PAD_RIGHT;
        if (frame == 56) p |= EG_PAD_A;
        if (frame == 62) p |= EG_PAD_SELECT;
        if (frame == 65 || frame == 68 || frame == 71 || frame == 74) p |= EG_PAD_DOWN;
        if (frame == 77) p |= EG_PAD_RIGHT;
        if (frame == 80) p |= EG_PAD_A;
    } else if (strcmp(scenario, "langread") == 0) {
        if (frame == 14) p |= EG_PAD_START;
        if (frame == 19) p |= EG_PAD_B;
        if (frame == 23) p |= EG_PAD_A;
        if (frame == 27) p |= EG_PAD_RIGHT;
        if (frame == 31) p |= EG_PAD_A;
        if (frame == 35) p |= EG_PAD_B;
        if (frame == 38) p |= EG_PAD_Y;
        if (frame == 42) p |= EG_PAD_B;
        if (frame == 45) p |= EG_PAD_START;
        if (frame == 49) p |= EG_PAD_B;
    } else if (strcmp(scenario, "help") == 0) {
        if (frame == 15) p |= EG_PAD_START;
    } else if (strcmp(scenario, "anchor") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 23) p |= EG_PAD_Y;
        if (frame == 26) p |= EG_PAD_DOWN;
        if (frame == 29) p |= EG_PAD_DOWN;
        if (frame == 32) p |= EG_PAD_A;
        if (frame == 36) p |= EG_PAD_RIGHT;
    } else if (strcmp(scenario, "shelf2") == 0) {
        if (frame == 14) p |= EG_PAD_R;
        if (frame == 18) p |= EG_PAD_LEFT;
        if (frame == 22) p |= EG_PAD_DOWN;
    } else if (strcmp(scenario, "resume") == 0) {
        if (frame == 14) p |= EG_PAD_A;
        if (frame == 18) p |= EG_PAD_RIGHT;
    } else if (strcmp(scenario, "bmjump") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 24) p |= EG_PAD_X;
        if (frame == 28) p |= EG_PAD_RIGHT;
        if (frame == 32) p |= EG_PAD_A;
        if (frame == 35) p |= EG_PAD_DOWN;
        if (frame == 38) p |= EG_PAD_A;
        if (frame == 41) p |= EG_PAD_A;
    } else if (strcmp(scenario, "reflow") == 0) {
        if (frame == 14) p |= EG_PAD_Y;
        if (frame == 16) p |= EG_PAD_A;
        if (frame == 20) p |= EG_PAD_RIGHT;
        if (frame == 24) p |= EG_PAD_START;
        if (frame == 27) p |= EG_PAD_DOWN;
        if (frame == 30) p |= EG_PAD_RIGHT;
        if (frame == 33) p |= EG_PAD_DOWN;
        if (frame == 34) p |= EG_PAD_RIGHT;
        if (frame == 36) p |= EG_PAD_DOWN;
        if (frame == 39) p |= EG_PAD_RIGHT;
        if (frame == 42) p |= EG_PAD_A;
        if (frame == 48) p |= EG_PAD_RIGHT;
    }
    return p;
}

static int is_shot(const char *scenario, int frame)
{
    static const int lib_shots[] = {1, 6, 11, 13, 16, 19, 23, 27, 31};
    static const int read_shots[] = {16, 21, 25, 29, 35, 38, 42, 46, 52, 55, 58, 61, 65, 69, 72};
    static const int night_shots[] = {17, 21, 25, 28, 31};
    static const int toc_shots[] = {17, 21, 24, 28};
    static const int image_shots[] = {33, 37, 41, 45, 48};
    static const int bm_shots[] = {25, 32, 35, 38};
    static const int info_shots[] = {25, 37};
    static const int help_shots[] = {17, 21};
    static const int lang_shots[] = {31, 34, 55, 58, 79, 82};
    static const int marquee_shots[] = {16, 28, 40, 52, 64, 76, 88, 100};
    static const int langread_shots[] = {6, 16, 24, 28, 32, 39, 46};
    static const int table_shots[] = {42, 46, 50};
    static const int scroll_shots[] = {24, 46, 78, 124, 146, 190, 202, 236, 250, 270};
    static const int nocover_shots[] = {1, 15, 19};
    static const int anchor_shots[] = {33, 37};
    static const int shelf2_shots[] = {15, 19, 23};
    static const int resume_shots[] = {15, 17, 19};
    static const int reflow_shots[] = {25, 31, 43, 49};
    static const int bmjump_shots[] = {25, 36, 39, 42};
    const int *arr = NULL;
    int n = 0;
    if (strcmp(scenario, "library") == 0) { arr = lib_shots; n = (int)(sizeof(lib_shots)/sizeof(lib_shots[0])); }
    else if (strcmp(scenario, "read") == 0 || strcmp(scenario, "mips") == 0) { arr = read_shots; n = (int)(sizeof(read_shots)/sizeof(read_shots[0])); }
    else if (strcmp(scenario, "night") == 0) { arr = night_shots; n = (int)(sizeof(night_shots)/sizeof(night_shots[0])); }
    else if (strcmp(scenario, "toc") == 0) { arr = toc_shots; n = (int)(sizeof(toc_shots)/sizeof(toc_shots[0])); }
    else if (strcmp(scenario, "images") == 0) { arr = image_shots; n = (int)(sizeof(image_shots)/sizeof(image_shots[0])); }
    else if (strcmp(scenario, "bookmarks") == 0) { arr = bm_shots; n = (int)(sizeof(bm_shots)/sizeof(bm_shots[0])); }
    else if (strcmp(scenario, "info") == 0) { arr = info_shots; n = (int)(sizeof(info_shots)/sizeof(info_shots[0])); }
    else if (strcmp(scenario, "help") == 0) { arr = help_shots; n = (int)(sizeof(help_shots)/sizeof(help_shots[0])); }
    else if (strcmp(scenario, "lang") == 0) { arr = lang_shots; n = (int)(sizeof(lang_shots)/sizeof(lang_shots[0])); }
    else if (strcmp(scenario, "marquee") == 0) { arr = marquee_shots; n = (int)(sizeof(marquee_shots)/sizeof(marquee_shots[0])); }
    else if (strcmp(scenario, "langread") == 0) { arr = langread_shots; n = (int)(sizeof(langread_shots)/sizeof(langread_shots[0])); }
    else if (strcmp(scenario, "table") == 0) { arr = table_shots; n = (int)(sizeof(table_shots)/sizeof(table_shots[0])); }
    else if (strcmp(scenario, "scroll") == 0) { arr = scroll_shots; n = (int)(sizeof(scroll_shots)/sizeof(scroll_shots[0])); }
    else if (strcmp(scenario, "nocover") == 0) { arr = nocover_shots; n = (int)(sizeof(nocover_shots)/sizeof(nocover_shots[0])); }
    else if (strcmp(scenario, "anchor") == 0) { arr = anchor_shots; n = (int)(sizeof(anchor_shots)/sizeof(anchor_shots[0])); }
    else if (strcmp(scenario, "shelf2") == 0) { arr = shelf2_shots; n = (int)(sizeof(shelf2_shots)/sizeof(shelf2_shots[0])); }
    else if (strcmp(scenario, "resume") == 0) { arr = resume_shots; n = (int)(sizeof(resume_shots)/sizeof(resume_shots[0])); }
    else if (strcmp(scenario, "reflow") == 0) { arr = reflow_shots; n = (int)(sizeof(reflow_shots)/sizeof(reflow_shots[0])); }
    else if (strcmp(scenario, "bmjump") == 0) { arr = bmjump_shots; n = (int)(sizeof(bmjump_shots)/sizeof(bmjump_shots[0])); }
    for (int i = 0; i < n; i++) if (arr[i] == frame) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *content = argc > 1 ? argv[1] : "library";
    int frames = argc > 2 ? atoi(argv[2]) : 90;
    if (argc > 3) snprintf(g_prefix, sizeof(g_prefix), "%s", argv[3]);
    const char *scenario = argc > 4 ? argv[4] : "read";
    const char *save_env = getenv("EPUB_READER_SAVE");
    if (save_env && *save_env) {
        snprintf(g_save_dir, sizeof(g_save_dir), "%s", save_env);
        mkdir(g_save_dir, 0755);
    }

    printf("=== EPUB Reader host runner ===\n");
    printf("content: %s frames: %d scenario: %s\n", content, frames, scenario);
    if (g_save_dir[0]) printf("save: %s\n", g_save_dir);

    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = content;
    info.data = NULL;
    info.size = 0;
    if (!retro_load_game(&info)) {
        fprintf(stderr, "retro_load_game failed\n");
        return 1;
    }

    char last_status[256] = "";
    for (int f = 1; f <= frames; f++) {
        g_pad = script_pad(scenario, f);
        retro_run();
        if (is_shot(scenario, f)) {
            char path[768];
            snprintf(path, sizeof(path), "%s_%03d.ppm", g_prefix, f);
            save_ppm(path);
            printf("[RUNNER] frame %d -> %s\n", f, path);
        }
        const char *st = eg_core_status();
        if (st && strcmp(st, last_status) != 0) {
            printf("[RUNNER] f=%d %s\n", f, st);
            snprintf(last_status, sizeof(last_status), "%s", st);
        }
        if (f % 30 == 0) {
            printf("[RUNNER] frame %d video_calls=%d\n", f, g_video_calls);
        }
    }
    retro_unload_game();
    retro_deinit();
    printf("[RUNNER] done, %d frames, %d video calls\n", frames, g_video_calls);
    return 0;
}
