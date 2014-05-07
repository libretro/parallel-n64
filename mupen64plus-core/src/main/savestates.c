/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - savestates.c                                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 CasualJames                                        *
 *   Copyright (C) 2009 Olejl Tillin9                                      *
 *   Copyright (C) 2008 Richard42 Tillin9                                  *
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

#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "api/m64p_config.h"
#include "api/config.h"

#include "savestates.h"
#include "main.h"
#include "rom.h"
#include "util.h"

#include "memory/memory.h"
#include "memory/flashram.h"
#include "r4300/macros.h"
#include "r4300/r4300.h"
#include "r4300/tlb.h"
#include "r4300/interupt.h"
#include "r4300/new_dynarec/new_dynarec.h"
#include "osal/preproc.h"

static const char* savestate_magic = "M64+SAVE";
static const int savestate_latest_version = 0x00010000;  /* 1.0 */

#define GETARRAY(buff, type, count) \
    (to_little_endian_buffer(buff, sizeof(type),count), \
     buff += count*sizeof(type), \
     (type *)(buff-count*sizeof(type)))
#define COPYARRAY(dst, buff, type, count) \
    memcpy(dst, GETARRAY(buff, type, count), sizeof(type)*count)
#define GETDATA(buff, type) *GETARRAY(buff, type, 1)

#define PUTARRAY(src, buff, type, count) \
    memcpy(buff, src, sizeof(type)*count); \
    to_little_endian_buffer(buff, sizeof(type), count); \
    buff += count*sizeof(type);

#define PUTDATA(buff, type, value) \
    do { type x = value; PUTARRAY(&x, buff, type, 1); } while(0)

