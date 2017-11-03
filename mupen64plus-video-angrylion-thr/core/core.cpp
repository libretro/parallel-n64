#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include <boolean.h>

#include "core.h"
#include "rdp.h"
#include "vi.h"
#include "parallel_c.hpp"
#include "rdram.h"
#include "m64p_plugin.h"

#ifdef __cplusplus
extern "C" {
extern void DebugMessage(int level, const char *message, ...);
#endif

#include "../Gfx #1.3.h"

#include "m64p_types.h"
#include "m64p_config.h"
#include "msg.h"

int retro_return(int just_flipping);

#ifdef __cplusplus
}
#endif

#define DP_INTERRUPT    0x20

extern "C" unsigned int screen_width, screen_height;
extern "C" uint32_t screen_pitch;

static uint32_t num_workers;
static bool parallel;
static bool parallel_tmp;

static struct core_config* config_new;
static struct core_config config;

static unsigned angrylion_filtering = 0;
static unsigned angrylion_dithering = 1;

static uint32_t rdram_size;
uint8_t rdram_hidden_bits[0x400000];

int ProcessDListShown = 0;

extern GFX_INFO gfx_info;

/* pointer indexing limits for aliasing RDRAM reads and writes */
uint32_t idxlim8;
uint32_t idxlim16;
uint32_t idxlim32;

uint32_t* rdram32;
uint16_t* rdram16;

#ifdef __cplusplus
extern "C" {
#endif

void angrylion_set_filtering(unsigned filter_type)
{
   angrylion_filtering = filter_type;
}

void angrylion_set_dithering(unsigned dither_type)
{
   angrylion_dithering = dither_type;
}

unsigned angrylion_get_dithering(void)
{
   return angrylion_dithering;
}

void angrylionChangeWindow (void)
{
}

void angrylionReadScreen2(void *dest, int *width, int *height, int front)
{
}
 
void angrylionDrawScreen (void)
{
}

void angrylionGetDllInfo(PLUGIN_INFO* PluginInfo)
{
    PluginInfo -> Version = 0x0103;
    PluginInfo -> Type  = 2;
    strcpy(
    PluginInfo -> Name, "angrylion's RDP"
    );
    PluginInfo -> NormalMemory = true;
    PluginInfo -> MemoryBswaped = true;
}

void angrylionSetRenderingCallback(void (*callback)(int))
{
}

int angrylionInitiateGFX (GFX_INFO Gfx_Info)
{
   return 0;
}

 
void angrylionMoveScreen (int xpos, int ypos)
{
}

 
void angrylionProcessDList(void)
{
   if (!ProcessDListShown)
      ProcessDListShown = 1;
}

void angrylionProcessRDPList(void)
{
   rdp_update();
}

void angrylionRomClosed (void)
{
   core_close();
}

int angrylionRomOpen(void)
{
   /* TODO/FIXME: For now just force it to 640x480.
    *
    * Later on we might want a low-res mode (320x240)
    * for better performance as well in case screen_width
    * is 320 and height is 240.
    */
   if (screen_width < 640)
      screen_width = 640;
   if (screen_width > 640)
      screen_width = 640;

   if (screen_height < 480)
      screen_height = 480;
   if (screen_height > 480)
      screen_height = 480;

   screen_pitch  = 640 << 2;

	core_config_defaults(&config);
	config.parallel = true;
	config.num_workers = 0;
	config.vi.mode = (vi_mode)0;
	config.vi.widescreen = 0;
	config.vi.overscan = 1;

	core_init(&config);
   return 1;
}

void angrylionUpdateScreen(void)
{
#ifdef HAVE_FRAMESKIP
    static int counter;
    if (counter++ < skip)
        return;
    counter = 0;
#endif
    vi_update();
    retro_return(true);
}

void screen_swap(void)
{
}

void angrylionShowCFB (void)
{
    angrylionUpdateScreen();
}


void angrylionViStatusChanged (void)
{
}

void angrylionViWidthChanged (void)
{
}

void angrylionFBWrite(unsigned int addr, unsigned int size)
{
}

void angrylionFBRead(unsigned int addr)
{
}

void angrylionFBGetFrameBufferInfo(void *pinfo)
{
}

m64p_error angrylionPluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
   /* set version info */
   if (PluginType != NULL)
      *PluginType = M64PLUGIN_GFX;

   if (PluginVersion != NULL)
      *PluginVersion = 0x016304;

   if (APIVersion != NULL)
      *APIVersion = 0x020100;

   if (PluginNamePtr != NULL)
      *PluginNamePtr = "MAME/Angrylion/HatCat/ata4 video Plugin";

   if (Capabilities != NULL)
      *Capabilities = 0;

   return M64ERR_SUCCESS;
}

