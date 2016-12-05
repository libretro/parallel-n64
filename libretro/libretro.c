#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/libretro.h"
#ifndef SINGLE_THREAD
#include <libco.h>
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include <glsm/glsmsym.h>
#endif

#include "api/m64p_frontend.h"
#include "plugin/plugin.h"
#include "api/m64p_types.h"
#include "r4300/r4300.h"
#include "memory/memory.h"
#include "main/main.h"
#include "main/version.h"
#include "main/savestates.h"
#include "pi/pi_controller.h"
#include "si/pif.h"
#include "libretro_memory.h"

/* Cxd4 RSP */
#include "../mupen64plus-rsp-cxd4/config.h"
#include "plugin/audio_libretro/audio_plugin.h"
#include "../Graphics/plugin.h"

#ifndef PRESCALE_WIDTH
#define PRESCALE_WIDTH  640
#endif

#ifndef PRESCALE_HEIGHT
#define PRESCALE_HEIGHT 625
#endif

/* forward declarations */
int InitGfx(void);
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
int glide64InitGfx(void);
void gles2n64_reset(void);
#endif
#if defined(HAVE_VULKAN)
#include "../mupen64plus-video-paraLLEl/parallel.h"

static struct retro_hw_render_callback hw_render;
static struct retro_hw_render_context_negotiation_interface_vulkan hw_context_negotiation;
static const struct retro_hw_render_interface_vulkan *vulkan;
#endif

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;

retro_log_printf_t log_cb = NULL;
retro_video_refresh_t video_cb = NULL;
retro_input_poll_t poll_cb = NULL;
retro_input_state_t input_cb = NULL;
retro_audio_sample_batch_t audio_batch_cb = NULL;
retro_environment_t environ_cb = NULL;

struct retro_rumble_interface rumble;

save_memory_data saved_memory;

#ifndef SINGLE_THREAD
cothread_t main_thread;
static cothread_t cpu_thread;
#endif

float polygonOffsetFactor;
float polygonOffsetUnits;

int astick_deadzone;
bool flip_only;

static uint8_t* game_data = NULL;
static uint32_t game_size = 0;

static bool     emu_initialized     = false;
static unsigned initial_boot        = true;
static unsigned audio_buffer_size   = 2048;

static unsigned retro_filtering     = 0;
static bool     reinit_screen       = false;
static bool     first_context_reset = false;
static bool     pushed_frame        = false;

unsigned frame_dupe = false;

uint32_t *blitter_buf;
uint32_t *blitter_buf_lock   = NULL;

uint32_t gfx_plugin_accuracy = 2;
static enum rsp_plugin_type rsp_plugin;
uint32_t screen_width = 640;
uint32_t screen_height = 480;
uint32_t screen_pitch;
uint32_t screen_aspectmodehint;

extern unsigned int VI_REFRESH;
unsigned int BUFFERSWAP;
unsigned int FAKE_SDL_TICKS;

bool alternate_mapping;

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
int pad_present[4] = {1, 1, 1, 1};

static void n64DebugCallback(void* aContext, int aLevel, const char* aMessage)
{
    char buffer[1024];
    snprintf(buffer, 1024, "mupen64plus: %s\n", aMessage);
    if (log_cb)
       log_cb(RETRO_LOG_INFO, buffer);
}

extern m64p_rom_header ROM_HEADER;

static void core_settings_autoselect_gfx_plugin(void)
{
   struct retro_variable gfx_var = { NAME_PREFIX "-gfxplugin", 0 };

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var);

   if (gfx_var.value && strcmp(gfx_var.value, "auto") != 0)
      return;

#if defined(HAVE_VULKAN)
   gfx_plugin = GFX_PARALLEL;
#elif defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   gfx_plugin = GFX_GLIDE64;
#else
   gfx_plugin = GFX_ANGRYLION;
#endif
}

unsigned libretro_get_gfx_plugin(void)
{
   return gfx_plugin;
}

static void core_settings_autoselect_rsp_plugin(void);

