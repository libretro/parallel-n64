#include <stdio.h>
#include <SDL_opengles2.h>
#include <string.h>

#include "libretro.h"
#include "performance.h"
#include "resampler.h"
#include "utils.h"
#include "libco.h"

#include "api/m64p_frontend.h"
#include "plugin/plugin.h"
#include "api/m64p_types.h"
#include "r4300/r4300.h"
#include "memory/memory.h"
#include "main/version.h"
#include "main/savestates.h"

retro_log_printf_t log_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
retro_input_state_t input_cb = NULL;
retro_audio_sample_batch_t audio_batch_cb = NULL;
retro_environment_t environ_cb = NULL;

struct retro_rumble_interface rumble;

static struct retro_hw_render_callback render_iface;
static cothread_t main_thread;
static cothread_t emulator_thread;
static bool emu_thread_has_run = false; // < This is used to ensure the core_gl_context_reset
                                        //   function doesn't try to reinit graphics before needed
static bool flip_only;
bool no_audio;
static savestates_job state_job_done;

static const rarch_resampler_t *resampler;
static void *resampler_data;
static float *audio_in_buffer_float;
static float *audio_out_buffer_float;
static int16_t *audio_out_buffer_s16;

void (*audio_convert_s16_to_float_arm)(float *out,
      const int16_t *in, size_t samples, float gain);
void (*audio_convert_float_to_s16_arm)(int16_t *out,
      const float *in, size_t samples);

static uint8_t* game_data;
static uint32_t game_size;

static enum gfx_plugin_type gfx_plugin;
static enum rsp_plugin_type rsp_plugin;
static uint32_t screen_width;
static uint32_t screen_height;

// after the controller's CONTROL* member has been assigned we can update
// them straight from here...
extern struct
{
    CONTROL *control;
    BUTTONS buttons;
} controller[4];

// ...but it won't be at least the first time we're called, in that case set
// these instead for input_plugin to read.
int pad_pak_types[4];

static void n64DebugCallback(void* aContext, int aLevel, const char* aMessage)
{
    char buffer[1024];
    snprintf(buffer, 1024, "mupen64plus: %s\n", aMessage);
    if (log_cb)
       log_cb(RETRO_LOG_INFO, buffer);
}

static m64p_rom_header romgame;

static void core_settings_set_defaults()
{
    /* Load GFX plugin core option */
    struct retro_variable gfx_var = { "mupen64-gfxplugin", 0 };
    environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var);

    gfx_plugin = GFX_GLN64;
    if (gfx_var.value)
    {
       if (gfx_var.value && strcmp(gfx_var.value, "rice") == 0)
          gfx_plugin = GFX_RICE;
       else if(gfx_var.value && strcmp(gfx_var.value, "glide64") == 0)
          gfx_plugin = GFX_GLIDE64;
    }

    /* Load RSP plugin core option */
    struct retro_variable rsp_var = { "mupen64-rspplugin", 0 };
    environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &rsp_var);

    rsp_plugin = RSP_HLE;
    if (rsp_var.value)
    {
       if (rsp_var.value && strcmp(rsp_var.value, "cxd4") == 0)
          rsp_plugin = RSP_CXD4;
    }
}

static void core_settings_set_balanced(void)
{
   struct retro_variable gfx_var = { "mupen64-gfxplugin", 0 };
   struct retro_variable rsp_var = { "mupen64-rspplugin", 0 };

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var);
   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &rsp_var);

   gfx_plugin = GFX_GLIDE64;
   rsp_plugin = RSP_CXD4;

   if (
         (strcmp(romgame.Name, "Perfect Dark") == 0)
      )
   {
      rsp_plugin = RSP_HLE;
   }
}

