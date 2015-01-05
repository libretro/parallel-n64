#include <stdio.h>
#include <SDL_opengles2.h>
#include <string.h>

#include "libretro.h"
#ifndef SINGLE_THREAD
#include "libco/libco.h"
#endif

#include "api/m64p_frontend.h"
#include "plugin/plugin.h"
#include "api/m64p_types.h"
#include "r4300/r4300.h"
#include "memory/dma.h"
#include "memory/memory.h"
#include "main/main.h"
#include "main/version.h"
#include "main/savestates.h"

/* Cxd4 RSP */
#include "../mupen64plus-rsp-cxd4/config.h"

static void EmuThreadFunction(void);
int glide64InitGfx(void);
void gles2n64_reset(void);

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;

retro_log_printf_t log_cb = NULL;
retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
retro_input_state_t input_cb = NULL;
retro_audio_sample_batch_t audio_batch_cb = NULL;
retro_environment_t environ_cb = NULL;

struct retro_rumble_interface rumble;

#ifdef SINGLE_THREAD
void dyna_start(void *code);
void dyna_jump(void);
#else
cothread_t main_thread;
static cothread_t cpu_thread;
#endif

float polygonOffsetFactor;
float polygonOffsetUnits;

int astick_deadzone;
bool flip_only;

static uint8_t* game_data;
static uint32_t game_size;

extern uint32_t *blitter_buf;

enum gfx_plugin_type gfx_plugin;
uint32_t gfx_plugin_accuracy = 2;
static enum rsp_plugin_type rsp_plugin;
uint32_t screen_width;
uint32_t screen_height;
uint32_t screen_pitch;

static bool first_context_reset;

extern unsigned int VI_REFRESH;

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
   struct retro_variable gfx_var = { "mupen64-gfxplugin", 0 };

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var);

   if (gfx_var.value && strcmp(gfx_var.value, "auto") != 0)
      return;

   gfx_plugin = GFX_GLIDE64;
}

unsigned libretro_get_gfx_plugin(void)
{
   return gfx_plugin;
}

static void core_settings_autoselect_rsp_plugin(void);

static void core_settings_set_defaults(void)
{
   /* Load GFX plugin core option */
   struct retro_variable gfx_var = { "mupen64-gfxplugin", 0 };
   struct retro_variable rsp_var = { "mupen64-rspplugin", 0 };
   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gfx_var);
   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &rsp_var);

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
   }
   else
      gfx_plugin = GFX_GLIDE64;

   gfx_var.key = "mupen64-gfxplugin-accuracy";
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
}



static void core_settings_autoselect_rsp_plugin(void)
{
   struct retro_variable rsp_var = { "mupen64-rspplugin", 0 };

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
}

static void setup_variables(void)
{
   struct retro_variable variables[] = {
      { "mupen64-cpucore",
#ifdef DYNAREC
         "CPU Core; dynamic_recompiler|cached_interpreter|pure_interpreter" },
#else
         "CPU Core; cached_interpreter|pure_interpreter" },
#endif
      {"mupen64-astick-deadzone",
        "Analog Deadzone (percent); 15|20|25|30|0|5|10"},
      {"mupen64-pak1",
        "Player 1 Pak; none|memory|rumble"},
      {"mupen64-pak2",
        "Player 2 Pak; none|memory|rumble"},
      {"mupen64-pak3",
        "Player 3 Pak; none|memory|rumble"},
      {"mupen64-pak4",
        "Player 4 Pak; none|memory|rumble"},
      { "mupen64-enable_expmem",
         "Enable Expansion Pak RAM; enabled|disabled" },
      { "mupen64-gfxplugin-accuracy",
#ifdef HAVE_OPENGLES2
         "GFX Accuracy (restart); medium|high|veryhigh|low" },
#else
         "GFX Accuracy (restart); high|veryhigh|medium|low" },
