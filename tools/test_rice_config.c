/* Rice's configuration (mupen64plus-video-rice/src/RiceConfig.c), on a
 * configuration store and frontend of its own, held to what it did as
 * C++:
 *
 *   * the configuration sections must open, and must be open to be read;
 *   * the screen size from the frontend's variable, 640x480 without one
 *     or for one it cannot read;
 *   * the game hacks by game name: each name the table knows, in other
 *     cases, with more after it, cut short, inside other names;
 *   * a ROM's options and the frame-buffer options from them, for every
 *     ROM setting against random defaults;
 *   * a ROM's name from its header, its trailing spaces cut;
 *   * the TV system of every country code.
 *
 * Each part folds what it leaves behind into a hash; the table below is
 * what the C++ gave (built with -DRICE_CFG_GOLDEN against the old file,
 * which prints the hashes instead). */
#include <stdio.h>
#include <string.h>

#include "m64p_types.h"
#include "m64p_config.h"
#include "Config.h"
#include "RenderBase.h"
#include "Video.h"
#include "libretro.h"

/* the option ConvertImage.c reads */
#ifdef __cplusplus
extern "C"
#endif
bool RiceConfig_UseFullTMEM(void);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- the frontend ---- */
uint32_t screen_width, screen_height;
PluginStatus status;

static const char *screensize;   /* the frontend's variable, or none */
static int env_refuses;          /* the frontend says no, value or not */

static bool test_environ(unsigned cmd, void *data)
{
   struct retro_variable *var = (struct retro_variable *)data;
   if (cmd != RETRO_ENVIRONMENT_GET_VARIABLE || strcmp(var->key, "parallel-n64-screensize") != 0)
      return false;
   var->value = screensize;
   return !env_refuses && screensize != NULL;
}
retro_environment_t environ_cb = test_environ;

#ifdef RICE_CFG_GOLDEN
/* what the C++ set that nothing read */
retro_get_cpu_features_t perf_get_cpu_features_cb;
void (*ProcessVertexData)(uint32_t dwAddr, uint32_t dwV0, uint32_t dwNum);
void ProcessVertexDataNoSSE(uint32_t dwAddr, uint32_t dwV0, uint32_t dwNum) { (void)dwAddr; (void)dwV0; (void)dwNum; }
#endif

/* ---- a configuration store ---- */
#define MAX_PARAMS 64
static struct { const char *name; int value; } params[MAX_PARAMS];
static int n_params;
static int fail_open;   /* 1: Video-General will not open, 2: Video-Rice */
static uint32_t help_hash = 2166136261u;

static int param_index(const char *name)
{
   int i;
   for (i = 0; i < n_params; i++)
      if (strcmp(params[i].name, name) == 0)
         return i;
   return -1;
}

static void set_param(const char *name, int value)
{
   int i = param_index(name);
   if (i < 0)
   {
      i = n_params++;
      params[i].name = name;
   }
   params[i].value = value;
}

m64p_error ConfigOpenSection(const char *name, m64p_handle *handle)
{
   if ((fail_open == 1 && strcmp(name, "Video-General") == 0)
         || (fail_open == 2 && strcmp(name, "Video-Rice") == 0))
      return M64ERR_INPUT_NOT_FOUND;
   *handle = (m64p_handle)name;
   return M64ERR_SUCCESS;
}

m64p_error ConfigSetDefaultInt(m64p_handle h, const char *name, int value, const char *help)
{
   const char *c;
   (void)h;
   for (c = help; *c; c++)
      help_hash = (help_hash ^ (unsigned char)*c) * 16777619u;
   if (param_index(name) < 0)
      set_param(name, value);
   return M64ERR_SUCCESS;
}

m64p_error ConfigSetDefaultBool(m64p_handle h, const char *name, int value, const char *help)
{
   return ConfigSetDefaultInt(h, name, value, help);
}

int ConfigGetParamInt(m64p_handle h, const char *name)
{
   int i = param_index(name);
   (void)h;
   return i < 0 ? 7 : params[i].value;   /* a name never set reads as 7 */
}

int ConfigGetParamBool(m64p_handle h, const char *name)
{
   return ConfigGetParamInt(h, name) != 0;
}

/* ---- hashing what is left behind ---- */
static uint32_t fold(uint32_t h, const void *p, size_t n)
{
   const unsigned char *b = (const unsigned char *)p;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ b[i]) * 16777619u;
   return h;
}