static void core_settings_set_defaults(void)
{
   /* Load GFX plugin core option */
   struct retro_variable gfx_var = { NAME_PREFIX "-gfxplugin", 0 };
   struct retro_variable rsp_var = { NAME_PREFIX "-rspplugin", 0 };
   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var);
   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &rsp_var);

#ifdef ONLY_VULKAN
   gfx_plugin = GFX_PARALLEL;
#else
   if (gfx_var.value)
   {
      if (gfx_var.value && !strcmp(gfx_var.value, "auto"))
         core_settings_autoselect_gfx_plugin();
      if (gfx_var.value && !strcmp(gfx_var.value, "gln64"))
         gfx_plugin = GFX_GLN64;
      if (gfx_var.value && !strcmp(gfx_var.value, "rice"))
         gfx_plugin = GFX_RICE;
      if(gfx_var.value && !strcmp(gfx_var.value, "glide64"))
         gfx_plugin = GFX_GLIDE64;
	  if(gfx_var.value && !strcmp(gfx_var.value, "angrylion"))
         gfx_plugin = GFX_ANGRYLION;
	  if(gfx_var.value && !strcmp(gfx_var.value, "parallel"))
         gfx_plugin = GFX_PARALLEL;
   }
   else
      gfx_plugin = GFX_GLIDE64;
#endif

   gfx_var.key = NAME_PREFIX "-gfxplugin-accuracy";
   gfx_var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var) && gfx_var.value)
   {
       if (gfx_var.value && !strcmp(gfx_var.value, "veryhigh"))
          gfx_plugin_accuracy = 3;
       else if (gfx_var.value && !strcmp(gfx_var.value, "high"))
          gfx_plugin_accuracy = 2;
       else if (gfx_var.value && !strcmp(gfx_var.value, "medium"))
          gfx_plugin_accuracy = 1;
       else if (gfx_var.value && !strcmp(gfx_var.value, "low"))
          gfx_plugin_accuracy = 0;
   }

   /* Load RSP plugin core option */
#ifdef ONLY_VULKAN
   rsp_plugin = RSP_CXD4;
#else
   rsp_plugin = RSP_HLE;

   if (rsp_var.value)
   {
      if (rsp_var.value && !strcmp(rsp_var.value, "auto"))
         core_settings_autoselect_rsp_plugin();
      if (rsp_var.value && !strcmp(rsp_var.value, "hle"))
         rsp_plugin = RSP_HLE;
      if (rsp_var.value && !strcmp(rsp_var.value, "cxd4"))
         rsp_plugin = RSP_CXD4;
   }
#endif
}



static void core_settings_autoselect_rsp_plugin(void)
{
#if !defined(ONLY_VULKAN)
   struct retro_variable rsp_var = { NAME_PREFIX "-rspplugin", 0 };

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &rsp_var);

   if (rsp_var.value && strcmp(rsp_var.value, "auto") != 0)
      return;

   rsp_plugin = RSP_HLE;

   if (
          (sl(ROM_HEADER.CRC1) == 0x7EAE2488   && sl(ROM_HEADER.CRC2) == 0x9D40A35A) /* Biohazard 2 (J) [!] */
          || (sl(ROM_HEADER.CRC1) == 0x9B500E8E   && sl(ROM_HEADER.CRC2) == 0xE90550B3) /* Resident Evil 2 (E) (M2) [!] */
          || (sl(ROM_HEADER.CRC1) == 0xAA18B1A5   && sl(ROM_HEADER.CRC2) == 0x7DB6AEB)  /* Resident Evil 2 (U) [!] */
          || (!strcmp((const char*)ROM_HEADER.Name, "GAUNTLET LEGENDS"))
      )
   {
      rsp_plugin = RSP_CXD4;
   }

   if (!strcmp((const char*)ROM_HEADER.Name, "CONKER BFD"))
      rsp_plugin = RSP_HLE;
#endif
}

static void setup_variables(void)
{
   struct retro_variable variables[] = {
      { NAME_PREFIX "-cpucore",
#ifdef DYNAREC
#if defined(IOS)
         "CPU Core; cached_interpreter|pure_interpreter|dynamic_recompiler" },
#else
         "CPU Core; dynamic_recompiler|cached_interpreter|pure_interpreter" },
#endif
#else
         "CPU Core; cached_interpreter|pure_interpreter" },
