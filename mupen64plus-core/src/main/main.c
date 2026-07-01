/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - main.c                                                  *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2012 CasualJames                                        *
 *   Copyright (C) 2008-2009 Richard Goedeken                              *
 *   Copyright (C) 2008 Ebenblues Nmn Okaygo Tillin9                       *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* This is MUPEN64's main entry point. It contains code that is common
 * to both the gui and non-gui versions of mupen64. See
 * gui subdirectories for the gui-specific code.
 * if you want to implement an interface, you should look here
 */

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/callbacks.h"
#include "api/config.h"
#include "api/debugger.h"
#include "api/m64p_config.h"
#include "api/m64p_types.h"
#include "api/m64p_vidext.h"
#include "api/vidext.h"
#include "backends/api/audio_out_backend.h"
#include "backends/api/clock_backend.h"
#include "backends/api/controller_input_backend.h"
#include "backends/api/joybus.h"
#include "backends/api/rumble_backend.h"
#include "backends/api/storage_backend.h"
#include "backends/api/video_capture_backend.h"
#include "backends/plugins_compat/plugins_compat.h"
#include "backends/clock_ctime_plus_delta.h"
#include "backends/file_storage.h"
#include "cheat.h"
#include "device/device.h"
#include "device/dd/disk.h"
#include "device/controllers/vru_controller.h"
#include "device/controllers/paks/biopak.h"
#include "device/controllers/paks/mempak.h"
#include "device/controllers/paks/rumblepak.h"
#include "device/controllers/paks/transferpak.h"
#include "device/gb/gb_cart.h"
#include "device/pif/bootrom_hle.h"
#include "eventloop.h"
#include "main.h"
#include "callbacks.h"
#include "plugin/plugin.h"
#if defined(PROFILE)
#include "profile.h"
#endif
#include "rom.h"
#include "savestates.h"
#include "util.h"

#include <libretro_private.h>

#ifdef HAVE_LIBNX
#include <switch.h>
#include <sys/stat.h>
#endif

#include <file/file_path.h>
#ifndef PATH_DEFAULT_SLASH
#if defined(_WIN32)
#define PATH_DEFAULT_SLASH() "\\"
#else
#define PATH_DEFAULT_SLASH() "/"
#endif
#endif
#include <libretro_memory.h>
#include <mupen64plus-next_common.h>

#ifdef DBG
#include "debugger/dbg_debugger.h"
#endif

/* version number for Core config section */
#define CONFIG_PARAM_VERSION 1.01

/** globals **/
m64p_handle g_CoreConfig = NULL;

int         g_RomWordsLittleEndian = 0; // after loading, ROM words are in native N64 byte order (big endian). We will swap them on x86
int         g_EmulatorRunning = 0;      // need separate boolean to tell if emulator is running, since --nogui doesn't use a thread

extern int frame_break; /* r4300: per-frame yield flag set by retro_return at VI */

struct cheat_ctx g_cheat_ctx;

/* g_mem_base is global to allow plugins early access (before device is initialized).
 * Do not use this variable directly in emulation code.
 * Initialization and DeInitialization of this variable is done at CoreStartup and CoreShutdown.
 */
void* g_mem_base = NULL;

uint32_t g_start_address = UINT32_C(0xa4000040);

struct device g_dev;

m64p_media_loader g_media_loader;

int g_gs_vi_counter = 0;

/** static (local) variables **/

/* compatible paks */
enum { PAK_MAX_SIZE = 5 };
static size_t l_paks_idx[GAME_CONTROLLERS_COUNT];
static void* l_paks[GAME_CONTROLLERS_COUNT][PAK_MAX_SIZE];
static const struct pak_interface* l_ipaks[PAK_MAX_SIZE];
static size_t l_pak_type_idx[6];

/* PRNG state - used for Mempaks ID generation */
struct xoshiro256pp_state l_mpk_idgen;

/*********************************************************************************************************
* static functions
*/

static char *get_gb_ram_path(const char* gbrom, unsigned int control_id)
{
    return "";
}

const char *get_dd_disk_save_path(const char* diskname, int save_format)
{
    char* newPath = (char*)malloc(PATH_MAX);
    strcpy(newPath, diskname);
    
    if(save_format == 0)
        strcat(newPath, ".disk_save");
    else
        strcat(newPath, ".ram");

    return newPath;
}

void main_message(m64p_msg_level level, unsigned int corner, const char *format, ...)
{
    va_list ap;
    char buffer[2049];
    va_start(ap, format);
    vsnprintf(buffer, 2047, format, ap);
    buffer[2048]='\0';
    va_end(ap);

    /* send message to front-end */
    DebugMessage(level, "%s", buffer);
}

extern retro_input_poll_t poll_cb;
static void main_check_inputs(void)
{
    if(!(current_rdp_type == RDP_PLUGIN_GLIDEN64 && EnableThreadedRenderer))
    {
        // Input Polling will be forced to early if Threaded GLideN64
        poll_cb();
    }
}

/*********************************************************************************************************
* global functions, for adjusting the core emulator behavior
*/
m64p_error main_reset(int do_hard_reset)
{
    if (do_hard_reset) {
        hard_reset_device(&g_dev);
    }
    else {
        soft_reset_device(&g_dev);
    }

    return M64ERR_SUCCESS;
}

/*********************************************************************************************************
* global functions, callbacks from the r4300 core or from other plugins
*/

static void video_plugin_render_callback(int bScreenRedrawn)
{
    // if the input plugin specified a render callback, call it now
    if(input.renderCallback)
    {
        input.renderCallback();
    }
}

void new_frame(void)
{
    /* advance the current frame */
}

/* TODO: make a GameShark module and move that there */
static void gs_apply_cheats(struct cheat_ctx* ctx)
{
    struct r4300_core* r4300 = &g_dev.r4300;

    if (g_gs_vi_counter < 60)
    {
        if (g_gs_vi_counter == 0)
            cheat_apply_cheats(ctx, r4300, ENTRY_BOOT);
        g_gs_vi_counter++;
    }
    else
    {
        cheat_apply_cheats(ctx, r4300, ENTRY_VI);
    }
}