static uint32_t fold_options(uint32_t h)
{
   h = fold(h, &options, sizeof(options));
   h = fold(h, &defaultRomOptions, sizeof(defaultRomOptions));
   h = fold(h, &currentRomOptions, sizeof(currentRomOptions));
   h = fold(h, &frameBufferOptions, sizeof(frameBufferOptions));
   h = fold(h, &windowSetting.uDisplayWidth, sizeof(windowSetting.uDisplayWidth));
   h = fold(h, &windowSetting.uDisplayHeight, sizeof(windowSetting.uDisplayHeight));
   h = fold(h, &windowSetting.bVerticalSync, sizeof(windowSetting.bVerticalSync));
   h = fold(h, &screen_width, sizeof(screen_width));
   h = fold(h, &screen_height, sizeof(screen_height));
   return h;
}

static uint32_t rng = 0x12345678u;
static uint32_t next(void)
{
   rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
   return rng;
}

/* the names the hack table goes by */
static const char *const names[] =
{
   "BANJO TOOIE", "Baseball", "Bust", "CONKER", "Command&Conquer",
   "DR.MARIO", "DUKE NUKEM", "Extreme G 2", "F-ZERO", "Fighting Force",
   "GOLDENEYE", "Honey", "MARIOKART64", "MASK", "MK_MYTHOLOGIES",
   "MLB FEATURING", "MarioGolf64", "MarioTennis", "Move", "NASCAR",
   "NITRO", "Ogre", "POLARISSNOCROSS", "Pilot", "READY 2 RUMBLE",
   "READY to RUMBLE", "SPIDERMAN", "SUPER BOWLING", "South Park Rally",
   "Squadron", "Star", "THPS", "TONY HAWK", "TOP GEAR RALLY", "TWINE",
   "Tigger", "YOSHI", "ZELDA",
   /* the pairs looked for together */
   "ZELDA MASK", "THE LEGEND OF ZELDA", "ZELDA MAJORA'S MASK",
   "Baseball Star", "All-Star Baseball", "Tigger's Honey Hunt",
   "Bust A Move", "Bust-A-Move 2", "Mega Ogre", "Rogue Squadron",
   "TWINE 007", "SUPER MARIO 64", "", "Z"
};

static void set_name(const char *name)
{
   memset(g_curRomInfo.szGameName, 0, sizeof(g_curRomInfo.szGameName));
   strncpy((char *)g_curRomInfo.szGameName, name, sizeof(g_curRomInfo.szGameName) - 1);
}

static uint32_t run_open(void)
{
   uint32_t h = 2166136261u;
   bool r;
   int i;

   /* not open yet */
   r = LoadConfiguration();
   CHECK(!r);
   h = fold(h, &r, sizeof(r));

   fail_open = 1;
   r = InitConfiguration();
   CHECK(!r);
   CHECK(!LoadConfiguration());
   /* one section open is not enough */
   fail_open = 2;
   r = InitConfiguration();
   CHECK(!r);
   CHECK(!LoadConfiguration());
   fail_open = 0;
   r = InitConfiguration();
   CHECK(r);
   h = fold(h, &r, sizeof(r));
   /* every default it sets */
   h = fold(h, &n_params, sizeof(n_params));
   h = fold(h, &help_hash, sizeof(help_hash));
   for (i = 0; i < n_params; i++)
   {
      h = fold(h, params[i].name, strlen(params[i].name));
      h = fold(h, &params[i].value, sizeof(params[i].value));
   }
   return h;
}

static uint32_t run_screen(void)
{
   static const char *const sizes[] = { "1280x960", "320x240", "abc", "800", "x600", "0x0", NULL };
   uint32_t h = 2166136261u;
   size_t i;

   for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
   {
      int round, p;
      for (round = 0; round < 3; round++)
      {
         /* each setting its own value, so each read is seen */
         for (p = 0; p < n_params; p++)
            params[p].value = (int)(next() % 1000);
         memset(&options, 0x5A, sizeof(options));
         memset(&defaultRomOptions, 0x5A, sizeof(defaultRomOptions));
         screensize = sizes[i];
         CHECK(LoadConfiguration());
         h = fold_options(h);
      }
   }
   screensize = "1280x960";
   LoadConfiguration();
   CHECK(screen_width == 1280 && screen_height == 960);
   CHECK(windowSetting.uDisplayWidth == 1280 && windowSetting.uDisplayHeight == 960);
   screensize = "abc";
   LoadConfiguration();
   CHECK(screen_width == 640 && screen_height == 480);
   screensize = NULL;
   LoadConfiguration();
   CHECK(screen_width == 640 && screen_height == 480);
   /* a value the frontend says it has not */
   screensize = "1280x960";
   env_refuses = 1;
   LoadConfiguration();
   CHECK(screen_width == 640 && screen_height == 480);
   h = fold_options(h);
   env_refuses = 0;
   return h;
}