#endif
      {NAME_PREFIX "-audio-buffer-size",
         "Audio Buffer Size (restart); 2048|1024"},
      {NAME_PREFIX "-astick-deadzone",
        "Analog Deadzone (percent); 15|20|25|30|0|5|10"},
      {NAME_PREFIX "-pak1",
        "Player 1 Pak; none|memory|rumble"},
      {NAME_PREFIX "-pak2",
        "Player 2 Pak; none|memory|rumble"},
      {NAME_PREFIX "-pak3",
        "Player 3 Pak; none|memory|rumble"},
      {NAME_PREFIX "-pak4",
        "Player 4 Pak; none|memory|rumble"},
      { NAME_PREFIX "-disable_expmem",
         "Enable Expansion Pak RAM; enabled|disabled" },
      { NAME_PREFIX "-gfxplugin-accuracy",
#if defined(ONLY_VULKAN)
         "GFX Accuracy; veryhigh" },
#elif defined(IOS) || defined(ANDROID)
         "GFX Accuracy (restart); medium|high|veryhigh|low" },
#else
         "GFX Accuracy (restart); veryhigh|high|medium|low" },
#endif
#ifdef HAVE_VULKAN
      { NAME_PREFIX "-parallel-rdp-synchronous",
         "ParaLLEl Synchronous RDP; enabled|disabled" },
#endif
      { NAME_PREFIX "-gfxplugin",
#ifdef ONLY_VULKAN
         "GFX Plugin; parallel" },
#else
         "GFX Plugin; auto|glide64|gln64|rice|angrylion|parallel" },
#endif
      { NAME_PREFIX "-rspplugin",
#ifdef ONLY_VULKAN
         "RSP Plugin; cxd4" },
#else
         "RSP Plugin; auto|hle|cxd4" },
#endif
#ifndef ONLY_VULKAN
      { NAME_PREFIX "-screensize",
         "Resolution (restart); 640x480|960x720|1280x960|1600x1200|1920x1440|2240x1680|320x240" },
      { NAME_PREFIX "-aspectratiohint",
         "Aspect ratio hint (reinit); normal|widescreen" },
      { NAME_PREFIX "-filtering",
		 "Texture Filtering; automatic|N64 3-point|bilinear|nearest" },
      { NAME_PREFIX "-polyoffset-factor",
       "(Glide64) Polygon Offset Factor; -3.0|-2.5|-2.0|-1.5|-1.0|-0.5|0.0|0.5|1.0|1.5|2.0|2.5|3.0|3.5|4.0|4.5|5.0|-3.5|-4.0|-4.5|-5.0"
      },
      { NAME_PREFIX "-polyoffset-units",
       "(Glide64) Polygon Offset Units; -3.0|-2.5|-2.0|-1.5|-1.0|-0.5|0.0|0.5|1.0|1.5|2.0|2.5|3.0|3.5|4.0|4.5|5.0|-3.5|-4.0|-4.5|-5.0"
      },
      { NAME_PREFIX "-angrylion-vioverlay",
       "(Angrylion) VI Overlay; disabled|enabled"
      },
      { NAME_PREFIX "-virefresh",
         "VI Refresh (Overclock); 1500|2200" },
#endif
      { NAME_PREFIX "-bufferswap",
         "Buffer Swap; off|on"
      },
      { NAME_PREFIX "-framerate",
         "Framerate (restart); original|fullspeed" },
      { NAME_PREFIX "-alt-map",
        "Digital C-button Config; disabled|enabled" },
#ifndef ONLY_VULKAN
      { NAME_PREFIX "-vcache-vbo",
         "(Glide64) Vertex cache VBO (restart); off|on" },
#endif
      { NAME_PREFIX "-boot-device",
         "Boot Device; Default|64DD IPL" },
      { NAME_PREFIX "-64dd-hardware",
         "64DD Hardware; disabled|enabled" },
      { NULL, NULL },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}