/* called on vertical interrupt.
 * Allow the core to perform various things */
void new_vi(void)
{
#if defined(PROFILE)
    timed_sections_refresh();
#endif

    gs_apply_cheats(&g_cheat_ctx);

    main_check_inputs();
    retro_return(false);
}

static void main_switch_pak(int control_id)
{
    struct game_controller* cont = &g_dev.controllers[control_id];

    change_pak(cont, l_paks[control_id][l_paks_idx[control_id]], l_ipaks[l_paks_idx[control_id]]);

    if (cont->ipak != NULL) {
        DebugMessage(M64MSG_INFO, "Controller %u pak changed to %s", control_id, cont->ipak->name);
    }
    else {
        DebugMessage(M64MSG_INFO, "Removing pak from controller %u", control_id);
    }
}

void main_switch_next_pak(int control_id)
{
    if (l_ipaks[l_paks_idx[control_id]] == NULL ||
        ++l_paks_idx[control_id] >= PAK_MAX_SIZE) {
        l_paks_idx[control_id] = 0;
    }

    main_switch_pak(control_id);
}

void main_switch_plugin_pak(int control_id)
{
    //Don't switch to the selected pak if it's not available for the game
    if (l_ipaks[l_pak_type_idx[Controls[control_id].Plugin]] == NULL) {
        Controls[control_id].Plugin = PLUGIN_NONE;
    }

    l_paks_idx[control_id] = l_pak_type_idx[Controls[control_id].Plugin];

    main_switch_pak(control_id);
}

void save_storage_file_libretro(void* storage)
{
}

static void open_mpk_file(struct file_storage* storage)
{
	storage->data = saved_memory.mempack[0];
	storage->size = MEMPAK_SIZE * 4;
}

static void open_fla_file(struct file_storage* storage)
{
        storage->data = saved_memory.flashram;
        storage->size = FLASHRAM_SIZE;
}

static void open_sra_file(struct file_storage* storage)
{
        storage->data = saved_memory.sram;
        storage->size = SRAM_SIZE;
}

static void open_eep_file(struct file_storage* storage)
{
        storage->data = saved_memory.eeprom;
        storage->size = EEPROM_MAX_SIZE;
}

extern char* retro_dd_path_rom;
static void load_dd_rom(uint8_t* rom, size_t* rom_size, uint8_t* disk_region)
{
    /* set the DD rom region */
    if (g_media_loader.set_dd_rom_region != NULL)
    {
        g_media_loader.set_dd_rom_region(g_media_loader.cb_data, *disk_region);
    }

    /* ask the core loader for DD disk filename */
    char* dd_ipl_rom_filename = (g_media_loader.get_dd_rom == NULL)
        ? NULL
        : g_media_loader.get_dd_rom(g_media_loader.cb_data);

    char* sys_pathname;
    environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_pathname);
    char* pathname = (char*)malloc(2048);
    strncpy(pathname, sys_pathname, 2048 - 1);
    if (pathname[(strlen(pathname)-1)] != '/' && pathname[(strlen(pathname)-1)] != '\\')
        strcat(pathname, PATH_DEFAULT_SLASH());
    strcat(pathname, "Mupen64plus");
    strcat(pathname, PATH_DEFAULT_SLASH());
    strcat(pathname, "IPL.n64");

    if(retro_dd_path_rom && strlen(retro_dd_path_rom) > 0)
    {
        /* explicit IPL path staged by the libretro frontend */
        dd_ipl_rom_filename = retro_dd_path_rom;
    }
    else if(retro_dd_path_img)
    {
        dd_ipl_rom_filename = pathname;
    }
    else
    {
        dd_ipl_rom_filename = NULL;
    }

    if ((dd_ipl_rom_filename == NULL) || (strlen(dd_ipl_rom_filename) == 0)) {
        goto no_dd;
    }

    struct file_storage dd_rom;
    memset(&dd_rom, 0, sizeof(dd_rom));

    if (open_rom_file_storage(&dd_rom, dd_ipl_rom_filename) != file_ok) {
        DebugMessage(M64MSG_ERROR, "Failed to load DD IPL ROM: %s. Disabling 64DD", dd_ipl_rom_filename);
        goto no_dd;
    }

    DebugMessage(M64MSG_INFO, "DD IPL ROM: %s", dd_ipl_rom_filename);

    /* load and swap DD IPL ROM */
    *rom_size = g_ifile_storage_ro.size(&dd_rom);
    memcpy(rom, g_ifile_storage_ro.data(&dd_rom), *rom_size);
    close_file_storage(&dd_rom);

    /* fetch 1st word to identify IPL ROM format */
    /* FIXME: use more robust ROM detection heuristic - do the same for regular ROMs */
    uint32_t pi_bsd_dom1_config = 0
        | ((uint32_t)rom[0] << 24)
        | ((uint32_t)rom[1] << 16)
        | ((uint32_t)rom[2] <<  8)
        | ((uint32_t)rom[3] <<  0);

    switch (pi_bsd_dom1_config)
    {
    case 0x80270740: /* Z64 - big endian */
        to_big_endian_buffer(rom, 4, *rom_size/4);
        break;

    case 0x40072780: /* N64 - little endian */
        to_little_endian_buffer(rom, 4, *rom_size/4);
        break;

    case 0x27804007: /* V64 - bi-endian */
        swap_buffer(rom, 2, *rom_size/2);
        break;

    default: /* unknown */
        DebugMessage(M64MSG_ERROR, "Invalid DD IPL ROM: Disabling 64DD.");
        *rom_size = 0;
        return;
    }

    return;

no_dd:
    free(dd_ipl_rom_filename);
    *rom_size = 0;
}