static void setup_variables_balanced(void)
{
   struct retro_variable variables[] = {
      { "mupen64-balanced-profile",
         "Balanced profile; no|yes" },
      { "mupen64-cpucore",
#ifdef DYNAREC
         "CPU Core; dynamic_recompiler|cached_interpreter|pure_interpreter" },
#else
         "CPU Core; cached_interpreter|pure_interpreter" },
#endif
      {"mupen64-pak1",
        "Player 1 Pak; none|memory|rumble"},
      {"mupen64-pak2",
        "Player 2 Pak; none|memory|rumble"},
      {"mupen64-pak3",
        "Player 3 Pak; none|memory|rumble"},
      {"mupen64-pak4",
        "Player 4 Pak; none|memory|rumble"},
      { "mupen64-disableexpmem",
         "Disable Expansion RAM; no|yes" },
      { "mupen64-gfxplugin",
         "Graphics Plugin; automatic (balanced)" },
      { "mupen64-rspplugin",
         "RSP Plugin; automatic (balanced)" },
      { "mupen64-screensize",
         "Graphics Resolution; 640x480|1280x960|320x240" },
      { "mupen64-filtering",
         "Texture filtering; automatic|bilinear|nearest" },
      { "mupen64-dupe",
         "Frame duping; no|yes" },
      { NULL, NULL },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

static void setup_variables(void)
{
   struct retro_variable variables[] = {
      { "mupen64-balanced-profile",
         "Balanced profile; no|yes" },
      { "mupen64-cpucore",
#ifdef DYNAREC
         "CPU Core; dynamic_recompiler|cached_interpreter|pure_interpreter" },
#else
         "CPU Core; cached_interpreter|pure_interpreter" },
#endif
      {"mupen64-pak1",
        "Player 1 Pak; none|memory|rumble"},
      {"mupen64-pak2",
        "Player 2 Pak; none|memory|rumble"},
      {"mupen64-pak3",
        "Player 3 Pak; none|memory|rumble"},
      {"mupen64-pak4",
        "Player 4 Pak; none|memory|rumble"},
      { "mupen64-disableexpmem",
         "Disable Expansion RAM; no|yes" },
      { "mupen64-gfxplugin",
         "Graphics Plugin; gln64|glide64|rice" },
      { "mupen64-rspplugin",
         "RSP Plugin; hle|cxd4" },
      { "mupen64-screensize",
         "Graphics Resolution; 640x480|1280x960|320x240" },
      { "mupen64-filtering",
         "Texture filtering; automatic|bilinear|nearest" },
      { "mupen64-dupe",
         "Frame duping; no|yes" },
      { NULL, NULL },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

static void EmuThreadFunction()
{
    emu_thread_has_run = true;

    if(CoreStartup(FRONTEND_API_VERSION, ".", ".", "Core", n64DebugCallback, 0, 0) && log_cb)
        log_cb(RETRO_LOG_ERROR, "mupen64plus: Failed to initialize core\n");

    if(CoreDoCommand(M64CMD_ROM_OPEN, game_size, (void*)game_data))
    {
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "mupen64plus: Failed to load ROM\n");
        goto load_fail;
    }

    free(game_data);
    game_data = 0;

    if(CoreDoCommand(M64CMD_ROM_GET_HEADER, sizeof(romgame), &romgame))
    {
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "mupen64plus; Failed to query ROM header information\n");
       goto load_fail;
    }

    core_settings_set_defaults();

    struct retro_variable var = { "mupen64-balanced-profile", 0 };

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value
          && (strcmp(var.value, "yes") == 0))
    {
       core_settings_set_balanced();
       setup_variables_balanced();
    }

    plugin_connect_all(gfx_plugin, rsp_plugin);

    CoreDoCommand(M64CMD_EXECUTE, 0, NULL);

    co_switch(main_thread);

load_fail:
    free(game_data);
    game_data = 0;

    //NEVER RETURN! That's how libco rolls
    while(1)
    {
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "Running Dead N64 Emulator");
       co_switch(main_thread);
    }
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

   extern int InitGfx ();
   extern void gles2n64_reset();

   if (gfx_plugin == GFX_GLIDE64 && emu_thread_has_run)
      InitGfx();
   else if (gfx_plugin == GFX_GLN64 && emu_thread_has_run)
      gles2n64_reset();
}