int savestates_load_m64p(const unsigned char *data, size_t size)
{
    unsigned char header[44], *curr;
    char queue[1024];
    int version;
    int i;

	(void)header;

    curr = (unsigned char*)data; // < HACK

    /* Read and check Mupen64Plus magic number. */
    if(strncmp((char *)curr, savestate_magic, 8)!=0)
    {
        return 0;
    }
    curr += 8;

    version = *curr++;
    version = (version << 8) | *curr++;
    version = (version << 8) | *curr++;
    version = (version << 8) | *curr++;
    if(version != 0x00010000)
    {
        return 0;
    }

    if(memcmp((char *)curr, ROM_SETTINGS.MD5, 32))
    {
        return 0;
    }
    curr += 32;

    // Parse savestate
    rdram_register.rdram_config = GETDATA(curr, unsigned int);
    rdram_register.rdram_device_id = GETDATA(curr, unsigned int);
    rdram_register.rdram_delay = GETDATA(curr, unsigned int);
    rdram_register.rdram_mode = GETDATA(curr, unsigned int);
    rdram_register.rdram_ref_interval = GETDATA(curr, unsigned int);
    rdram_register.rdram_ref_row = GETDATA(curr, unsigned int);
    rdram_register.rdram_ras_interval = GETDATA(curr, unsigned int);
    rdram_register.rdram_min_interval = GETDATA(curr, unsigned int);
    rdram_register.rdram_addr_select = GETDATA(curr, unsigned int);
    rdram_register.rdram_device_manuf = GETDATA(curr, unsigned int);

    MI_register.w_mi_init_mode_reg = GETDATA(curr, unsigned int);
    MI_register.mi_init_mode_reg = GETDATA(curr, unsigned int);
    curr += 4; // Duplicate MI init mode flags from old implementation
    MI_register.mi_version_reg = GETDATA(curr, unsigned int);
    MI_register.mi_intr_reg = GETDATA(curr, unsigned int);
    MI_register.mi_intr_mask_reg = GETDATA(curr, unsigned int);
    MI_register.w_mi_intr_mask_reg = GETDATA(curr, unsigned int);
    curr += 8; // Duplicated MI intr flags and padding from old implementation

    pi_register.pi_dram_addr_reg = GETDATA(curr, unsigned int);
    pi_register.pi_cart_addr_reg = GETDATA(curr, unsigned int);
    pi_register.pi_rd_len_reg = GETDATA(curr, unsigned int);
    pi_register.pi_wr_len_reg = GETDATA(curr, unsigned int);
    pi_register.read_pi_status_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom1_lat_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom1_pwd_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom1_pgs_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom1_rls_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom2_lat_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom2_pwd_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom2_pgs_reg = GETDATA(curr, unsigned int);
    pi_register.pi_bsd_dom2_rls_reg = GETDATA(curr, unsigned int);

    sp_register.sp_mem_addr_reg = GETDATA(curr, unsigned int);
    sp_register.sp_dram_addr_reg = GETDATA(curr, unsigned int);
    sp_register.sp_rd_len_reg = GETDATA(curr, unsigned int);
    sp_register.sp_wr_len_reg = GETDATA(curr, unsigned int);
    sp_register.w_sp_status_reg = GETDATA(curr, unsigned int);
    sp_register.sp_status_reg = GETDATA(curr, unsigned int);
    curr += 16; // Duplicated SP flags and padding from old implementation
    sp_register.sp_dma_full_reg = GETDATA(curr, unsigned int);
    sp_register.sp_dma_busy_reg = GETDATA(curr, unsigned int);
    sp_register.sp_semaphore_reg = GETDATA(curr, unsigned int);

    rsp_register.rsp_pc = GETDATA(curr, unsigned int);
    rsp_register.rsp_ibist = GETDATA(curr, unsigned int);

    si_register.si_dram_addr = GETDATA(curr, unsigned int);
    si_register.si_pif_addr_rd64b = GETDATA(curr, unsigned int);
    si_register.si_pif_addr_wr64b = GETDATA(curr, unsigned int);
    si_register.si_stat = GETDATA(curr, unsigned int);

    vi_register.vi_status = GETDATA(curr, unsigned int);
    vi_register.vi_origin = GETDATA(curr, unsigned int);
    vi_register.vi_width = GETDATA(curr, unsigned int);
    vi_register.vi_v_intr = GETDATA(curr, unsigned int);
    vi_register.vi_current = GETDATA(curr, unsigned int);
    vi_register.vi_burst = GETDATA(curr, unsigned int);
    vi_register.vi_v_sync = GETDATA(curr, unsigned int);
    vi_register.vi_h_sync = GETDATA(curr, unsigned int);
    vi_register.vi_leap = GETDATA(curr, unsigned int);
    vi_register.vi_h_start = GETDATA(curr, unsigned int);
    vi_register.vi_v_start = GETDATA(curr, unsigned int);
    vi_register.vi_v_burst = GETDATA(curr, unsigned int);
    vi_register.vi_x_scale = GETDATA(curr, unsigned int);
    vi_register.vi_y_scale = GETDATA(curr, unsigned int);
    vi_register.vi_delay = GETDATA(curr, unsigned int);

    update_vi_status(vi_register.vi_status);
    update_vi_width(vi_register.vi_width);

    ri_register.ri_mode = GETDATA(curr, unsigned int);
    ri_register.ri_config = GETDATA(curr, unsigned int);
    ri_register.ri_current_load = GETDATA(curr, unsigned int);
    ri_register.ri_select = GETDATA(curr, unsigned int);
    ri_register.ri_refresh = GETDATA(curr, unsigned int);
    ri_register.ri_latency = GETDATA(curr, unsigned int);
    ri_register.ri_error = GETDATA(curr, unsigned int);
    ri_register.ri_werror = GETDATA(curr, unsigned int);

    ai_register.ai_dram_addr = GETDATA(curr, unsigned int);
    ai_register.ai_len = GETDATA(curr, unsigned int);
    ai_register.ai_control = GETDATA(curr, unsigned int);
    ai_register.ai_status = GETDATA(curr, unsigned int);
    ai_register.ai_dacrate = GETDATA(curr, unsigned int);
    ai_register.ai_bitrate = GETDATA(curr, unsigned int);
    ai_register.next_delay = GETDATA(curr, unsigned int);
    ai_register.next_len = GETDATA(curr, unsigned int);
    ai_register.current_delay = GETDATA(curr, unsigned int);
    ai_register.current_len = GETDATA(curr, unsigned int);
    update_ai_dacrate(ai_register.ai_dacrate);

    dpc_register.dpc_start = GETDATA(curr, unsigned int);
    dpc_register.dpc_end = GETDATA(curr, unsigned int);
    dpc_register.dpc_current = GETDATA(curr, unsigned int);
    dpc_register.w_dpc_status = GETDATA(curr, unsigned int);
    dpc_register.dpc_status = GETDATA(curr, unsigned int);
    curr += 12; // Duplicated DPC flags and padding from old implementation
    dpc_register.dpc_clock = GETDATA(curr, unsigned int);
    dpc_register.dpc_bufbusy = GETDATA(curr, unsigned int);
    dpc_register.dpc_pipebusy = GETDATA(curr, unsigned int);
    dpc_register.dpc_tmem = GETDATA(curr, unsigned int);

    dps_register.dps_tbist = GETDATA(curr, unsigned int);
    dps_register.dps_test_mode = GETDATA(curr, unsigned int);
    dps_register.dps_buftest_addr = GETDATA(curr, unsigned int);
    dps_register.dps_buftest_data = GETDATA(curr, unsigned int);

    COPYARRAY(rdram, curr, unsigned int, 0x800000/4);
    COPYARRAY(SP_DMEM, curr, unsigned int, 0x1000/4);
    COPYARRAY(SP_IMEM, curr, unsigned int, 0x1000/4);
    COPYARRAY(PIF_RAM, curr, unsigned char, 0x40);

    flashram_info.use_flashram = GETDATA(curr, int);
    flashram_info.mode = GETDATA(curr, int);
    flashram_info.status = GETDATA(curr, unsigned long long);
    flashram_info.erase_offset = GETDATA(curr, unsigned int);
    flashram_info.write_pointer = GETDATA(curr, unsigned int);

    COPYARRAY(tlb_LUT_r, curr, unsigned int, 0x100000);
    COPYARRAY(tlb_LUT_w, curr, unsigned int, 0x100000);

    llbit = GETDATA(curr, unsigned int);
    COPYARRAY(reg, curr, long long int, 32);
    COPYARRAY(reg_cop0, curr, unsigned int, 32);
    set_fpr_pointers(Status);  // Status is reg_cop0[12]
    lo = GETDATA(curr, long long int);
    hi = GETDATA(curr, long long int);
    COPYARRAY(reg_cop1_fgr_64, curr, long long int, 32);
    if ((Status & 0x04000000) == 0)  // 32-bit FPR mode requires data shuffling because 64-bit layout is always stored in savestate file
        shuffle_fpr_data(0x04000000, 0);
    FCR0 = GETDATA(curr, int);
    FCR31 = GETDATA(curr, int);

    for (i = 0; i < 32; i++)
    {
        tlb_e[i].mask = GETDATA(curr, short);
        curr += 2;
        tlb_e[i].vpn2 = GETDATA(curr, int);
        tlb_e[i].g = GETDATA(curr, char);
        tlb_e[i].asid = GETDATA(curr, unsigned char);
        curr += 2;
        tlb_e[i].pfn_even = GETDATA(curr, int);
        tlb_e[i].c_even = GETDATA(curr, char);
        tlb_e[i].d_even = GETDATA(curr, char);
        tlb_e[i].v_even = GETDATA(curr, char);
        curr++;
        tlb_e[i].pfn_odd = GETDATA(curr, int);
        tlb_e[i].c_odd = GETDATA(curr, char);
        tlb_e[i].d_odd = GETDATA(curr, char);
        tlb_e[i].v_odd = GETDATA(curr, char);
        tlb_e[i].r = GETDATA(curr, char);
   
        tlb_e[i].start_even = GETDATA(curr, unsigned int);
        tlb_e[i].end_even = GETDATA(curr, unsigned int);
        tlb_e[i].phys_even = GETDATA(curr, unsigned int);
        tlb_e[i].start_odd = GETDATA(curr, unsigned int);
        tlb_e[i].end_odd = GETDATA(curr, unsigned int);
        tlb_e[i].phys_odd = GETDATA(curr, unsigned int);
    }

#ifdef NEW_DYNAREC
    if (r4300emu == CORE_DYNAREC) {
        pcaddr = GETDATA(curr, unsigned int);
        pending_exception = 1;
        invalidate_all_pages();
    } else {
        if(r4300emu != CORE_PURE_INTERPRETER)
        {
            for (i = 0; i < 0x100000; i++)
                invalid_code[i] = 1;
        }
        generic_jump_to(GETDATA(curr, unsigned int)); // PC
    }
#else
    if(r4300emu != CORE_PURE_INTERPRETER)
    {
        for (i = 0; i < 0x100000; i++)
            invalid_code[i] = 1;
    }
    generic_jump_to(GETDATA(curr, unsigned int)); // PC
#endif

    next_interupt = GETDATA(curr, unsigned int);
    next_vi = GETDATA(curr, unsigned int);
    vi_field = GETDATA(curr, unsigned int);

    memcpy(queue, curr, sizeof(queue));
    to_little_endian_buffer(queue, 4, 256);
    load_eventqueue_infos(queue);

#ifdef NEW_DYNAREC
    if (r4300emu == CORE_DYNAREC)
        last_addr = pcaddr;
    else
        last_addr = PC->addr;
#else
    last_addr = PC->addr;
#endif

    // deliver callback to indicate completion of state loading operation
    StateChanged(M64CORE_STATE_LOADCOMPLETE, 1);

    return 1;
}