extern char* retro_dd_path_img;
extern char* retro_dd_path_rom;
static int load_dd_disk(struct dd_disk* dd_disk, const struct storage_backend_interface** dd_idisk)
{
    /* ask the core loader for DD disk filename */
    char* dd_disk_filename = (g_media_loader.get_dd_disk == NULL)
        ? (retro_dd_path_img ? strdup(retro_dd_path_img) : NULL)
        : g_media_loader.get_dd_disk(g_media_loader.cb_data);

    /* handle the no disk case */
    if (dd_disk_filename == NULL || strlen(dd_disk_filename) == 0)
        goto no_disk;

    /* Get DD Disk size */
    size_t dd_size = 0;
    if (get_file_size(dd_disk_filename, &dd_size) != file_ok) {
        DebugMessage(M64MSG_ERROR, "Can't get DD disk file size");
        goto no_disk;
    }

    struct file_storage* fstorage = malloc(sizeof(struct file_storage));
    struct file_storage* fstorage_save = malloc(sizeof(struct file_storage));
    if (fstorage == NULL || fstorage_save == NULL) {
        DebugMessage(M64MSG_ERROR, "Failed to allocate DD file_storage");
        if (fstorage != NULL)      { free(fstorage);      fstorage = NULL; }
        if (fstorage_save != NULL) { free(fstorage_save); fstorage_save = NULL; }
        goto no_disk;
    }

    /* Determine disk save format */
    int save_format = 0; //ConfigGetParamInt(g_CoreConfig, "SaveDiskFormat");
    /* MAME disks only support full disk save */
    if (dd_size == MAME_FORMAT_DUMP_SIZE && save_format != 0) {
        DebugMessage(M64MSG_WARNING, "MAME disks only support full disk save format, switching to full disk format !");
        save_format = 0;
    }

    /* Determine save file name */
    const char* save_filename = get_dd_disk_save_path(dd_disk_filename, save_format);
    if (save_filename == NULL) {
        DebugMessage(M64MSG_ERROR, "Failed to get DD save path, DD will be read-only.");
        save_format = -1;
    }

    /* Try loading *.{nd,d6}r file first (if SaveDiskFormat == 0) */
    if (save_format == 0)
    {
        if (open_rom_file_storage(fstorage, save_filename) != file_ok) {
            DebugMessage(M64MSG_WARNING, "Failed to load DD Disk save: %s.", save_filename);

            /* Try loading regular disk file */
            if (open_rom_file_storage(fstorage, dd_disk_filename) != file_ok) {
                DebugMessage(M64MSG_ERROR, "Failed to load DD Disk: %s.", dd_disk_filename);
                goto free_fstorage;
            }
        }
    }
    else
    {
        /* Try loading regular disk file */
        if (open_rom_file_storage(fstorage, dd_disk_filename) != file_ok) {
            DebugMessage(M64MSG_ERROR, "Failed to load DD Disk: %s.", dd_disk_filename);
            goto free_fstorage;
        }
    }

    /* Force fstorage to point to save_filename, to redirect all writes to save file,
     * (and to avoid corrupting 64DD dump)
     * save_filename is now owned by fstorage.
     * dd_disk_filename is not owned anymore and must be freed individually.
     */
    fstorage->filename = save_filename;

    /* Scan disk to deduce disk format and other parameters and expand its size for D64 */
    unsigned int format = 0;
    unsigned int development = 0;
    size_t offset_sys = 0;
    size_t offset_id = 0;
    size_t offset_ram = 0;
    size_t size_ram = 0;
    uint8_t* new_data = scan_and_expand_disk_format(fstorage->data, fstorage->size, &format, &development, &offset_sys, &offset_id, &offset_ram, &size_ram);
    if (new_data == NULL) {
        DebugMessage(M64MSG_ERROR, "Wrong disk format");
        goto wrong_disk_format;
    }
    else {
        fstorage->data = new_data;
    }

    /* Load RAM save data (if SaveDiskFormat == 1) */
    if (save_format == 1)
    {
        if (read_from_file(save_filename, &fstorage->data[offset_ram], size_ram) != file_ok)
        {
            DebugMessage(M64MSG_WARNING, "Failed to load DD Disk RAM area (*.ram): %s.", save_filename);
        }
    }

    switch(save_format)
    {
    case 0: /* Full disk */
        *dd_idisk = &g_istorage_disk_full;
        fstorage_save->filename = save_filename;
        fstorage_save->data = fstorage->data;
        fstorage_save->size = fstorage->size;
        fstorage_save->first_access = 1;
        break;
    case 1: /* RAM only */
        *dd_idisk = &g_istorage_disk_ram_only;
        fstorage_save->filename = save_filename;
        fstorage_save->data = &fstorage->data[offset_ram];
        fstorage_save->size = size_ram;
        fstorage_save->first_access = 1;
        break;
    default: /* read only */
        *dd_idisk = &g_istorage_disk_read_only;
        free(fstorage_save);
        fstorage_save = NULL;
    }

    /* Setup dd_disk */
    dd_disk->storage = fstorage;
    dd_disk->istorage = &g_ifile_storage_ro;
    dd_disk->save_storage = fstorage_save;
    dd_disk->isave_storage = (save_format >= 0) ? &g_ifile_storage : NULL;
    dd_disk->format = format;
    dd_disk->development = development;
    dd_disk->region = DDREGION_UNKNOWN;
    dd_disk->offset_sys = offset_sys;
    dd_disk->offset_id = offset_id;
    dd_disk->offset_ram = offset_ram;

    /* Generate LBA conversion table */
    GenerateLBAToPhysTable(dd_disk);

    DebugMessage(M64MSG_INFO, "DD Disk: %s - %zu - %s",
            dd_disk_filename,
            (*dd_idisk)->size(dd_disk),
            get_disk_format_name(format));

    /* Get region from disk and byteswap it as needed */
    uint32_t w = *(uint32_t*)(*dd_idisk)->data(dd_disk);
    if (dd_disk->format == DISK_FORMAT_SDK) {
        swap_buffer(&w, sizeof(w), 1);
    }
    
    /* Set region in dd_disk */
    if (w == DD_REGION_DV || development) {
        dd_disk->region = DDREGION_DEV;
    } else if (w == DD_REGION_JP) {
        dd_disk->region = DDREGION_JAPAN;
    } else if (w == DD_REGION_US) {
        dd_disk->region = DDREGION_US;
    }

    if (w == DD_REGION_JP || w == DD_REGION_US || w == DD_REGION_DV) {
        DebugMessage(M64MSG_WARNING, "Loading a saved disk");
    }

    free(dd_disk_filename);
    return 1;

wrong_disk_format:
    /* no need to close save_storage as it is a child of disk->storage */
    close_file_storage(fstorage);
free_fstorage:
    free(fstorage);
    free(fstorage_save);
no_disk:
    free(dd_disk_filename);
    *dd_idisk = NULL;

    return 0;
}