GLuint retro_get_fbo_id()
{
    return render_iface.get_current_framebuffer();
}

void vbo_draw();

int retro_return(bool just_flipping)
{
   if (!stop)
   {
      state_job_done = savestates_job_nothing;
      flip_only = just_flipping;

      vbo_draw();
      co_switch(main_thread);

      return state_job_done;
   }

   return 0;
}

//

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)   { }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }


void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   setup_variables();
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "Mupen64plus";
   info->library_version = "2.0-rc2";
   info->valid_extensions = "n64|v64|z64";
   info->need_fullpath = false;
   info->block_extract = false;
}

// Get the system type associated to a ROM country code.
static m64p_system_type rom_country_code_to_system_type(unsigned short country_code)
{
    switch (country_code)
    {
        // PAL codes
        case 0x44:
        case 0x46:
        case 0x49:
        case 0x50:
        case 0x53:
        case 0x55:
        case 0x58:
        case 0x59:
            return SYSTEM_PAL;

        // NTSC codes
        case 0x37:
        case 0x41:
        case 0x45:
        case 0x4a:
        default: // Fallback for unknown codes
            return SYSTEM_NTSC;
    }
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   // TODO
   struct retro_variable var = { "mupen64-screensize", 0 };
   screen_width = 640;
   screen_height = 480;
   m64p_system_type region = rom_country_code_to_system_type(romgame.Country_code);

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (sscanf(var.value ? var.value : "640x480", "%dx%d", &screen_width, &screen_height) != 2)
      {
         screen_width = 640;
         screen_height = 480;
      }
   }

   info->geometry.base_width = screen_width;
   info->geometry.base_height = screen_height;
   info->geometry.max_width = screen_width;
   info->geometry.max_height = screen_height;
   info->geometry.aspect_ratio = 0.0;
   info->timing.fps = (region == SYSTEM_PAL) ? 50.0 : 60;                // TODO: Actual timing 
   info->timing.sample_rate = 44100.0;
}

unsigned retro_get_region (void)
{
    // TODO
    return RETRO_REGION_NTSC;
}

void retro_init(void)
{
   struct retro_log_callback log;
   unsigned colorMode = RETRO_PIXEL_FORMAT_XRGB8888;

   environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log);
   if (log.log)
      log_cb = log.log;

   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &colorMode);

   rarch_resampler_realloc(&resampler_data, &resampler, NULL, 1.0);
   audio_in_buffer_float = malloc(4096 * sizeof(float));
   audio_out_buffer_float = malloc(4096 * sizeof(float));
   audio_out_buffer_s16 = malloc(4096 * sizeof(int16_t));
   audio_convert_init_simd();

   environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble);
}