#endif
      { "mupen64-gfxplugin",
         "GFX Plugin; auto|glide64|gln64|rice|angrylion" },
      { "mupen64-rspplugin",
         "RSP Plugin; auto|hle|cxd4" },
      { "mupen64-screensize",
         "Resolution (restart); 640x480|320x200|320x240|400x256|512x384|640x200|640x350|640x400|800x600|960x720|856x480|512x256|1024x768|1280x1024|1600x1200|400x300|1152x864|1280x960|1600x1024|1920x1440|2048x1536|2048x2048" },
      { "mupen64-filtering",
		 "Texture Filtering; automatic|N64 3-point|bilinear|nearest" },
      { "mupen64-polyoffset-factor",
       "(Glide64) Polygon Offset Factor; -3.0|-2.5|-2.0|-1.5|-1.0|-0.5|0.0|0.5|1.0|1.5|2.0|2.5|3.0|3.5|4.0|4.5|5.0|-3.5|-4.0|-4.5|-5.0"
      },
      { "mupen64-polyoffset-units",
       "(Glide64) Polygon Offset Units; -3.0|-2.5|-2.0|-1.5|-1.0|-0.5|0.0|0.5|1.0|1.5|2.0|2.5|3.0|3.5|4.0|4.5|5.0|-3.5|-4.0|-4.5|-5.0"
      },
      { "mupen64-angrylion-vioverlay",
       "(Angrylion) VI Overlay; disabled|enabled"
      },
      { "mupen64-virefresh",
         "VI Refresh (Overclock); 1500|2200" },
      { "mupen64-framerate",
         "Framerate (restart); original|fullspeed" },
      { NULL, NULL },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

void reinit_gfx_plugin(void)
{
    if(first_context_reset)
    {
        first_context_reset=false;
#ifdef SINGLE_THREAD
        EmuThreadFunction();
#else
        co_switch(cpu_thread);
#endif
    }

   if (gfx_plugin == GFX_GLIDE64)
      glide64InitGfx();
   else if (gfx_plugin == GFX_GLN64)
      gles2n64_reset();
}

static void EmuThreadFunction(void)
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

#ifndef SINGLE_THREAD
    //ROM is loaded, switch back to main thread so retro_load_game can return (returning failure if needed).
    //We'll continue here once the context is reset.
    co_switch(main_thread);
#endif

    core_settings_set_defaults();
    core_settings_autoselect_gfx_plugin();
    core_settings_autoselect_rsp_plugin();

    plugin_connect_all(gfx_plugin, rsp_plugin);

    log_cb(RETRO_LOG_INFO, "EmuThread: M64CMD_EXECUTE. \n");

    CoreDoCommand(M64CMD_EXECUTE, 0, NULL);

#ifndef SINGLE_THREAD
    //Context is reset too, everything is safe to use. Now back to main thread so we don't start pushing frames outside retro_run.
    co_switch(main_thread);
#endif

#ifdef SINGLE_THREAD
    return;
#else
    main_run();
    log_cb(RETRO_LOG_INFO, "EmuThread: co_switch main_thread. \n");

    co_switch(main_thread);
#endif

load_fail:
    free(game_data);
    game_data = NULL;
    stop = 1;

#ifndef SINGLE_THREAD
    //NEVER RETURN! That's how libco rolls
    while(1)
    {
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "Running Dead N64 Emulator");
       co_switch(main_thread);
    }
#endif
}

//

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
   m64p_system_type region = rom_country_code_to_system_type(ROM_HEADER.Country_code);

   info->geometry.base_width = screen_width;
   info->geometry.base_height = screen_height;
   info->geometry.max_width = screen_width;
   info->geometry.max_height = screen_height;
   info->geometry.aspect_ratio = 0.0;
   info->timing.fps = (region == SYSTEM_PAL) ? 50.0 : (60/1.001);                // TODO: Actual timing 
   info->timing.sample_rate = 44100.0;
}

unsigned retro_get_region (void)
{
   m64p_system_type region = rom_country_code_to_system_type(ROM_HEADER.Country_code);
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

#ifndef SINGLE_THREAD
   co_delete(cpu_thread);
#endif

   if (perf_cb.perf_log)
      perf_cb.perf_log();
}

unsigned int retro_filtering = 0;
unsigned int frame_dupe = false;
unsigned int initial_boot = true;

#include "../angrylionrdp/vi.h"