static bool emu_step_load_data()
{
   if(CoreStartup(FRONTEND_API_VERSION, ".", ".", "Core", n64DebugCallback, 0, 0) && log_cb)
       log_cb(RETRO_LOG_ERROR, "mupen64plus: Failed to initialize core\n");

   log_cb(RETRO_LOG_INFO, "EmuThread: M64CMD_ROM_OPEN\n");

   if(CoreDoCommand(M64CMD_ROM_OPEN, game_size, (void*)game_data))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "mupen64plus: Failed to load ROM\n");
       goto load_fail;
   }

   free(game_data);
   game_data = NULL;

   log_cb(RETRO_LOG_INFO, "EmuThread: M64CMD_ROM_GET_HEADER\n");

   if(CoreDoCommand(M64CMD_ROM_GET_HEADER, sizeof(ROM_HEADER), &ROM_HEADER))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "mupen64plus; Failed to query ROM header information\n");
      goto load_fail;
   }

   return true;

load_fail:
   free(game_data);
   game_data = NULL;
   stop = 1;

   return false;
}

bool emu_step_render(void)
{
   if (flip_only)
   {
      switch (gfx_plugin)
      {
         case GFX_ANGRYLION:
            video_cb((screen_pitch == 0) ? NULL : blitter_buf_lock, screen_width, screen_height, screen_pitch);
            break;

#if defined(HAVE_VULKAN)
         case GFX_PARALLEL:
            video_cb(parallel_frame_is_valid() ? RETRO_HW_FRAME_BUFFER_VALID : NULL,
                  parallel_frame_width(), parallel_frame_height(), 0);
            break;
#endif

         default:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
            video_cb(RETRO_HW_FRAME_BUFFER_VALID, screen_width, screen_height, 0);
#else
            video_cb((screen_pitch == 0) ? NULL : blitter_buf_lock, screen_width, screen_height, screen_pitch);
#endif
            break;
      }

      pushed_frame = true;
      return true;
   }

   if (!pushed_frame && frame_dupe) // Dupe. Not duping violates libretro API, consider it a speedhack.
      video_cb(NULL, screen_width, screen_height, screen_pitch);

   return false;
}

static void emu_step_initialize(void)
{
   if (emu_initialized)
      return;

   emu_initialized = true;

   core_settings_set_defaults();
   core_settings_autoselect_gfx_plugin();
   core_settings_autoselect_rsp_plugin();

   plugin_connect_all(gfx_plugin, rsp_plugin);

   log_cb(RETRO_LOG_INFO, "EmuThread: M64CMD_EXECUTE. \n");

   CoreDoCommand(M64CMD_EXECUTE, 0, NULL);
}

void reinit_gfx_plugin(void)
{
    if(first_context_reset)
    {
        first_context_reset = false;
#ifdef SINGLE_THREAD
        emu_step_initialize();
#else
        co_switch(cpu_thread);
#endif
    }

#if defined(HAVE_VULKAN)
    switch (gfx_plugin)
    {
       case GFX_PARALLEL:
       {
          if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &vulkan) || !vulkan)
             log_cb(RETRO_LOG_ERROR, "Failed to obtain Vulkan interface.");
          else
             parallel_init(vulkan);
          break;
       }
    }
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
    switch (gfx_plugin)
    {
       case GFX_GLIDE64:
          glide64InitGfx();
          break;
       case GFX_GLN64:
          gles2n64_reset();
          break;
    }
#endif
}

void deinit_gfx_plugin(void)
{
#if defined(HAVE_VULKAN)
    switch (gfx_plugin)
    {
       case GFX_PARALLEL:
          parallel_deinit();
          break;
    }
#endif
}

#ifndef SINGLE_THREAD
static void EmuThreadFunction(void)
{
    if (!emu_step_load_data())
       goto load_fail;

    //ROM is loaded, switch back to main thread so retro_load_game can return (returning failure if needed).
    //We'll continue here once the context is reset.
    co_switch(main_thread);

    emu_step_initialize();

    //Context is reset too, everything is safe to use. Now back to main thread so we don't start pushing frames outside retro_run.
    co_switch(main_thread);

    main_run();
    log_cb(RETRO_LOG_INFO, "EmuThread: co_switch main_thread. \n");

    co_switch(main_thread);

load_fail:
    //NEVER RETURN! That's how libco rolls
    while(1)
    {
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "Running Dead N64 Emulator");
       co_switch(main_thread);
    }
}
#endif