int savestates_save_m64p(unsigned char *data, size_t size)
{
    unsigned char outbuf[4];
    int i;

    char queue[1024];
    int queuelength;

    unsigned char *curr = data;

    queuelength = save_eventqueue_infos(queue);

    // Write the save state data to memory
    PUTARRAY(savestate_magic, curr, unsigned char, 8);

    outbuf[0] = (savestate_latest_version >> 24) & 0xff;
    outbuf[1] = (savestate_latest_version >> 16) & 0xff;
    outbuf[2] = (savestate_latest_version >>  8) & 0xff;
    outbuf[3] = (savestate_latest_version >>  0) & 0xff;
    PUTARRAY(outbuf, curr, unsigned char, 4);

    PUTARRAY(ROM_SETTINGS.MD5, curr, char, 32);

    PUTDATA(curr, unsigned int, rdram_register.rdram_config);
    PUTDATA(curr, unsigned int, rdram_register.rdram_device_id);
    PUTDATA(curr, unsigned int, rdram_register.rdram_delay);
    PUTDATA(curr, unsigned int, rdram_register.rdram_mode);
    PUTDATA(curr, unsigned int, rdram_register.rdram_ref_interval);
    PUTDATA(curr, unsigned int, rdram_register.rdram_ref_row);
    PUTDATA(curr, unsigned int, rdram_register.rdram_ras_interval);
    PUTDATA(curr, unsigned int, rdram_register.rdram_min_interval);
    PUTDATA(curr, unsigned int, rdram_register.rdram_addr_select);
    PUTDATA(curr, unsigned int, rdram_register.rdram_device_manuf);

    PUTDATA(curr, unsigned int, MI_register.w_mi_init_mode_reg);
    PUTDATA(curr, unsigned int, MI_register.mi_init_mode_reg);
    PUTDATA(curr, unsigned char, MI_register.mi_init_mode_reg & 0x7F);
    PUTDATA(curr, unsigned char, (MI_register.mi_init_mode_reg & 0x80) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_init_mode_reg & 0x100) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_init_mode_reg & 0x200) != 0);
    PUTDATA(curr, unsigned int, MI_register.mi_version_reg);
    PUTDATA(curr, unsigned int, MI_register.mi_intr_reg);
    PUTDATA(curr, unsigned int, MI_register.mi_intr_mask_reg);
    PUTDATA(curr, unsigned int, MI_register.w_mi_intr_mask_reg);
    PUTDATA(curr, unsigned char, (MI_register.mi_intr_mask_reg & 0x1) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_intr_mask_reg & 0x2) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_intr_mask_reg & 0x4) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_intr_mask_reg & 0x8) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_intr_mask_reg & 0x10) != 0);
    PUTDATA(curr, unsigned char, (MI_register.mi_intr_mask_reg & 0x20) != 0);
    PUTDATA(curr, unsigned short, 0); // Padding from old implementation

    PUTDATA(curr, unsigned int, pi_register.pi_dram_addr_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_cart_addr_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_rd_len_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_wr_len_reg);
    PUTDATA(curr, unsigned int, pi_register.read_pi_status_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom1_lat_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom1_pwd_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom1_pgs_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom1_rls_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom2_lat_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom2_pwd_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom2_pgs_reg);
    PUTDATA(curr, unsigned int, pi_register.pi_bsd_dom2_rls_reg);

    PUTDATA(curr, unsigned int, sp_register.sp_mem_addr_reg);
    PUTDATA(curr, unsigned int, sp_register.sp_dram_addr_reg);
    PUTDATA(curr, unsigned int, sp_register.sp_rd_len_reg);
    PUTDATA(curr, unsigned int, sp_register.sp_wr_len_reg);
    PUTDATA(curr, unsigned int, sp_register.w_sp_status_reg);
    PUTDATA(curr, unsigned int, sp_register.sp_status_reg);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x1) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x2) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x4) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x8) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x10) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x20) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x40) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x80) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x100) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x200) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x400) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x800) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x1000) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x2000) != 0);
    PUTDATA(curr, unsigned char, (sp_register.sp_status_reg & 0x4000) != 0);
    PUTDATA(curr, unsigned char, 0);
    PUTDATA(curr, unsigned int, sp_register.sp_dma_full_reg);
    PUTDATA(curr, unsigned int, sp_register.sp_dma_busy_reg);
    PUTDATA(curr, unsigned int, sp_register.sp_semaphore_reg);

    PUTDATA(curr, unsigned int, rsp_register.rsp_pc);
    PUTDATA(curr, unsigned int, rsp_register.rsp_ibist);

    PUTDATA(curr, unsigned int, si_register.si_dram_addr);
    PUTDATA(curr, unsigned int, si_register.si_pif_addr_rd64b);
    PUTDATA(curr, unsigned int, si_register.si_pif_addr_wr64b);
    PUTDATA(curr, unsigned int, si_register.si_stat);

    PUTDATA(curr, unsigned int, vi_register.vi_status);
    PUTDATA(curr, unsigned int, vi_register.vi_origin);
    PUTDATA(curr, unsigned int, vi_register.vi_width);
    PUTDATA(curr, unsigned int, vi_register.vi_v_intr);
    PUTDATA(curr, unsigned int, vi_register.vi_current);
    PUTDATA(curr, unsigned int, vi_register.vi_burst);
    PUTDATA(curr, unsigned int, vi_register.vi_v_sync);
    PUTDATA(curr, unsigned int, vi_register.vi_h_sync);
    PUTDATA(curr, unsigned int, vi_register.vi_leap);
    PUTDATA(curr, unsigned int, vi_register.vi_h_start);
    PUTDATA(curr, unsigned int, vi_register.vi_v_start);
    PUTDATA(curr, unsigned int, vi_register.vi_v_burst);
    PUTDATA(curr, unsigned int, vi_register.vi_x_scale);
    PUTDATA(curr, unsigned int, vi_register.vi_y_scale);
    PUTDATA(curr, unsigned int, vi_register.vi_delay);

    PUTDATA(curr, unsigned int, ri_register.ri_mode);
    PUTDATA(curr, unsigned int, ri_register.ri_config);
    PUTDATA(curr, unsigned int, ri_register.ri_current_load);
    PUTDATA(curr, unsigned int, ri_register.ri_select);
    PUTDATA(curr, unsigned int, ri_register.ri_refresh);
    PUTDATA(curr, unsigned int, ri_register.ri_latency);
    PUTDATA(curr, unsigned int, ri_register.ri_error);
    PUTDATA(curr, unsigned int, ri_register.ri_werror);

    PUTDATA(curr, unsigned int, ai_register.ai_dram_addr);
    PUTDATA(curr, unsigned int, ai_register.ai_len);
    PUTDATA(curr, unsigned int, ai_register.ai_control);
    PUTDATA(curr, unsigned int, ai_register.ai_status);
    PUTDATA(curr, unsigned int, ai_register.ai_dacrate);
    PUTDATA(curr, unsigned int, ai_register.ai_bitrate);
    PUTDATA(curr, unsigned int, ai_register.next_delay);
    PUTDATA(curr, unsigned int, ai_register.next_len);
    PUTDATA(curr, unsigned int, ai_register.current_delay);
    PUTDATA(curr, unsigned int, ai_register.current_len);

    PUTDATA(curr, unsigned int, dpc_register.dpc_start);
    PUTDATA(curr, unsigned int, dpc_register.dpc_end);
    PUTDATA(curr, unsigned int, dpc_register.dpc_current);
    PUTDATA(curr, unsigned int, dpc_register.w_dpc_status);
    PUTDATA(curr, unsigned int, dpc_register.dpc_status);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x1) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x2) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x4) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x8) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x10) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x20) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x40) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x80) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x100) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x200) != 0);
    PUTDATA(curr, unsigned char, (dpc_register.dpc_status & 0x400) != 0);
    PUTDATA(curr, unsigned char, 0);
    PUTDATA(curr, unsigned int, dpc_register.dpc_clock);
    PUTDATA(curr, unsigned int, dpc_register.dpc_bufbusy);
    PUTDATA(curr, unsigned int, dpc_register.dpc_pipebusy);
    PUTDATA(curr, unsigned int, dpc_register.dpc_tmem);

    PUTDATA(curr, unsigned int, dps_register.dps_tbist);
    PUTDATA(curr, unsigned int, dps_register.dps_test_mode);
    PUTDATA(curr, unsigned int, dps_register.dps_buftest_addr);
    PUTDATA(curr, unsigned int, dps_register.dps_buftest_data);

    PUTARRAY(rdram, curr, unsigned int, 0x800000/4);
    PUTARRAY(SP_DMEM, curr, unsigned int, 0x1000/4);
    PUTARRAY(SP_IMEM, curr, unsigned int, 0x1000/4);
    PUTARRAY(PIF_RAM, curr, unsigned char, 0x40);

    PUTDATA(curr, int, flashram_info.use_flashram);
    PUTDATA(curr, int, flashram_info.mode);
    PUTDATA(curr, unsigned long long, flashram_info.status);
    PUTDATA(curr, unsigned int, flashram_info.erase_offset);
    PUTDATA(curr, unsigned int, flashram_info.write_pointer);

    PUTARRAY(tlb_LUT_r, curr, unsigned int, 0x100000);
    PUTARRAY(tlb_LUT_w, curr, unsigned int, 0x100000);

    PUTDATA(curr, unsigned int, llbit);
    PUTARRAY(reg, curr, long long int, 32);
    PUTARRAY(reg_cop0, curr, unsigned int, 32);
    PUTDATA(curr, long long int, lo);
    PUTDATA(curr, long long int, hi);

    if ((Status & 0x04000000) == 0) // FR bit == 0 means 32-bit (MIPS I) FGR mode
        shuffle_fpr_data(0, 0x04000000);  // shuffle data into 64-bit register format for storage
    PUTARRAY(reg_cop1_fgr_64, curr, long long int, 32);
    if ((Status & 0x04000000) == 0)
        shuffle_fpr_data(0x04000000, 0);  // put it back in 32-bit mode

    PUTDATA(curr, int, FCR0);
    PUTDATA(curr, int, FCR31);
    for (i = 0; i < 32; i++)
    {
        PUTDATA(curr, short, tlb_e[i].mask);
        PUTDATA(curr, short, 0);
        PUTDATA(curr, int, tlb_e[i].vpn2);
        PUTDATA(curr, char, tlb_e[i].g);
        PUTDATA(curr, unsigned char, tlb_e[i].asid);
        PUTDATA(curr, short, 0);
        PUTDATA(curr, int, tlb_e[i].pfn_even);
        PUTDATA(curr, char, tlb_e[i].c_even);
        PUTDATA(curr, char, tlb_e[i].d_even);
        PUTDATA(curr, char, tlb_e[i].v_even);
        PUTDATA(curr, char, 0);
        PUTDATA(curr, int, tlb_e[i].pfn_odd);
        PUTDATA(curr, char, tlb_e[i].c_odd);
        PUTDATA(curr, char, tlb_e[i].d_odd);
        PUTDATA(curr, char, tlb_e[i].v_odd);
        PUTDATA(curr, char, tlb_e[i].r);
   
        PUTDATA(curr, unsigned int, tlb_e[i].start_even);
        PUTDATA(curr, unsigned int, tlb_e[i].end_even);
        PUTDATA(curr, unsigned int, tlb_e[i].phys_even);
        PUTDATA(curr, unsigned int, tlb_e[i].start_odd);
        PUTDATA(curr, unsigned int, tlb_e[i].end_odd);
        PUTDATA(curr, unsigned int, tlb_e[i].phys_odd);
    }
#ifdef NEW_DYNAREC
    if (r4300emu == CORE_DYNAREC)
        PUTDATA(curr, unsigned int, pcaddr);
    else
        PUTDATA(curr, unsigned int, PC->addr);
#else
    PUTDATA(curr, unsigned int, PC->addr);
#endif

    PUTDATA(curr, unsigned int, next_interupt);
    PUTDATA(curr, unsigned int, next_vi);
    PUTDATA(curr, unsigned int, vi_field);

    to_little_endian_buffer(queue, 4, queuelength/4);
    PUTARRAY(queue, curr, char, queuelength);

    // deliver callback to indicate completion of state saving operation
    StateChanged(M64CORE_STATE_SAVECOMPLETE, 1);

    return 1;
}