extern void glide_set_filtering(unsigned value);

void update_variables(bool startup)
{
   struct retro_variable var;

   var.key = "mupen64-screensize";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (sscanf(var.value ? var.value : "640x480", "%dx%d", &screen_width, &screen_height) != 2)
      {
         screen_width = 640;
         screen_height = 480;
      }
   }

   if (startup)
   {
      var.key = "mupen64-gfxplugin";
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
      }
      else
         gfx_plugin = GFX_GLIDE64;
   }

   var.key = "mupen64-enable_expmem";
   var.value = NULL;

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

   if (var.value)
   {
      if(!strcmp(var.value, "enabled"))
         dma_enable_expmem(1);
      if(!strcmp(var.value, "disabled"))
         dma_enable_expmem(0);
   }
   else
      dma_enable_expmem(1);
   

   var.key = "mupen64-angrylion-vioverlay";
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

   CFG_HLE_GFX = (gfx_plugin != GFX_ANGRYLION) ? 1 : 0;
   CFG_HLE_AUD = 0; /* There is no HLE audio code in libretro audio plugin. */

   var.key = "mupen64-filtering";
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
	  if (gfx_plugin == GFX_GLIDE64)
      {
          log_cb(RETRO_LOG_DEBUG, "set glide filtering mode\n");
		  glide_set_filtering(retro_filtering);
      }
   }

   var.key = "mupen64-polyoffset-factor";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      float new_val = (float)atoi(var.value);
      polygonOffsetFactor = new_val;
   }

   var.key = "mupen64-polyoffset-units";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      float new_val = (float)atoi(var.value);
      polygonOffsetUnits = new_val;
   }

   var.key = "mupen64-astick-deadzone";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      astick_deadzone = (int)(atoi(var.value) * 0.01f * 0x8000);

   var.key = "mupen64-gfxplugin-accuracy";
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

   var.key = "mupen64-virefresh";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "1500"))
         VI_REFRESH = 1500;
      else if (!strcmp(var.value, "2200"))
         VI_REFRESH = 2200;
   }

   var.key = "mupen64-framerate";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && initial_boot)
   {
      if (!strcmp(var.value, "original"))
         frame_dupe = false;
      else if (!strcmp(var.value, "fullspeed"))
         frame_dupe = true;
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
   struct retro_hw_render_callback *render = NULL;
   format_saved_memory(); // < defined in mupen64plus-core/src/memory/memory.c

   update_variables(true);
   initial_boot = false;

   if ((render = (struct retro_hw_render_callback*)retro_gl_init()))
   {
      if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, render))
      {
         if (log_cb)
            log_cb(RETRO_LOG_ERROR, "mupen64plus: libretro frontend doesn't have OpenGL support.");
         return false;
      }
   }

   game_data = malloc(game->size);
   memcpy(game_data, game->data, game->size);
   game_size = game->size;

#ifndef SINGLE_THREAD
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
}

unsigned int FAKE_SDL_TICKS;
static bool pushed_frame;
void retro_run (void)
{
   static bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables(false);

   FAKE_SDL_TICKS += 16;
   pushed_frame = false;

   poll_cb();

run_again:

#ifndef HAVE_SHARED_CONTEXT
   sglEnter();
#endif

#ifdef SINGLE_THREAD
   stop = 0;
   main_run();
   stop = 0;
#else
   co_switch(cpu_thread);
#endif


#ifndef HAVE_SHARED_CONTEXT
   sglExit();
#endif

   if (flip_only)
   {
      if (gfx_plugin == GFX_ANGRYLION)
         video_cb((screen_pitch == 0) ? NULL : blitter_buf, screen_width, screen_height, screen_pitch); 
#ifndef SINGLE_THREAD
      else
         video_cb(RETRO_HW_FRAME_BUFFER_VALID, screen_width, screen_height, 0);
#endif
      pushed_frame = true;
      goto run_again;
   }

   if (!pushed_frame && frame_dupe) // Dupe. Not duping violates libretro API, consider it a speedhack.
      video_cb(NULL, screen_width, screen_height, screen_pitch);
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