const char* retro_get_system_directory(void)
{
    const char* dir;
    environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir);

    return dir ? dir : ".";
}


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
#ifdef ONLY_VULKAN
   info->library_name = "ParaLLEl";
#else
   info->library_name = "Mupen64Plus";
#endif
   info->library_version = "2.0-rc2";
   info->valid_extensions = "n64|v64|z64|bin|u1|ndd";
   info->need_fullpath = false;
   info->block_extract = false;
}

// Get the system type associated to a ROM country code.
static m64p_system_type rom_country_code_to_system_type(char country_code)
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
   m64p_system_type region = rom_country_code_to_system_type(ROM_HEADER.destination_code);

   info->geometry.base_width   = screen_width;
   info->geometry.base_height  = screen_height;
   info->geometry.max_width    = screen_width;
   info->geometry.max_height   = screen_height;
   info->geometry.aspect_ratio = 4.0 / 3.0;
   info->timing.fps = (region == SYSTEM_PAL) ? 50.0 : (60/1.001);                // TODO: Actual timing 
   info->timing.sample_rate = 44100.0;
}

unsigned retro_get_region (void)
{
   m64p_system_type region = rom_country_code_to_system_type(ROM_HEADER.destination_code);
   return ((region == SYSTEM_PAL) ? RETRO_REGION_PAL : RETRO_REGION_NTSC);
}

void retro_init(void)
{
   struct retro_log_callback log;
   unsigned colorMode = RETRO_PIXEL_FORMAT_XRGB8888;
   screen_pitch = 0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &colorMode);
   environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble);

   blitter_buf = (uint32_t*)calloc(
         PRESCALE_WIDTH * PRESCALE_HEIGHT, sizeof(uint32_t)
         );
   blitter_buf_lock = blitter_buf;

   //hacky stuff for Glide64
   polygonOffsetUnits = -3.0f;
   polygonOffsetFactor =  -3.0f;

#ifndef SINGLE_THREAD
   main_thread = co_active();
   cpu_thread = co_create(65536 * sizeof(void*) * 16, EmuThreadFunction);
#endif
} 

void retro_deinit(void)
{
   main_stop();
   main_exit();

   if (blitter_buf)
      free(blitter_buf);
   blitter_buf      = NULL;
   blitter_buf_lock = NULL;

#ifndef SINGLE_THREAD
   co_delete(cpu_thread);
#endif

   deinit_audio_libretro();

   if (perf_cb.perf_log)
      perf_cb.perf_log();
}

#include "../mupen64plus-video-angrylion/vi.h"

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
extern void glide_set_filtering(unsigned value);
#endif
extern void angrylion_set_filtering(unsigned value);
extern void ChangeSize();

static bool parallel_rdp_synchronous = true;

bool is_parallel_rdp_synchronous(void)
{
   return parallel_rdp_synchronous;
}

void update_variables(bool startup)
{
   struct retro_variable var;

#if defined(HAVE_VULKAN)
   var.key = NAME_PREFIX "-parallel-rdp-synchronous";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         parallel_rdp_synchronous = true;
      else
         parallel_rdp_synchronous = false;
   }
   else
      parallel_rdp_synchronous = true;