void retro_deinit(void)
{
#ifdef PERF_TEST
   rarch_perf_log();
#endif

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
static bool frame_dupe = true;

void update_variables(void)
{
   struct retro_variable var;

   var.key = "mupen64-filtering";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "automatic") == 0)
         retro_filtering = 0;
      else if (strcmp(var.value, "bilinear") == 0)
         retro_filtering = 1;
      else if (strcmp(var.value, "nearest") == 0)
         retro_filtering = 2;
   }

   var.key = "mupen64-dupe";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "yes"))
         frame_dupe = true;
      else if (!strcmp(var.value, "no"))
         frame_dupe = false;
   }
   
   
   {
      struct retro_variable pk1var = { "mupen64-pak1" };
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pk1var) && pk1var.value)
      {
         int p1_pak = PLUGIN_NONE;
         if (!strcmp(pk1var.value, "rumble"))
            p1_pak = PLUGIN_RAW;
         else if (!strcmp(pk1var.value, "memory"))
            p1_pak = PLUGIN_MEMPAK;
         
         // If controller struct is not initialised yet, set pad_pak_types instead
         // which will be looked at when initialising the controllers.
         if (controller[0].control)
            controller[0].control->Plugin = p1_pak;
         else
            pad_pak_types[0] = p1_pak;
         
      }
   }

   {
      struct retro_variable pk2var = { "mupen64-pak2" };
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pk2var) && pk2var.value)
      {
         int p2_pak = PLUGIN_NONE;
         if (!strcmp(pk2var.value, "rumble"))
            p2_pak = PLUGIN_RAW;
         else if (!strcmp(pk2var.value, "memory"))
            p2_pak = PLUGIN_MEMPAK;
            
         if (controller[1].control)
            controller[1].control->Plugin = p2_pak;
         else
            pad_pak_types[1] = p2_pak;
         
      }
   }
   
   {
      struct retro_variable pk3var = { "mupen64-pak3" };
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pk3var) && pk3var.value)
      {
         int p3_pak = PLUGIN_NONE;
         if (!strcmp(pk3var.value, "rumble"))
            p3_pak = PLUGIN_RAW;
         else if (!strcmp(pk3var.value, "memory"))
            p3_pak = PLUGIN_MEMPAK;
            
         if (controller[2].control)
            controller[2].control->Plugin = p3_pak;
         else
            pad_pak_types[2] = p3_pak;
         
      }
   }
  
   {
      struct retro_variable pk4var = { "mupen64-pak4" };
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pk4var) && pk4var.value)
      {
         int p4_pak = PLUGIN_NONE;
         if (!strcmp(pk4var.value, "rumble"))
            p4_pak = PLUGIN_RAW;
         else if (!strcmp(pk4var.value, "memory"))
            p4_pak = PLUGIN_MEMPAK;
            
         if (controller[3].control)
            controller[3].control->Plugin = p4_pak;
         else
            pad_pak_types[3] = p4_pak;
      }
   }
}

bool retro_load_game(const struct retro_game_info *game)
{
   format_saved_memory(); // < defined in mupen64plus-core/src/memory/memory.c

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
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "mupen64plus: libretro frontend doesn't have OpenGL support.");
       return false;
    }

    game_data = malloc(game->size);
    memcpy(game_data, game->data, game->size);
    game_size = game->size;

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
   if (no_audio)
      return;

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

unsigned int FAKE_SDL_TICKS;
static bool pushed_frame;
void retro_run (void)
{
    FAKE_SDL_TICKS += 16;
    pushed_frame = false;

    poll_cb();

run_again:
    sglEnter();
    co_switch(emulator_thread);
    sglExit();

    if (flip_only)
    {
        video_cb(RETRO_HW_FRAME_BUFFER_VALID, screen_width, screen_height, 0);
        pushed_frame = true;
        goto run_again;
    }

    if (!pushed_frame && frame_dupe) // Dupe. Not duping violates libretro API, consider it a speedhack.
        video_cb(NULL, screen_width, screen_height, 0);
}

void retro_reset (void)
{
    CoreDoCommand(M64CMD_RESET, 1, (void*)0);
}

void *retro_get_memory_data(unsigned type)
{
   return (type == RETRO_MEMORY_SAVE_RAM) ? &saved_memory : 0;
}

size_t retro_get_memory_size(unsigned type)
{
   return (type == RETRO_MEMORY_SAVE_RAM) ? sizeof(saved_memory) : 0;
}



size_t retro_serialize_size (void)
{
    return 16788288 + 1024; // < 16MB and some change... ouch
}

bool retro_serialize(void *data, size_t size)
{
    if (savestates_save_m64p(data, size))
    {
        state_job_done = savestates_job_save;
        return true;
    }

    return false;
}

bool retro_unserialize(const void * data, size_t size)
{
    if (savestates_load_m64p(data, size))
    {
        state_job_done = savestates_job_load;
        return true;
    }

    return false;
}



// Stubs
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }

void retro_set_controller_port_device(unsigned in_port, unsigned device) { }

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2) { }

