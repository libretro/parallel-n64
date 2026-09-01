/* Headless libretro host for measuring a core's frame time.
 *
 * Built by `make tools`. Loads the core and a ROM, serves every
 * key=value argument as a core option, accepts and discards video, audio
 * and input, and times each retro_run:
 *
 *   ./lrhost parallel_n64_libretro.dll rom.v64 900 \
 *       parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle \
 *       parallel-n64-cpucore=dynamic_recompiler \
 *       parallel-n64-angrylion-multithread="all threads" \
 *       parallel-n64-angrylion-async=disabled
 *
 * Prints the per-retro_run mean, standard deviation, p50/p90/p99 and
 * worst frame - the variance a player feels - over the run with the
 * first tenth (the boot) left out, plus how many frames were presented
 * and duplicated. LRHOST_DUMPDIR=dir writes the presented frames
 * LRHOST_DUMP_FROM..LRHOST_DUMP_TO (default 595..605) as raw 16-bit rows
 * so two configurations can be compared pixel for pixel;
 * LRHOST_VERBOSE=1 shows the core's log.
 */
#if defined(_WIN32)
#include <windows.h>
#else
#define _GNU_SOURCE
#include <dlfcn.h>
#endif
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdarg.h>
#include <libretro.h>
#include <features/features_cpu.h>

static struct { char key[64]; char val[64]; } opts[64];
static int nopts;
static const char *opt(const char *key)
{
    int i;
    for (i = 0; i < nopts; i++) if (!strcmp(opts[i].key, key)) return opts[i].val;
    return NULL;
}

static long frames_presented, frames_duped;
static long dump_from = 595, dump_to = 605;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned vid_w, vid_h;
static uint32_t frame_crc;

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    vid_w = w; vid_h = h;
    if (!data) { frames_duped++; return; }
    frames_presented++;
    if (getenv("LRHOST_DUMPDIR") && frames_presented >= dump_from && frames_presented <= dump_to)
    {
        char n[256]; FILE *f; unsigned y;
        snprintf(n, sizeof n, "%s/%04ld.raw", getenv("LRHOST_DUMPDIR"), frames_presented);
        f = fopen(n, "wb");
        if (f) { for (y = 0; y < h; y++) fwrite((const uint8_t*)data + y * pitch, 1, w * 2, f); fclose(f); }
    }
    if (frames_presented % 60 == 0) /* cheap signature: sample a few rows */
    {
        const uint8_t *p = data; unsigned y; uint32_t c = 2166136261u;
        for (y = 0; y < h; y += 16) { unsigned x; for (x = 0; x < w * 2; x += 7) { c ^= p[y * pitch + x]; c *= 16777619u; } }
        frame_crc = c;
    }
}
static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t n) { (void)d; return n; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) { (void)port; (void)dev; (void)idx; (void)id; return 0; }
static void log_cb(enum retro_log_level lvl, const char *fmt, ...)
{
    if (lvl < RETRO_LOG_WARN && !getenv("LRHOST_VERBOSE")) return;
    { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback*)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        struct retro_variable *v = data;
        v->value = opt(v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool*)data = false; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt = *(enum retro_pixel_format*)data; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char**)data = "lrhost-system"; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool*)data = true; return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int*)data = 3; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        return false;   /* software renderers only */
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned*)data = RETRO_HW_CONTEXT_NONE; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned*)data = 2; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned*)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return false;
    default:
        return false;
    }
}

static double now_ms(void)
{
    return (double)cpu_features_get_time_usec() / 1e3;
}