#define MSG_BUFFER_LEN 256
void msg_error(const char * err, ...)
{
   va_list ap;
   char buffer[2049];
   va_start(ap, err);
   vsnprintf(buffer, 2047, err, ap);
   buffer[2048] = '\0';
   va_end(ap);

   DebugMessage(M64MSG_ERROR, "%s", buffer);
}

void msg_warning(const char* err, ...)
{
   va_list ap;
   char buffer[2049];
   va_start(ap, err);
   vsnprintf(buffer, 2047, err, ap);
   buffer[2048] = '\0';
   va_end(ap);

   DebugMessage(M64MSG_WARNING, "%s", buffer);
}

void msg_debug(const char* err, ...)
{
   va_list ap;
   char buffer[2049];
   va_start(ap, err);
   vsnprintf(buffer, 2047, err, ap);
   buffer[2048] = '\0';
   va_end(ap);

   DebugMessage(M64MSG_INFO, "%s", buffer);
}

static char filter_char(char c)
{
    if (isalnum(c) || c == '_' || c == '-' || c == '.')
        return c;
    return ' ';
}

static uint32_t get_rom_name(char* name, uint32_t name_size)
{
   int i = 0;
   uint8_t *rom_header = NULL;

   // buffer too small
   if (name_size < 21)
      return 0;

   rom_header = gfx_info.HEADER;

   // not available
   if (!rom_header)
      return 0;

   // copy game name from ROM header, which is encoded in Shift_JIS.
   // most games just use the ASCII subset, so filter out the rest.
   for (; i < 20; i++)
      name[i] = filter_char(rom_header[(32 + i) ^ BYTE_ADDR_XOR]);

   // make sure there's at least one 
   // whitespace that will terminate the string
   // below
   name[i] = ' ';

   // trim trailing whitespaces
   for (; i > 0; i--)
   {
      if (name[i] != ' ')
         break;
      name[i] = 0;
   }

   // game title is empty or invalid, use safe 
   // fallback using the four-character
   // game ID
   if (i == 0)
   {
      for (; i < 4; i++)
         name[i] = filter_char(rom_header[(59 + i) ^ BYTE_ADDR_XOR]);
      name[i] = 0;
   }

   return i;
}

static void rdram_init(void)
{
    idxlim8      = rdram_size - 1;
    idxlim16     = (idxlim8 >> 1) & 0xffffffu;
    idxlim32     = (idxlim8 >> 2) & 0xffffffu;

    rdram32      = (uint32_t*)gfx_info.RDRAM;
    rdram16      = (uint16_t*)gfx_info.RDRAM;
}

void core_init(struct core_config* _config)
{
   unsigned i;
   config = *_config;

   rdram_size = 0x800000;

   // mupen64plus plugins can't access the hidden bits, so allocate it on our own
   for (i = 0; i < sizeof(rdram_hidden_bits); i++)
      rdram_hidden_bits[i] = 0x03;

   num_workers = config.num_workers;
   parallel    = config.parallel;

   if (config.parallel)
      parallel_alinit(num_workers);

   rdram_init();
   rdp_init(&config);
   vi_init(&config);
}

void core_dp_sync(void)
{
   // update config if set
   if (config_new)
   {
      config = *config_new;
      config_new = NULL;

      // enable/disable multithreading or update number of workers
      if (config.parallel != parallel || config.num_workers != num_workers)
      {
         // destroy old threads
         parallel_close();

         // create new threads if parallel option is still enabled
         if (config.parallel)
            parallel_alinit(num_workers);

         num_workers = config.num_workers;
         parallel = config.parallel;
      }
   }

   // signal plugin to handle interrupts
   *gfx_info.MI_INTR_REG |= DP_INTERRUPT;
   gfx_info.CheckInterrupts();
}

void core_config_update(struct core_config* _config)
{
    config_new = _config;
}

void core_config_defaults(struct core_config* config)
{
    memset(config, 0, sizeof(*config));
    config->parallel = true;
}

void core_close(void)
{
    parallel_close();
    vi_close();
}

#ifdef __cplusplus
}
#endif
