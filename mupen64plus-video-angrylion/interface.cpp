#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <boolean.h>
#include "common.h"
#include "screen.h"
#include "n64video.h"
#include "m64p_plugin.h"

#ifdef __cplusplus
extern "C" {
extern void DebugMessage(int level, const char *message, ...);
#endif

#include "Gfx #1.3.h"

#include "m64p_types.h"
#include "m64p_config.h"

int retro_return(bool just_flipping);

#ifdef __cplusplus
}
#endif

#define DP_INTERRUPT    0x20

static unsigned angrylion_filtering = 0;
static unsigned angrylion_dithering = 1;
static unsigned angrylion_vi = 0;
static unsigned angrylion_threads = 0;

int ProcessDListShown = 0;

extern GFX_INFO gfx_info;

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t *blitter_buf_lock;
extern unsigned int screen_width, screen_height;
extern uint32_t screen_pitch;

static struct n64video_config config;

#include <ctype.h>



void plugin_init(void)
{
}

void plugin_sync_dp(void)
{
    *gfx_info.MI_INTR_REG |= DP_INTERRUPT;
    gfx_info.CheckInterrupts();
}

uint32_t** plugin_get_dp_registers(void)
{
    // HACK: this only works because the ordering of registers in GFX_INFO is
    // the same as in dp_register
    return (uint32_t**)&gfx_info.DPC_START_REG;
}

uint32_t** plugin_get_vi_registers(void)
{
    // HACK: this only works because the ordering of registers in GFX_INFO is
    // the same as in vi_register
    return (uint32_t**)&gfx_info.VI_STATUS_REG;
}

uint8_t* plugin_get_rdram(void)
{
    return gfx_info.RDRAM;
}

uint32_t plugin_get_rdram_size(void)
{
    return 0x800000;
}

uint8_t* plugin_get_dmem(void)
{
    return gfx_info.DMEM;
}

uint8_t* plugin_get_rom_header(void)
{
    return gfx_info.HEADER;
}

void plugin_close(void)
{
}

static char filter_char(char c)
{
    if (isalnum(c) || c == '_' || c == '-' || c == '.') {
        return c;
    } else {
        return ' ';
    }
}

uint32_t plugin_get_rom_name(char* name, uint32_t name_size)
{
    if (name_size < 21) {
        // buffer too small
        return 0;
    }

    uint8_t* rom_header = plugin_get_rom_header();
    if (!rom_header) {
        // not available
        return 0;
    }

    // copy game name from ROM header, which is encoded in Shift_JIS.
    // most games just use the ASCII subset, so filter out the rest.
    int i = 0;
    for (; i < 20; i++) {
        name[i] = filter_char(rom_header[(32 + i) ^ BYTE_ADDR_XOR]);
    }

    // make sure there's at least one whitespace that will terminate the string
    // below
    name[i] = ' ';

    // trim trailing whitespaces
    for (; i > 0; i--) {
        if (name[i] != ' ') {
            break;
        }
        name[i] = 0;
    }

    // game title is empty or invalid, use safe fallback using the four-character
    // game ID
    if (i == 0) {
        for (; i < 4; i++) {
            name[i] = filter_char(rom_header[(59 + i) ^ BYTE_ADDR_XOR]);
        }
        name[i] = 0;
    }

    return i;
}

void screen_swap(bool)
{
}

void screen_init(struct n64video_config* config)
{
}

void screen_read(struct frame_buffer* buffer, bool alpha)
{}

void screen_set_fullscreen(bool _fullscreen)
{}

bool screen_get_fullscreen(void)
{
   return false;
}

void screen_close(void)
{}


void screen_write(struct frame_buffer* buffer, int32_t output_height)
{
   int i, cur_line;
	uint32_t * buf = (uint32_t*)buffer->pixels;
    
    for (i = 0; i < buffer->height; i++)
          memcpy(&blitter_buf_lock[i * buffer->width],&buf[i * buffer->width], buffer->pitch * 4);


  screen_width=buffer->width;
  screen_height=buffer->height;
  screen_pitch= buffer->pitch*4;
}


unsigned angrylion_get_vi(void)
{
   return angrylion_vi;
}

void angrylion_set_vi(unsigned value)
{

   angrylion_vi = value;

}

void angrylion_set_threads(unsigned value)
{
    angrylion_threads = value;
}

unsigned angrylion_get_threads()
{
    return angrylion_threads;
}

void angrylion_set_filtering(unsigned filter_type)
{
   angrylion_filtering = filter_type;
}

unsigned angrylion_get_filtering()
{
    return angrylion_filtering;
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
  n64video_process_list();
}

void angrylionRomClosed (void)
{
  n64video_close();
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
  
	n64video_config_defaults(&config);
	config.parallel = true;
	config.num_workers = angrylion_get_threads();
	config.vi.mode = (vi_mode)angrylion_get_vi();
    config.vi.interp = (vi_interp)angrylion_get_filtering();
	config.vi.widescreen = 0;
	config.vi.hide_overscan = 0;
   n64video_init(&config);
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
    n64video_update_screen();
    retro_return(true);
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

#ifdef __cplusplus
}
#endif