static void lower(char *d, const char *s)
{
   for (; *s; s++, d++)
      *d = (*s >= 'A' && *s <= 'Z') ? (char)(*s - 'A' + 'a') : *s;
   *d = 0;
}

static void upper(char *d, const char *s)
{
   for (; *s; s++, d++)
      *d = (*s >= 'a' && *s <= 'z') ? (char)(*s - 'a' + 'A') : *s;
   *d = 0;
}

static uint32_t run_names(void)
{
   uint32_t h = 2166136261u;
   char buf[64];
   size_t i;
   int v;

   for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
      for (v = 0; v < 6; v++)
      {
         size_t n = strlen(names[i]);
         switch (v)
         {
            case 0: strcpy(buf, names[i]); break;
            case 1: lower(buf, names[i]); break;
            case 2: upper(buf, names[i]); break;
            case 3: sprintf(buf, "%s X", names[i]); break;
            case 4: strcpy(buf, names[i]); if (n) buf[n - 1] = 0; break;
            case 5: sprintf(buf, "A %s", names[i]); break;
         }
         set_name(buf);
         GenerateCurrentRomOptions();
         h = fold(h, &options.enableHackForGames, sizeof(options.enableHackForGames));
         h = fold(h, &frameBufferOptions, sizeof(frameBufferOptions));
      }

   /* a few by what they must give */
   set_name("Pilotwings64");
   GenerateCurrentRomOptions();
   CHECK(options.enableHackForGames == HACK_FOR_PILOT_WINGS);
   set_name("PILOTWINGS64");
   GenerateCurrentRomOptions();
   CHECK(options.enableHackForGames == HACK_FOR_PILOT_WINGS);
   set_name("Pilo");
   GenerateCurrentRomOptions();
   CHECK(options.enableHackForGames == NO_HACK_FOR_GAME);
   set_name("dr.mario 64");   /* DR.MARIO goes by case */
   GenerateCurrentRomOptions();
   CHECK(options.enableHackForGames == NO_HACK_FOR_GAME);
   set_name("MARIOGOLF64");
   GenerateCurrentRomOptions();
   CHECK(options.enableHackForGames == HACK_FOR_MARIO_GOLF);
   CHECK(frameBufferOptions.bIgnoreRenderTextureIfHeightUnknown);
   return h;
}

static uint32_t run_rom_options(void)
{
   uint32_t h = 2166136261u;
   int fb, rt, su, tm, i;

   set_name("SUPER MARIO 64");
   for (fb = 0; fb < 12; fb++)
      for (rt = 0; rt < 7; rt++)
         for (su = 0; su < 9; su++)
            for (tm = 0; tm < 4; tm++)
               for (i = 0; i < 2; i++)
               {
                  uint32_t r = next();
                  g_curRomInfo.dwFrameBufferOption     = fb;
                  g_curRomInfo.dwRenderToTextureOption = rt;
                  g_curRomInfo.dwScreenUpdateSetting   = su;
                  g_curRomInfo.dwFullTMEM              = tm;
                  g_curRomInfo.dwNormalCombiner        = r & 3;
                  g_curRomInfo.dwNormalBlender         = (r >> 2) & 3;
                  g_curRomInfo.dwFastTextureCRC        = (r >> 4) & 3;
                  g_curRomInfo.dwAccurateTextureMapping = (r >> 6) & 3;
                  defaultRomOptions.N64FrameBufferEmuType          = (r >> 8) % 11;
                  defaultRomOptions.N64FrameBufferWriteBackControl = (r >> 12) % 8;
                  defaultRomOptions.N64RenderToTextureEmuType      = (r >> 16) % 6;
                  defaultRomOptions.screenUpdateSetting            = (r >> 20) % 8;
                  options.bFullTMEM = (r >> 24) & 1;
                  memset(&frameBufferOptions, (int)(r >> 25) & 1, sizeof(frameBufferOptions));
                  GenerateCurrentRomOptions();
                  h = fold_options(h);
               }

   /* the ROM's own setting, else the default */
   memset(&defaultRomOptions, 0, sizeof(defaultRomOptions));
   defaultRomOptions.screenUpdateSetting = SCREEN_UPDATE_AT_VI_UPDATE;
   g_curRomInfo.dwScreenUpdateSetting = 0;
   g_curRomInfo.dwFrameBufferOption = 0;
   g_curRomInfo.dwRenderToTextureOption = 0;
   GenerateCurrentRomOptions();
   CHECK(currentRomOptions.screenUpdateSetting == SCREEN_UPDATE_AT_VI_UPDATE);
   CHECK(currentRomOptions.N64FrameBufferEmuType == FRM_BUF_NONE);
   g_curRomInfo.dwFrameBufferOption = FRM_BUF_COMPLETE + 1;
   GenerateCurrentRomOptions();
   /* any frame-buffer emulation is only ignored */
   CHECK(currentRomOptions.N64FrameBufferEmuType == FRM_BUF_IGNORE);
   CHECK(frameBufferOptions.bUpdateCIInfo && frameBufferOptions.bIgnore);
   CHECK(!frameBufferOptions.bProcessCPUWrite && !frameBufferOptions.bCheckBackBufs);

   /* full TMEM: on by the option unless the ROM says, or by the ROM */
   options.bFullTMEM = true;  g_curRomInfo.dwFullTMEM = 0;
   GenerateCurrentRomOptions();
   CHECK(options.bUseFullTMEM);
   g_curRomInfo.dwFullTMEM = 1;
   GenerateCurrentRomOptions();
   CHECK(!options.bUseFullTMEM);
   options.bFullTMEM = false; g_curRomInfo.dwFullTMEM = 2;
   GenerateCurrentRomOptions();
   CHECK(options.bUseFullTMEM);
   CHECK(RiceConfig_UseFullTMEM());
   return h;
}