#endif

   var.key = NAME_PREFIX "-screensize";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      /* TODO/FIXME - hack - force screen width and height back to 640x480 in case
       * we change it with Angrylion. If we ever want to support variable resolution sizes in Angrylion
       * then we need to drop this. */
      if (gfx_plugin == GFX_ANGRYLION || sscanf(var.value ? var.value : "640x480", "%dx%d", &screen_width, &screen_height) != 2)
      {
         screen_width = 640;
         screen_height = 480;
      }
   }
   else
   {
      screen_width  = 640;
      screen_height = 480;
   }

   if (startup)
   {
      var.key = NAME_PREFIX "-audio-buffer-size";
      var.value = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         audio_buffer_size = atoi(var.value);

      var.key = NAME_PREFIX "-gfxplugin";
      var.value = NULL;

      environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

      if (var.value)
      {
         if (!strcmp(var.value, "auto"))
            core_settings_autoselect_gfx_plugin();
         if (!strcmp(var.value, "gln64"))
            gfx_plugin = GFX_GLN64;
         if (!strcmp(var.value, "rice"))
            gfx_plugin = GFX_RICE;
         if(!strcmp(var.value, "glide64"))
            gfx_plugin = GFX_GLIDE64;
         if(!strcmp(var.value, "angrylion"))
            gfx_plugin = GFX_ANGRYLION;
         if(!strcmp(var.value, "parallel"))
            gfx_plugin = GFX_PARALLEL;
      }
      else
         gfx_plugin = GFX_GLIDE64;
   }

   var.key = NAME_PREFIX "-angrylion-vioverlay";
   var.value = NULL;

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

   if (var.value)
   {
      if(!strcmp(var.value, "enabled"))
         overlay = 1;
      else if(!strcmp(var.value, "disabled"))
         overlay = 0;
   }
   else
      overlay = 1;

   CFG_HLE_GFX = (gfx_plugin != GFX_ANGRYLION) && (gfx_plugin != GFX_PARALLEL) ? 1 : 0;
   CFG_HLE_AUD = 0; /* There is no HLE audio code in libretro audio plugin. */

   var.key = NAME_PREFIX "-filtering";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	  if (!strcmp(var.value, "automatic"))
		  retro_filtering = 0;
	  else if (!strcmp(var.value, "N64 3-point"))
#ifdef DISABLE_3POINT
		  retro_filtering = 3;
#else
		  retro_filtering = 1;
#endif
	  else if (!strcmp(var.value, "nearest"))
		  retro_filtering = 2;
	  else if (!strcmp(var.value, "bilinear"))
		  retro_filtering = 3;

     log_cb(RETRO_LOG_DEBUG, "set filtering mode...\n");
     switch (gfx_plugin)
     {
        case GFX_GLIDE64:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
           glide_set_filtering(retro_filtering);
#endif
           break;
        case GFX_ANGRYLION:
           angrylion_set_filtering(retro_filtering);
           break;
     }
   }

   var.key = NAME_PREFIX "-polyoffset-factor";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      float new_val = (float)atoi(var.value);
      polygonOffsetFactor = new_val;
   }

   var.key = NAME_PREFIX "-polyoffset-units";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      float new_val = (float)atoi(var.value);
      polygonOffsetUnits = new_val;
   }

   var.key = NAME_PREFIX "-astick-deadzone";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      astick_deadzone = (int)(atoi(var.value) * 0.01f * 0x8000);

   var.key = NAME_PREFIX "-gfxplugin-accuracy";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
       if (var.value && !strcmp(var.value, "veryhigh"))
          gfx_plugin_accuracy = 3;
       else if (var.value && !strcmp(var.value, "high"))
          gfx_plugin_accuracy = 2;
       else if (var.value && !strcmp(var.value, "medium"))
          gfx_plugin_accuracy = 1;
       else if (var.value && !strcmp(var.value, "low"))
          gfx_plugin_accuracy = 0;
   }

   var.key = NAME_PREFIX "-virefresh";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "1500"))
         VI_REFRESH = 1500;
      else if (!strcmp(var.value, "2200"))
         VI_REFRESH = 2200;
   }

   var.key = NAME_PREFIX "-bufferswap";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "on"))
         BUFFERSWAP = true;
      else if (!strcmp(var.value, "off"))
         BUFFERSWAP = false;
   }

   var.key = NAME_PREFIX "-framerate";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && initial_boot)
   {
      if (!strcmp(var.value, "original"))
         frame_dupe = false;
      else if (!strcmp(var.value, "fullspeed"))
         frame_dupe = true;
   }

   var.key = NAME_PREFIX "-alt-map";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && startup)
   {
      if (!strcmp(var.value, "disabled"))
         alternate_mapping = false;
      else if (!strcmp(var.value, "enabled"))
         alternate_mapping = true;
   }

   
   {
      struct retro_variable pk1var = { NAME_PREFIX "-pak1" };
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
      struct retro_variable pk2var = { NAME_PREFIX "-pak2" };
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
      struct retro_variable pk3var = { NAME_PREFIX "-pak3" };
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
      struct retro_variable pk4var = { NAME_PREFIX "-pak4" };
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

static void format_saved_memory(void)
{
   format_sram(saved_memory.sram);
   format_eeprom(saved_memory.eeprom, sizeof(saved_memory.eeprom));
   format_flashram(saved_memory.flashram);
   format_mempak(saved_memory.mempack[0]);
   format_mempak(saved_memory.mempack[1]);
   format_mempak(saved_memory.mempack[2]);
   format_mempak(saved_memory.mempack[3]);
}

#if defined(HAVE_VULKAN) || defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
static void context_reset(void)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   if (gfx_plugin != GFX_ANGRYLION && gfx_plugin != GFX_PARALLEL)
   {
      static bool first_init = true;
      printf("context_reset.\n");
      glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

      if (first_init)
      {
         glsm_ctl(GLSM_CTL_STATE_SETUP, NULL);
         first_init = false;
      }
   }
#endif

   reinit_gfx_plugin();
}