static void close_dd_disk(struct dd_disk* disk)
{
    if (disk->save_storage != NULL) {
        /* no need to close save_storage as it is a child of disk->storage */
        free(disk->save_storage);
        disk->save_storage = NULL;
    }

    if (disk->storage != NULL) {
        close_file_storage(disk->storage);
        free(disk->storage);
        disk->storage = NULL;
    }
}


struct gb_cart_data
{
    int control_id;
    struct file_storage rom_fstorage;
    struct file_storage ram_fstorage;
    void* gbcam_backend;
    const struct video_capture_backend_interface* igbcam_backend;
};

static struct gb_cart_data l_gb_carts_data[GAME_CONTROLLERS_COUNT];

static void init_gb_rom(void* opaque, void** storage, const struct storage_backend_interface** istorage)
{
    struct gb_cart_data* data = (struct gb_cart_data*)opaque;

    /* Ask the core loader for rom filename */
    char* rom_filename = (g_media_loader.get_gb_cart_rom == NULL)
        ? (retro_transferpak_rom_path ? strdup(retro_transferpak_rom_path) : NULL)
        : g_media_loader.get_gb_cart_rom(g_media_loader.cb_data, data->control_id);

    /* Handle the no cart case */
    if (rom_filename == NULL || strlen(rom_filename) == 0) {
        goto no_cart;
    }

    /* Open ROM file */
    if (open_rom_file_storage(&data->rom_fstorage, rom_filename) != file_ok) {
        log_cb(RETRO_LOG_ERROR, "Failed to load ROM file: %s\n", rom_filename);
        goto no_cart;
    }

    log_cb(RETRO_LOG_INFO, "GB Loader ROM: %s - %zu\n",
            data->rom_fstorage.filename,
            data->rom_fstorage.size);

    /* init GB ROM storage */
    *storage = &data->rom_fstorage;
    *istorage = &g_ifile_storage_ro;
    return;

no_cart:
    free(rom_filename);
    *storage = NULL;
    *istorage = NULL;
}

static void release_gb_rom(void* opaque)
{
    struct gb_cart_data* data = (struct gb_cart_data*)opaque;

    close_file_storage(&data->rom_fstorage);

    memset(&data->rom_fstorage, 0, sizeof(data->rom_fstorage));
}

static void init_gb_ram(void* opaque, size_t ram_size, void** storage, const struct storage_backend_interface** istorage)
{
    struct gb_cart_data* data = (struct gb_cart_data*)opaque;

    /* Ask the core loader for ram filename */
    char* ram_filename = (g_media_loader.get_gb_cart_ram == NULL)
        ? (retro_transferpak_ram_path ? strdup(retro_transferpak_ram_path) : NULL)
        : g_media_loader.get_gb_cart_ram(g_media_loader.cb_data, data->control_id);

    /* Handle the no RAM case
     * if NULL or empty string generate a filename
     */
    if (ram_filename == NULL || strlen(ram_filename) == 0) {
        free(ram_filename);
        ram_filename = get_gb_ram_path(namefrompath(data->rom_fstorage.filename), data->control_id+1);
    }

    /* Open RAM file
     * if file doesn't exists provide default content */
    int err = open_file_storage(&data->ram_fstorage, ram_size, ram_filename);
    if (err == file_open_error) {
        memset(data->ram_fstorage.data, 0, data->ram_fstorage.size);
        log_cb(RETRO_LOG_INFO, "Providing default RAM content\n");
    }
    else if (err == file_read_error) {
        log_cb(RETRO_LOG_WARN, "Size mismatch between expected RAM size and effective file size\n");
    }

    log_cb(RETRO_LOG_INFO, "GB Loader RAM: %s - %zu\n",
            data->ram_fstorage.filename,
            data->ram_fstorage.size);

    /* init GB RAM storage */
    *storage = &data->ram_fstorage;
    *istorage = &g_ifile_storage;
}

static void release_gb_ram(void* opaque)
{
    struct gb_cart_data* data = (struct gb_cart_data*)opaque;

    close_file_storage(&data->ram_fstorage);

    memset(&data->ram_fstorage, 0, sizeof(data->ram_fstorage));
}

void main_change_gb_cart(int control_id)
{
    struct transferpak* tpk = &g_dev.transferpaks[control_id];
    struct gb_cart* gb_cart = &g_dev.gb_carts[control_id];
    struct gb_cart_data* data = &l_gb_carts_data[control_id];

    /* reset gb_cart_data */
    memset(data, 0, sizeof(*data));
    data->control_id = control_id;

    init_gb_cart(gb_cart,
            data, init_gb_rom, release_gb_rom,
            data, init_gb_ram, release_gb_ram,
            NULL, &g_iclock_ctime_plus_delta,
            &data->control_id, &g_irumble_backend_plugin_compat,
            data->gbcam_backend, data->igbcam_backend);

    if (gb_cart->read_gb_cart == NULL) {
        gb_cart = NULL;
    }

    change_gb_cart(tpk, gb_cart);

    if (tpk->gb_cart != NULL) {
        const uint8_t* rom_data = gb_cart->irom_storage->data(gb_cart->rom_storage);
        log_cb(RETRO_LOG_INFO, "Inserting GB cart %s into transferpak %u\n", rom_data + 0x134, control_id);
    }
    else {
        log_cb(RETRO_LOG_WARN, "Removing GB cart from transferpak %u\n", control_id);
    }
}