#if defined(_WIN32)
static void *lib_open(const char *p) { return (void*)LoadLibraryA(p); }
static void *lib_sym(void *h, const char *n) { return (void*)GetProcAddress((HMODULE)h, n); }
static const char *lib_err(void) { static char b[64]; snprintf(b, sizeof b, "error %lu", GetLastError()); return b; }
#define MKDIR(p) _mkdir(p)
#include <direct.h>
#else
static void *lib_open(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *lib_sym(void *h, const char *n) { return dlsym(h, n); }
static const char *lib_err(void) { return dlerror(); }
#define MKDIR(p) mkdir(p, 0755)
#endif
static int cmp_d(const void *a, const void *b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
    void *h; int i, nframes;
    void (*set_env)(retro_environment_t);
    void (*set_video)(retro_video_refresh_t);
    void (*set_audio)(retro_audio_sample_t);
    void (*set_audio_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*r_init)(void); void (*r_deinit)(void);
    bool (*r_load)(const struct retro_game_info*);
    void (*r_run)(void);
    void (*r_get_av)(struct retro_system_av_info*);
    struct retro_game_info game;
    struct retro_system_av_info av;
    double *ft, sum = 0, sq = 0, t0, t1, t_first;
    FILE *f;

    if (argc < 4) { fprintf(stderr, "usage: %s core.so rom FRAMES [key=value ...]\n", argv[0]); return 2; }
    for (i = 4; i < argc && nopts < 64; i++)
    {
        const char *eq = strchr(argv[i], '=');
        if (!eq) continue;
        snprintf(opts[nopts].key, sizeof opts[nopts].key, "%.*s", (int)(eq - argv[i]), argv[i]);
        snprintf(opts[nopts].val, sizeof opts[nopts].val, "%s", eq + 1);
        nopts++;
    }
    nframes = atoi(argv[3]);
    if (getenv("LRHOST_DUMP_FROM")) dump_from = atol(getenv("LRHOST_DUMP_FROM"));
    if (getenv("LRHOST_DUMP_TO"))   dump_to   = atol(getenv("LRHOST_DUMP_TO"));
    MKDIR("lrhost-system");

    h = lib_open(argv[1]);
    if (!h) { fprintf(stderr, "cannot load %s: %s\n", argv[1], lib_err()); return 1; }
#define SYM(v, n) *(void**)&v = lib_sym(h, n); if (!v) { fprintf(stderr, "missing %s\n", n); return 1; }
    SYM(set_env, "retro_set_environment"); SYM(set_video, "retro_set_video_refresh");
    SYM(set_audio, "retro_set_audio_sample"); SYM(set_audio_batch, "retro_set_audio_sample_batch");
    SYM(set_input_poll, "retro_set_input_poll"); SYM(set_input_state, "retro_set_input_state");
    SYM(r_init, "retro_init"); SYM(r_deinit, "retro_deinit"); SYM(r_load, "retro_load_game");
    SYM(r_run, "retro_run"); SYM(r_get_av, "retro_get_system_av_info");

    set_env(env_cb); r_init();
    set_video(video_cb); set_audio(audio_sample_cb); set_audio_batch(audio_batch_cb);
    set_input_poll(input_poll_cb); set_input_state(input_state_cb);

    f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END); game.size = ftell(f); fseek(f, 0, SEEK_SET);
    game.data = malloc(game.size); if (fread((void*)game.data, 1, game.size, f) != game.size) return 1; fclose(f);
    game.path = argv[2]; game.meta = NULL;
    if (!r_load(&game)) { fprintf(stderr, "retro_load_game failed\n"); return 1; }
    r_get_av(&av);

    ft = calloc(nframes, sizeof *ft);
    t_first = now_ms();
    for (i = 0; i < nframes; i++)
    {
        t0 = now_ms(); r_run(); t1 = now_ms();
        ft[i] = t1 - t0;
    }
    for (i = nframes / 10; i < nframes; i++) { sum += ft[i]; sq += ft[i] * ft[i]; }   /* skip the boot */
    {
        int n = nframes - nframes / 10; double mean = sum / n, sd = sqrt(sq / n - mean * mean);
        double *s = malloc(n * sizeof *s); memcpy(s, ft + nframes / 10, n * sizeof *s); qsort(s, n, sizeof *s, cmp_d);
        printf("%s: %dx%d %.2ffps | %d frames in %.1fs | per retro_run: mean %.3f ms sd %.3f | p50 %.3f p90 %.3f p99 %.3f max %.3f ms | presented %ld duped %ld | sig %08x\n",
               strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 : argv[2], av.geometry.base_width, av.geometry.base_height, av.timing.fps,
               nframes, (now_ms() - t_first) / 1e3, mean, sd, s[n / 2], s[n * 9 / 10], s[n * 99 / 100], s[n - 1],
               frames_presented, frames_duped, frame_crc);
    }
    r_deinit();
    return 0;
}