static void context_destroy(void)
{
   deinit_gfx_plugin();
}
#endif

static bool context_framebuffer_lock(void *data)
{
   if (!stop)
      return false;
   return true;
}

bool retro_load_game(const struct retro_game_info *game)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   glsm_ctx_params_t params = {0};
#endif
   format_saved_memory();

   update_variables(true);
   initial_boot = false;

   init_audio_libretro(audio_buffer_size);

#if defined(HAVE_VULKAN)
   if (gfx_plugin == GFX_PARALLEL)
   {
      hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
      hw_render.version_major = VK_MAKE_VERSION(1, 0, 12);
      hw_render.context_reset = context_reset;
      hw_render.context_destroy = context_destroy;
      if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      {
         log_cb(RETRO_LOG_ERROR, "mupen64plus: libretro frontend doesn't have Vulkan support.");
      }

      hw_context_negotiation.interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
      hw_context_negotiation.interface_version = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION;
      hw_context_negotiation.get_application_info = parallel_get_application_info;
      hw_context_negotiation.create_device = parallel_create_device;
      hw_context_negotiation.destroy_device = NULL;
      if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, &hw_context_negotiation))
      {
         log_cb(RETRO_LOG_ERROR, "mupen64plus: libretro frontend doesn't have context negotiation support.");
      }
   }
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   if (gfx_plugin != GFX_ANGRYLION && gfx_plugin != GFX_PARALLEL)
   {
      params.context_reset         = context_reset;
      params.context_destroy       = context_destroy;
      params.environ_cb            = environ_cb;
      params.stencil               = false;

      params.framebuffer_lock      = context_framebuffer_lock;

      if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
      {
         if (log_cb)
            log_cb(RETRO_LOG_ERROR, "mupen64plus: libretro frontend doesn't have OpenGL support.");
         return false;
      }
   }
#endif

   game_data = malloc(game->size);
   memcpy(game_data, game->data, game->size);
   game_size = game->size;

#ifdef SINGLE_THREAD
   if (!emu_step_load_data())
      return false;
#else
   stop = false;
   //Finish ROM load before doing anything funny, so we can return failure if needed.
   co_switch(cpu_thread);
   if (stop) return false;
#endif

   first_context_reset = true;

   return true;
}

void retro_unload_game(void)
{
    stop = 1;

#ifndef SINGLE_THREAD
    co_switch(cpu_thread);
#endif

    CoreDoCommand(M64CMD_ROM_CLOSE, 0, NULL);
    emu_initialized = false;
}

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
static void glsm_exit(void)
{
#ifndef HAVE_SHARED_CONTEXT
   if (gfx_plugin == GFX_ANGRYLION || gfx_plugin == GFX_PARALLEL || stop)
      return;
   glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
#endif
}