/*********************************************************************************************************
* emulation thread - runs the core
*/
extern gfx_plugin_functions gfx_gln64;
extern rsp_plugin_functions rsp_hle;
extern input_plugin_functions dummy_input;
extern audio_plugin_functions dummy_audio;

unsigned int r4300_emumode;
/* Which JIT backend EMUMODE_DYNAREC uses: 0 = ari64 new dynarec, 1 = Hacktarux.
 * Set from the parallel-n64-dynarec core option; only consulted when emumode is
 * EMUMODE_DYNAREC. */
unsigned int r4300_jit_backend;
size_t rdram_size;

m64p_error main_run(void)
{
    static int l_setup_done = 0;
    size_t i, k;
    uint32_t count_per_op;
    uint32_t count_per_op_denom_pot;
    uint32_t emumode;
    uint32_t disable_extra_mem;
    int32_t si_dma_duration;
    int32_t no_compiled_jump;
    int32_t randomize_interrupt;
    /* These backends/storages are stored by address into g_dev by init_device
     * and dereferenced throughout emulation. With the libco-free per-frame model
     * main_run() returns after every frame, so they must NOT live on the stack
     * (that memory is reused once main_run returns, leaving g_dev pointing at
     * garbage -- e.g. ai->iaout->set_frequency jumping to a stale address). They
     * are set up once (l_setup_done) and torn down once on a real stop, so file
     * scope is correct and does not leak. */
    static struct file_storage eep;
    static struct file_storage fla;
    static struct file_storage sra;
    size_t dd_rom_size;
    static struct dd_disk dd_disk;
    m64p_error failure_rval;
    static struct audio_out_backend_interface audio_out_backend_libretro;

    static int control_ids[GAME_CONTROLLERS_COUNT];
    static struct controller_input_compat cin_compats[GAME_CONTROLLERS_COUNT];

    static struct file_storage mpk_storages[GAME_CONTROLLERS_COUNT];
    static struct file_storage mpk;

    void* gbcam_backend;
    const struct video_capture_backend_interface* igbcam_backend;

    /* XXX: select type of flashram from db */
    uint32_t flashram_type = MX29L1100_ID;

    /* Per-frame re-entry (libco-free model): once the device has been set up,
     * every subsequent main_run() only advances one frame slice and returns.
     * This check must come first so the one-time initialization below -- config
     * reads, ROM byte-swap, storage-file opening, 64DD load, controller setup
     * and its logging -- does NOT re-run on every frame (it was spamming the log
     * with controller/pak messages once per frame and redundantly reinitializing
     * everything). The re-entry path only touches globals, so it is safe here. */
    if (l_setup_done) {
        /* Clear the per-frame yield flag and run a slice (run_device returns
         * when retro_return sets mupencorestop again). Clear the active
         * backend's stop via the accessor (ari64 keeps it in
         * new_dynarec_hot_state, Hacktarux/interpreters in the base struct). */
        *r4300_stop(&g_dev.r4300) = 0;
        /* mupencorestop (r4300.h) is hardcoded to ari64's hot-state stop and
         * gates retro_return's early-out; clear it directly so that in Hacktarux
         * mode -- where *r4300_stop() is the base-struct field, a different
         * location -- it does not stay latched from the first frame and block
         * retro_return from arming the next frame-break. The hot-state stop only
         * exists when the ari64 dynarec is compiled in (NEW_DYNAREC); without it
         * there is no separate ari64 stop and *r4300_stop() above is sufficient. */
#ifdef NEW_DYNAREC
        g_dev.r4300.new_dynarec_hot_state.stop = 0;
#endif
        run_device(&g_dev);
        return M64ERR_SUCCESS;
    }

    emumode = r4300_emumode;
    randomize_interrupt = 0; // We don't want this right now
    no_compiled_jump = 0;
    count_per_op = CountPerOp;
    count_per_op_denom_pot = CountPerOpDenomPot;
    disable_extra_mem = ROM_SETTINGS.disableextramem;

    uint16_t eeprom_type = JDT_NONE;
    switch (ROM_SETTINGS.savetype) {
        case SAVETYPE_EEPROM_4K:
            eeprom_type = JDT_EEPROM_4K;
            break;
        case SAVETYPE_EEPROM_16K:
            eeprom_type = JDT_EEPROM_16K;
            break;
    }

    /* Seed MPK ID gen using current time */
    uint64_t mpk_seed = (uint64_t)time(NULL);
    l_mpk_idgen = xoshiro256pp_seed(mpk_seed);

    no_compiled_jump = 0;

    if (ROM_SETTINGS.disableextramem)
        disable_extra_mem = ROM_SETTINGS.disableextramem;
    else
        disable_extra_mem = ForceDisableExtraMem;

    if (count_per_op <= 0)
        count_per_op = ROM_SETTINGS.countperop;

    if (count_per_op_denom_pot > 20)
        count_per_op_denom_pot = 20;

    si_dma_duration = ROM_SETTINGS.sidmaduration;

    rdram_size = (disable_extra_mem == 0) ? 0x800000 : 0x400000;

    cheat_add_hacks(&g_cheat_ctx, ROM_PARAMS.cheats);

    /* do byte-swapping if it hasn't been done yet */
#if !defined(MSB_FIRST)
    if (g_RomWordsLittleEndian == 0)
    {
        swap_buffer((uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM), 4, g_rom_size/4);
        g_RomWordsLittleEndian = 1;
    }
#endif

    /* setup backends */
    extern void set_audio_format_via_libretro(void* user_data, unsigned int frequency);
    extern void push_audio_samples_via_libretro(void* user_data, const void* buffer, size_t size);
    audio_out_backend_libretro = (struct audio_out_backend_interface){ set_audio_format_via_libretro, push_audio_samples_via_libretro };
    
    /* Fill-in l_pak_type_idx and l_ipaks according to game compatibility */
    k = 0;
    if (ROM_SETTINGS.biopak) {
        l_ipaks[k++] = &g_ibiopak;
    }
    if (ROM_SETTINGS.mempak) {
        l_pak_type_idx[PLUGIN_MEMPAK] = k;
        l_ipaks[k] = &g_imempak;
        ++k;
    }
    if (ROM_SETTINGS.rumble) {
        l_pak_type_idx[PLUGIN_RUMBLE_PAK] = k;
        l_pak_type_idx[PLUGIN_RAW] = k;
        l_ipaks[k] = &g_irumblepak;
        ++k;
    }
    if (ROM_SETTINGS.transferpak) {
        l_pak_type_idx[PLUGIN_TRANSFER_PAK] = k;
        l_ipaks[k] = &g_itransferpak;
        ++k;
    }
    l_pak_type_idx[PLUGIN_NONE] = k;
    l_ipaks[k] = NULL;

    if (!ROM_SETTINGS.mempak) {
        l_pak_type_idx[PLUGIN_MEMPAK] = k;
    }
    if (!ROM_SETTINGS.rumble) {
        l_pak_type_idx[PLUGIN_RUMBLE_PAK] = k;
        l_pak_type_idx[PLUGIN_RAW] = k;
    }
    if (!ROM_SETTINGS.transferpak) {
        l_pak_type_idx[PLUGIN_TRANSFER_PAK] = k;
    }

    /* open storage files, provide default content if not present */
    open_mpk_file(&mpk);
    open_eep_file(&eep);
    open_fla_file(&fla);
    open_sra_file(&sra);

    /* Load 64DD IPL ROM and Disk */
    const struct clock_backend_interface* dd_rtc_iclock = NULL;
    const struct storage_backend_interface* dd_idisk = NULL;
    memset(&dd_disk, 0, sizeof(dd_disk));

    /* try to load DD disk first, if that succeeds, pass the region to load_dd_rom */
    if (load_dd_disk(&dd_disk, &dd_idisk))
    {
        dd_rtc_iclock = &g_iclock_ctime_plus_delta;
        load_dd_rom((uint8_t*)mem_base_u32(g_mem_base, MM_DD_ROM), &dd_rom_size, &dd_disk.region);
    }
    else
    {
        dd_rom_size = 0;
    }

    /* ensure the 64DD rom & disk are loaded,
     * otherwise we have to bail right now */
    if (g_rom_size == 0 && dd_rom_size == 0)
    {
        goto on_disk_failure;
    }

    /* setup pif channel devices */
    static void* joybus_devices[PIF_CHANNELS_COUNT];
    static const struct joybus_device_interface* ijoybus_devices[PIF_CHANNELS_COUNT];

    memset(&g_dev.gb_carts, 0, GAME_CONTROLLERS_COUNT*sizeof(*g_dev.gb_carts));
    memset(&l_gb_carts_data, 0, GAME_CONTROLLERS_COUNT*sizeof(*l_gb_carts_data));
    memset(cin_compats, 0, GAME_CONTROLLERS_COUNT*sizeof(*cin_compats));

    for (i = 0; i < GAME_CONTROLLERS_COUNT; ++i)
    {
        control_ids[i] = (int)i;

        /* if input plugin requests RawData let the input plugin do the channel device processing */
        if (Controls[i].RawData) {
            joybus_devices[i] = &control_ids[i];
            ijoybus_devices[i] = &g_ijoybus_device_plugin_compat;
        }
        else if (Controls[i].Type == CONT_TYPE_VRU) {
            const struct game_controller_flavor* cont_flavor =
                &g_vru_controller_flavor;
            joybus_devices[i] = &g_dev.controllers[i];
            ijoybus_devices[i] = &g_ijoybus_vru_controller;

            cin_compats[i].control_id = (int)i;
            cin_compats[i].cont = &g_dev.controllers[i];
            cin_compats[i].last_pak_type = Controls[i].Plugin;
            cin_compats[i].last_input = 0;

            Controls[i].Plugin = PLUGIN_NONE;

            /* init vru_controller */
            init_game_controller(&g_dev.controllers[i],
                    cont_flavor,
                    &cin_compats[i], &g_icontroller_input_backend_plugin_compat,
                    NULL, NULL);
        }
        /* otherwise let the core do the processing */
        else {
            /* select appropriate controller
             * FIXME: assume for now that only standard controller is compatible
             * Use the rom db to know if other peripherals are compatibles (VRU, mouse, train, ...)
             */
            const struct game_controller_flavor* cont_flavor =
                &g_standard_controller_flavor;

            joybus_devices[i] = &g_dev.controllers[i];
            ijoybus_devices[i] = &g_ijoybus_device_controller;

            cin_compats[i].control_id = (int)i;
            cin_compats[i].cont = &g_dev.controllers[i];
            cin_compats[i].tpk = &g_dev.transferpaks[i];
            cin_compats[i].last_pak_type = Controls[i].Plugin;
            cin_compats[i].last_input = 0;

            l_gb_carts_data[i].control_id = (int)i;

            l_gb_carts_data[i].gbcam_backend = gbcam_backend;
            l_gb_carts_data[i].igbcam_backend = igbcam_backend;

            l_paks_idx[i] = 0;

            //Don't use the selected pak if it's not available for the game, instead use NONE
            if (l_ipaks[l_pak_type_idx[Controls[i].Plugin]] == NULL) {
                Controls[i].Plugin = PLUGIN_NONE;
            }

            /* init all compatibles paks */
            for(k = 0; k < PAK_MAX_SIZE; ++k) {
                /* Bio Pak */
                if (l_ipaks[k] == &g_ibiopak) {
                    init_biopak(&g_dev.biopaks[i], 64);
                    l_paks[i][k] = &g_dev.biopaks[i];

                    if (Controls[i].Plugin == PLUGIN_BIO_PAK) {
                        l_paks_idx[i] = k;
                    }
                }
                /* Memory Pak */
                else if (l_ipaks[k] == &g_imempak) {
                    mpk_storages[i].data = mpk.data + i * MEMPAK_SIZE;
                    mpk_storages[i].size = MEMPAK_SIZE;
                    mpk_storages[i].filename = (void*)&mpk; /* OK for isubfile_storage */

                    init_mempak(&g_dev.mempaks[i], &mpk_storages[i], &g_ifile_storage_ro);
                    l_paks[i][k] = &g_dev.mempaks[i];

                    if (Controls[i].Plugin == PLUGIN_MEMPAK) {
                        l_paks_idx[i] = k;
                    }
                }
                /* Rumble Pak */
                else if (l_ipaks[k] == &g_irumblepak) {
                    init_rumblepak(&g_dev.rumblepaks[i], &control_ids[i], &g_irumble_backend_plugin_compat);
                    l_paks[i][k] = &g_dev.rumblepaks[i];

                    if (Controls[i].Plugin == PLUGIN_RUMBLE_PAK
                     || Controls[i].Plugin == PLUGIN_RAW) {
                        l_paks_idx[i] = k;
                    }
                }
                /* Transfer Pak */
                else if (l_ipaks[k] == &g_itransferpak) {

                    /* init GB cart */
                    init_gb_cart(&g_dev.gb_carts[i],
                            &l_gb_carts_data[i], init_gb_rom, release_gb_rom,
                            &l_gb_carts_data[i], init_gb_ram, release_gb_ram,
                            NULL, &g_iclock_ctime_plus_delta,
                            &l_gb_carts_data[i].control_id, &g_irumble_backend_plugin_compat,
                            l_gb_carts_data[i].gbcam_backend, l_gb_carts_data[i].igbcam_backend);

                    init_transferpak(&g_dev.transferpaks[i], (g_dev.gb_carts[i].read_gb_cart == NULL) ? NULL : &g_dev.gb_carts[i]);
                    l_paks[i][k] = &g_dev.transferpaks[i];

                    if (Controls[i].Plugin == PLUGIN_TRANSFER_PAK) {
                        l_paks_idx[i] = k;
                    }

                    /* enable GB cart switch */
                    // cin_compats[i].gb_cart_switch_enabled = 1;
                }
                /* No Pak */
                else {
                    l_ipaks[k] = NULL;
                    l_paks[i][k] = NULL;

                    if (Controls[i].Plugin == PLUGIN_NONE) {
                        l_paks_idx[i] = k;
                    }

                    break;
                }
            }

            /* init game_controller */
            init_game_controller(&g_dev.controllers[i],
                    cont_flavor,
                    &cin_compats[i], &g_icontroller_input_backend_plugin_compat,
                    l_paks[i][l_paks_idx[i]], l_ipaks[l_paks_idx[i]]);

            if (l_ipaks[l_paks_idx[i]] != NULL) {
                DebugMessage(M64MSG_INFO, "Game controller %u (%s) has a %s plugged in",
                    (uint32_t) i, cont_flavor->name, l_ipaks[l_paks_idx[i]]->name);
            } else {
                DebugMessage(M64MSG_INFO, "Game controller %u (%s) has nothing plugged in",
                    (uint32_t) i, cont_flavor->name);
            }
        }
    }

    for (i = GAME_CONTROLLERS_COUNT; i < PIF_CHANNELS_COUNT; ++i) {
        joybus_devices[i] = &g_dev.cart;
        ijoybus_devices[i] = &g_ijoybus_device_cart;
    }

    l_setup_done = 1;
    init_device(&g_dev,
                g_mem_base,
                r4300_emumode,
                count_per_op,
                count_per_op_denom_pot,
                no_compiled_jump,
                randomize_interrupt,
                g_start_address,
                &g_dev.ai, &audio_out_backend_libretro, ROM_SETTINGS.aidmamodifier,
                si_dma_duration,
                rdram_size,
                joybus_devices, ijoybus_devices,
                vi_clock_from_tv_standard(ROM_PARAMS.systemtype), vi_expected_refresh_rate_from_tv_standard(ROM_PARAMS.systemtype),
                NULL, &g_iclock_ctime_plus_delta,
                g_rom_size,
                eeprom_type,
                &eep, &g_ifile_storage_ro,
                flashram_type,
                &fla, &g_ifile_storage_ro,
                &sra, &g_ifile_storage_ro,
                NULL, dd_rtc_iclock,
                dd_rom_size,
                &dd_disk, dd_idisk);

    // Attach rom to plugins
    failure_rval = M64ERR_PLUGIN_FAIL;
    if (!gfx.romOpen())
    {
        goto on_gfx_open_failure;
    }
    if (!audio.romOpen())
    {
        goto on_audio_open_failure;
    }
    if (!input.romOpen())
    {
        goto on_input_open_failure;
    }

    // setup rendering callback from video plugin to the core
    gfx.setRenderingCallback(video_plugin_render_callback);

    g_EmulatorRunning = 1;
    StateChanged(M64CORE_EMU_STATE, M64EMU_RUNNING);

    poweron_device(&g_dev);
    pif_bootrom_hle_execute(&g_dev.r4300);

    run_device(&g_dev);

    /* Per-frame yield (frame_break set by retro_return at the VI interrupt):
     * the game is still running, so return WITHOUT tearing anything down. The
     * next retro_run re-enters via the l_setup_done path above. Only a genuine
     * stop (frame_break clear) falls through to the teardown below. */
    if (frame_break)
        return M64ERR_SUCCESS;

    /* Genuine stop: allow the one-time setup to run again for the next ROM. */
    l_setup_done = 0;

    /* release gb_carts */
    for(i = 0; i < GAME_CONTROLLERS_COUNT; ++i) {
        if (!Controls[i].RawData  && (Controls[i].Type == CONT_TYPE_STANDARD) && g_dev.gb_carts[i].read_gb_cart != NULL) {
            release_gb_rom(&l_gb_carts_data[i]);
            release_gb_ram(&l_gb_carts_data[i]);
        }
    }

#if 0
    igbcam_backend->close(gbcam_backend);
    igbcam_backend->release(gbcam_backend);

    close_file_storage(&sra);
    close_file_storage(&fla);
    close_file_storage(&eep);
    close_file_storage(&mpk);
#endif

    /* reset pif */
    close_pif();
    close_dd_disk(&dd_disk);

    /* Emulation stopped */
    rsp.romClosed();
    input.romClosed();
    audio.romClosed();
    gfx.romClosed();

    // clean up
    g_EmulatorRunning = 0;
    StateChanged(M64CORE_EMU_STATE, M64EMU_STOPPED);

    /**
     * Actually never returns.
     * Jump back to frontend for deinit
     */
    /* libretro-fork: no libco. The frontend deinit is driven by the
     * RetroArch emu-thread loop (retro_return); just yield back. */
    retro_return(true);

    return M64ERR_SUCCESS;

on_disk_failure:
    failure_rval = M64ERR_INVALID_STATE;
    rsp.romClosed();
    input.romClosed();
on_input_open_failure:
    audio.romClosed();
on_audio_open_failure:
    gfx.romClosed();
on_gfx_open_failure:
    /* release gb_carts */
    for(i = 0; i < GAME_CONTROLLERS_COUNT; ++i) {
        if (!Controls[i].RawData  && (Controls[i].Type == CONT_TYPE_STANDARD) && g_dev.gb_carts[i].read_gb_cart != NULL) {
            release_gb_rom(&l_gb_carts_data[i]);
            release_gb_ram(&l_gb_carts_data[i]);
        }
    }

#if 0
    igbcam_backend->close(gbcam_backend);
    igbcam_backend->release(gbcam_backend);

    /* release storage files */
    close_file_storage(&sra);
    close_file_storage(&fla);
    close_file_storage(&eep);
    close_file_storage(&mpk);
    close_dd_disk(&dd_disk);
#endif

    /* reset pif */
    close_pif();

    return failure_rval;
}