static uint32_t run_header(void)
{
   static const char *const raw[] = { "SUPER MARIO 64      ", "                    ",
      "ABCDEFGHIJKLMNOPQRST", "A B                 ", "" };
   uint32_t h = 2166136261u;
   ROMHeader hdr;
   unsigned char name[64];
   size_t i;

   for (i = 0; i < sizeof(raw) / sizeof(raw[0]); i++)
   {
      memset(&hdr, 0, sizeof(hdr));
      memset(name, 0xAA, sizeof(name));
      memcpy(hdr.szName, raw[i], strlen(raw[i]));
      ROM_GetRomNameFromHeader(name, &hdr);
      h = fold(h, name, 21);
   }
   memset(&hdr, 0, sizeof(hdr));
   memcpy(hdr.szName, "SUPER MARIO 64      ", 20);
   ROM_GetRomNameFromHeader(name, &hdr);
   CHECK(strcmp((char *)name, "SUPER MARIO 64") == 0);
   memcpy(hdr.szName, "ABCDEFGHIJKLMNOPQRST", 20);
   ROM_GetRomNameFromHeader(name, &hdr);
   CHECK(strcmp((char *)name, "ABCDEFGHIJKLMNOPQRST") == 0);
   return h;
}

static uint32_t run_tv(void)
{
   uint32_t h = 2166136261u, c;
   for (c = 0; c < 256; c++)
   {
      uint32_t tv = CountryCodeToTVSystem(c);
      h = fold(h, &tv, sizeof(tv));
   }
   CHECK(CountryCodeToTVSystem('E') == TV_SYSTEM_NTSC);
   CHECK(CountryCodeToTVSystem('J') == TV_SYSTEM_NTSC);
   CHECK(CountryCodeToTVSystem('P') == TV_SYSTEM_PAL);
   CHECK(CountryCodeToTVSystem('D') == TV_SYSTEM_PAL);
   return h;
}

/* what the C++ gave */
static const uint32_t golden[6] =
{
   0x8de63dc9u, 0xd6c18fceu, 0x2380ab14u,
   0x65fa7da3u, 0x78b4f379u, 0xc4fa4ab4u
};

int main(void)
{
   uint32_t got[6];
   int i;

   got[0] = run_open();
   got[1] = run_screen();
   got[2] = run_names();
   got[3] = run_rom_options();
   got[4] = run_header();
   got[5] = run_tv();

#ifdef RICE_CFG_GOLDEN
   for (i = 0; i < 6; i++)
      printf("   0x%08xu,\n", (unsigned)got[i]);
#else
   for (i = 0; i < 6; i++)
      if (got[i] != golden[i])
      {
         printf("FAILED part %d: hash 0x%08x, the C++ gave 0x%08x\n", i, (unsigned)got[i], (unsigned)golden[i]);
         failures++;
      }
#endif
   if (failures)
   {
      printf("test_rice_config: %d failed\n", failures);
      return 1;
   }
   printf("test_rice_config: ok\n");
   return 0;
}