static void glsm_enter(void)
{
#ifndef HAVE_SHARED_CONTEXT
   if (gfx_plugin == GFX_ANGRYLION || gfx_plugin == GFX_PARALLEL || stop)
      return;
   glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
#endif
}
#endif

void retro_run (void)
{
   static bool updated = false;

   blitter_buf_lock = blitter_buf;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      static float last_aspect = 4.0 / 3.0;
      struct retro_variable var;

      update_variables(false);

      var.key = NAME_PREFIX "-aspectratiohint";
      var.value = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         float aspect_val = 4.0 / 3.0;
         float aspectmode = 0;

         if (!strcmp(var.value, "widescreen"))
         {
            aspect_val = 16.0 / 9.0;
            aspectmode = 1;
         }
         else if (!strcmp(var.value, "normal"))
         {
            aspect_val = 4.0 / 3.0;
            aspectmode = 0;
         }

         if (aspect_val != last_aspect)
         {
            screen_aspectmodehint = aspectmode;

            switch (gfx_plugin)
            {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
               case GFX_GLIDE64:
                  ChangeSize();
                  break;
#endif
               default:
                  break;
            }

            last_aspect = aspect_val;
            reinit_screen = true;
         }
      }
   }

#ifdef SINGLE_THREAD
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   switch (gfx_plugin)
   {
      case GFX_PARALLEL:
      case GFX_ANGRYLION:
         if (!emu_initialized)
            emu_step_initialize();
         break;
   }
#else
   if (!emu_initialized)
      emu_step_initialize();
#endif
#endif

   FAKE_SDL_TICKS += 16;
   pushed_frame = false;

   if (reinit_screen)
   {
      bool ret;
      struct retro_system_av_info info;
      retro_get_system_av_info(&info);
      switch (screen_aspectmodehint)
      {
         case 0:
            info.geometry.aspect_ratio = 4.0 / 3.0;
            break;
         case 1:
            info.geometry.aspect_ratio = 16.0 / 9.0;
            break;
      }
      ret = environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info.geometry);
      reinit_screen = false;
   }

   do
   {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      if (gfx_plugin != GFX_ANGRYLION && gfx_plugin != GFX_PARALLEL)
         glsm_enter();
#endif

#if defined(HAVE_VULKAN)
      if (gfx_plugin == GFX_PARALLEL)
         parallel_begin_frame();
#endif

#ifdef SINGLE_THREAD
      stop = 0;
      main_run();
      stop = 0;
#else
      co_switch(cpu_thread);
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      if (gfx_plugin != GFX_ANGRYLION && gfx_plugin != GFX_PARALLEL)
         glsm_exit();
#endif
   } while (emu_step_render());
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
        return true;

    return false;
}

bool retro_unserialize(const void * data, size_t size)
{
    if (savestates_load_m64p(data, size))
        return true;

    return false;
}

//Needed to be able to detach controllers for Lylat Wars multiplayer
//Only sets if controller struct is initialised as addon paks do.
void retro_set_controller_port_device(unsigned in_port, unsigned device) {
    if (in_port < 4){
        switch(device)
        {
            case RETRO_DEVICE_NONE:
                if (controller[in_port].control){
                    controller[in_port].control->Present = 0;
                    break;
                } else {
                    pad_present[in_port] = 0;
                    break;
                }
                
            case RETRO_DEVICE_JOYPAD:
            default:
                if (controller[in_port].control){
                    controller[in_port].control->Present = 1;
                    break;
                } else {
                    pad_present[in_port] = 1;
                    break;
                }
        }
    }
}

// Stubs
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2) { }

#ifdef SINGLE_THREAD
bool emu_step_render(void);

int retro_return(int just_flipping)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   vbo_disable();
#endif

   flip_only = just_flipping;

   if (just_flipping)
   {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      glsm_exit();
#endif

      emu_step_render();

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      glsm_enter();
#endif
   }

   stop = 1;
   return 0;
}
#else
int retro_return(int just_flipping)
{
   if (stop)
      return 0;

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   vbo_disable();
#endif

   flip_only = just_flipping;

   co_switch(main_thread);

   return 0;
}
#endif