void main_stop(void)
{
    /* note: this operation is asynchronous.  It may be called from a thread other than the
       main emulator thread, and may return before the emulator is completely stopped */
    if (!g_EmulatorRunning)
        return;

    DebugMessage(M64MSG_STATUS, "Stopping emulation.");

    stop_device(&g_dev);
}

m64p_error open_pif(const unsigned char* pifimage, unsigned int size)
{
    md5_byte_t old_pif_ntsc_md5[] = {0x49, 0x21, 0xD5, 0xF2, 0x16, 0x5D, 0xEE, 0x6E, 0x24, 0x96, 0xF4, 0x38, 0x8C, 0x4C, 0x81, 0xDA};
    md5_byte_t old_pif_pal_md5[]  = {0x2B, 0x6E, 0xEC, 0x58, 0x6F, 0xAA, 0x43, 0xF3, 0x46, 0x23, 0x33, 0xB8, 0x44, 0x83, 0x45, 0x54};

    md5_byte_t pif_ntsc_md5[] = {0x5C, 0x12, 0x4E, 0x79, 0x48, 0xAD, 0xA8, 0x5D, 0xA6, 0x03, 0xA5, 0x22, 0x78, 0x29, 0x40, 0xD0};
    md5_byte_t pif_pal_md5[]  = {0xD4, 0x23, 0x2D, 0xC9, 0x35, 0xCA, 0xD0, 0x65, 0x0A, 0xC2, 0x66, 0x4D, 0x52, 0x28, 0x1F, 0x3A};

    uint32_t *dst32 = mem_base_u32(g_mem_base, MM_PIF_MEM);
    uint32_t *src32 = (uint32_t*) pifimage;
    md5_state_t state;
    md5_byte_t digest[16];

    md5_init(&state);
    md5_append(&state, (const md5_byte_t*)pifimage, size);
    md5_finish(&state, digest);

    if (memcmp(digest, old_pif_ntsc_md5, 16) == 0 ||
        memcmp(digest, pif_ntsc_md5, 16) == 0)
    {
        DebugMessage(M64MSG_INFO, "Using NTSC PIF ROM");
    }
    else if (memcmp(digest, old_pif_pal_md5, 16) == 0 ||
             memcmp(digest, pif_pal_md5, 16) == 0)
    {
        DebugMessage(M64MSG_INFO, "Using PAL PIF ROM");
    }
    else
    {
        DebugMessage(M64MSG_ERROR, "Invalid PIF ROM");
        return M64ERR_INPUT_INVALID;
    }

    for (unsigned int i = 0; i < size; i += 4)
        *dst32++ = big32(*src32++);

    g_start_address = UINT32_C(0xbfc00000);
    return M64ERR_SUCCESS;
}

m64p_error close_pif(void)
{
    g_start_address = UINT32_C(0xa4000040);
    return M64ERR_SUCCESS;
}

/* ---- libretro-fork threading shims --------------------------------------
 * The RetroArch in-tree libretro.c drives the core through main_pre_run() +
 * main_run() + mupen_main_stop()/exit(); upstream next folds setup into
 * main_run(). main_pre_run is a no-op here (init_device now happens inside
 * main_run before run_device, as upstream); the stop/exit entry points alias
 * next's main_stop() plus the fork's plugin romClosed teardown. */
m64p_error main_pre_run(void)
{
   return M64ERR_SUCCESS;
}

extern int g_real_stop;
void mupen_main_stop(void)
{
   g_real_stop = 1;
   main_stop();
}

void mupen_main_exit(void)
{
   if (rsp.romClosed)   rsp.romClosed();
   if (input.romClosed) input.romClosed();
   if (gfx.romClosed)   gfx.romClosed();
   if (audio.romClosed) audio.romClosed();
}
