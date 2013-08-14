#include <stdio.h>
#include <SDL_opengles2.h>
#include <string.h>

#include "libretro.h"
#include "resampler.h"
#include "utils.h"
#include "libco.h"

#include "api/m64p_frontend.h"
#include "api/m64p_types.h"
#include "r4300/r4300.h"
#include "main/version.h"

static struct retro_hw_render_callback render_iface;
static cothread_t main_thread;
static cothread_t emulator_thread;
static bool emu_thread_has_run = false; // < This is used to ensure the core_gl_context_reset
                                        //   function doesn't try to reinit graphics before needed

static const rarch_resampler_t *resampler;
static void *resampler_data;
static float *audio_in_buffer_float;
static float *audio_out_buffer_float;
static int16_t *audio_out_buffer_s16;

static void EmuThreadFunction()
{
    emu_thread_has_run = true;

    CoreAttachPlugin(M64PLUGIN_GFX, 0);
    CoreAttachPlugin(M64PLUGIN_AUDIO, 0);
    CoreAttachPlugin(M64PLUGIN_INPUT, 0);
    CoreAttachPlugin(M64PLUGIN_RSP, 0);

    CoreDoCommand(M64CMD_EXECUTE, 0, NULL);

    CoreDetachPlugin(M64PLUGIN_GFX);
    CoreDetachPlugin(M64PLUGIN_AUDIO);
    CoreDetachPlugin(M64PLUGIN_INPUT);
    CoreDetachPlugin(M64PLUGIN_RSP);

    co_switch(main_thread);

    //NEVER RETURN! That's how libco rolls
    while(1)
    {
        printf("Running Dead N64 Emulator");
        co_switch(main_thread);
    }
}

static void n64DebugCallback(void* aContext, int aLevel, const char* aMessage)
{
    char buffer[1024];
    snprintf(buffer, 1024, "mupen64plus: %s\n", aMessage);
    printf("%s", buffer);
}


//

static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
retro_input_state_t input_cb = NULL;
retro_audio_sample_batch_t audio_batch_cb = NULL;
retro_environment_t environ_cb = NULL;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)   { }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      { "mupen64-filtering",
         "Texture filtering; automatic|bilinear|nearest" },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

//

const char* retro_get_system_directory()
{
    const char* dir;
    environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir);

    return dir ? dir : ".";
}

static void core_gl_context_reset()
{
   glsym_init_procs(render_iface.get_proc_address);

#ifdef GLIDE64
    extern int InitGfx ();

    if (emu_thread_has_run)
        InitGfx();
#endif
}

GLuint retro_get_fbo_id()
{
    return render_iface.get_current_framebuffer();
}

void retro_n64_video_flipped()
{
    co_switch(main_thread);
}

//

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "Mupen64plus";
   info->library_version = "2.0-rc2";
   info->valid_extensions = "n64|v64|z64";
   info->need_fullpath = false;
   info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    // TODO
    info->geometry.base_width = 640;        // TODO: Real sizes
    info->geometry.base_height = 480;
    info->geometry.max_width = 640;
    info->geometry.max_height = 480;
    info->geometry.aspect_ratio = 0.0;
    info->timing.fps = 60.0;                // TODO: NTSC/PAL + Actual timing 
    info->timing.sample_rate = 44100.0;
}

void retro_init(void)
{
    unsigned colorMode = RETRO_PIXEL_FORMAT_XRGB8888;
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &colorMode);

    if(CoreStartup(FRONTEND_API_VERSION, ".", ".", "Core", n64DebugCallback, 0, 0))
    {
        printf("mupen64plus: Failed to initialize core\n");
    }

    rarch_resampler_realloc(&resampler_data, &resampler, NULL, 1.0);
    audio_in_buffer_float = malloc(4096 * sizeof(float));
    audio_out_buffer_float = malloc(4096 * sizeof(float));
    audio_out_buffer_s16 = malloc(4096 * sizeof(int16_t));
    audio_convert_init_simd();
}

void retro_deinit(void)
{
    CoreShutdown();

    if (resampler && resampler_data)
    {
       resampler->free(resampler_data);
       resampler = NULL;
       resampler_data = NULL;
       free(audio_in_buffer_float);
       free(audio_out_buffer_float);
       free(audio_out_buffer_s16);
    }
}

unsigned retro_filtering = 0;

void update_variables(void)
{
#ifdef GLIDE64
   struct retro_variable var;
   
   var.key = "mupen64-filtering";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {

      if (strcmp(var.value, "automatic") == 0)
         retro_filtering = 0;
      else if (strcmp(var.value, "bilinear") == 0)
         retro_filtering = 1;
      else if (strcmp(var.value, "nearest") == 0)
         retro_filtering = 2;
   }
#endif
}

bool retro_load_game(const struct retro_game_info *game)
{
   update_variables();

   memset(&render_iface, 0, sizeof(render_iface));
#ifndef GLES
    render_iface.context_type = RETRO_HW_CONTEXT_OPENGL;
#else
    render_iface.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#endif
    render_iface.context_reset = core_gl_context_reset;
    render_iface.depth = true;
    render_iface.bottom_left_origin = true;
    render_iface.cache_context = true;
    
    if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &render_iface))
    {
        printf("mupen64plus: libretro frontend doesn't have OpenGL support.");
        return false;
    }

    if(CoreDoCommand(M64CMD_ROM_OPEN, game->size, (void*)game->data))
    {
        printf("mupen64plus: Failed to load ROM\n");
        return false;
    }

    main_thread = co_active();
    emulator_thread = co_create(65536 * sizeof(void*) * 16, EmuThreadFunction);

    return true;
}

void retro_unload_game(void)
{
    stop = 1;
    co_switch(emulator_thread);

    CoreDoCommand(M64CMD_ROM_CLOSE, 0, NULL);
}

void retro_audio_batch_cb(const int16_t *raw_data, size_t frames, unsigned freq)
{
   audio_convert_s16_to_float(audio_in_buffer_float, raw_data, frames * 2, 1.0f);

   double ratio = 44100.0 / freq;
   struct resampler_data data = {0};
   data.data_in = audio_in_buffer_float;
   data.data_out = audio_out_buffer_float;
   data.input_frames = frames;
   data.ratio = ratio;

   resampler->process(resampler_data, &data);

   audio_convert_float_to_s16(audio_out_buffer_s16, audio_out_buffer_float, data.output_frames * 2);

   const int16_t *out = audio_out_buffer_s16;
   while (data.output_frames)
   {
      size_t ret = audio_batch_cb(out, data.output_frames);
      data.output_frames -= ret;
      out += ret * 2;
   }
}

void retro_run (void)
{
    poll_cb();

    sglEnter();
    co_switch(emulator_thread);
    sglExit();

    video_cb(RETRO_HW_FRAME_BUFFER_VALID, 640, 480, 0);
}

void retro_reset (void)
{
    CoreDoCommand(M64CMD_RESET, 1, (void*)0);
}

unsigned retro_get_region (void)
{
    // TODO
    return RETRO_REGION_NTSC;
}



// Stubs
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }

size_t retro_serialize_size (void) { return 0; }
bool retro_serialize(void *data, size_t size) { return false; }
bool retro_unserialize(const void * data, size_t size) { return false; }

void *retro_get_memory_data(unsigned type) { return 0; }
size_t retro_get_memory_size(unsigned type) { return 0; }


void retro_set_controller_port_device(unsigned in_port, unsigned device) { }

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2) { }

