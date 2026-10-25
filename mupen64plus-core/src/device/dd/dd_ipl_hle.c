/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dd_ipl_hle.c                                            *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Cleanroom 64DD IPL. The IPL's code was never read: everything below
 * comes from the disk format, from what games read out of the drive's ROM
 * and from the machine state measured at the moment the retail IPL jumps
 * into a disk's boot code. */

#include "dd_ipl_hle.h"

#include <string.h>

#include "api/m64p_types.h"
#include "backends/api/storage_backend.h"
#include "device/dd/dd_controller.h"
#include "device/dd/disk.h"
#include "device/device.h"
#include "device/memory/m64p_memory.h"
#include "device/r4300/cp0.h"
#include "device/r4300/cp1.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/tlb.h"
#include "device/rcp/ai/ai_controller.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/pi/pi_controller.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/rcp/si/si_controller.h"
#include "device/rcp/vi/vi_controller.h"
#include "device/rdram/rdram.h"
#include "main/main.h"
#include "osal/preproc.h"

#include "dd_ipl_hle_font.h"

static int s_enabled;
static int s_latin_filled;

void dd_ipl_hle_set_enabled(int enabled)
{
    s_enabled = enabled ? 1 : 0;
}

int dd_ipl_hle_enabled(void)
{
    return s_enabled;
}

/* ---- ROM image ---------------------------------------------------------- */

/* Word 0 is the PI domain 1 timing every N64 ROM leads with; the OS reads
 * it to set up its DD ROM handle. Japanese games test the word at 0x1010
 * against this value to recognise the retail Japanese drive. */
#define DD_ROM_PI_TIMING   UINT32_C(0x80270740)
#define DD_ROM_JP_MARK_AT  0x1010
#define DD_ROM_JP_MARK     UINT32_C(0x2129fff8)

/* The drive library checks the top byte of the word at 0x9ff00 when it
 * re-reads a disk's ID: 0xc3 on the Japanese drive, 0x04 on the American
 * one. Anything else stops it in a loop. */
#define DD_ROM_ID_MARK_AT  0x9ff00
#define DD_ROM_ID_MARK_JP  UINT32_C(0xc3000000)
#define DD_ROM_ID_MARK_US  UINT32_C(0x04000000)

/* The Latin fonts need the game's drive library in RDRAM: filled after the
 * boot code is loaded, or at the first fetch from them if the library came
 * in later. */
static void fill_latin(void)
{
    if (s_latin_filled)
        return;
    s_latin_filled = dd_ipl_hle_font_latin(
        mem_base_u32(g_dev.mem.base, MM_DD_ROM) + DD_IPL_HLE_FONT_START / 4,
        (DD_IPL_HLE_WAVE_START - DD_IPL_HLE_FONT_START) / 4,
        mem_base_u32(g_dev.mem.base, MM_RDRAM_DRAM), g_dev.rdram.dram_size);
}

void dd_ipl_hle_rom_fetch(uint32_t rom_off)
{
    if (s_enabled && rom_off >= DD_IPL_HLE_FONT_START + DD_IPL_HLE_LATIN_START
     && rom_off < DD_IPL_HLE_WAVE_START)
        fill_latin();
}

size_t dd_ipl_hle_build_rom(uint32_t* rom, unsigned int region)
{
    s_latin_filled = 0;
    memset(rom, 0, DD_IPL_HLE_ROM_SIZE);
    rom[0] = DD_ROM_PI_TIMING;
    if (region == DDREGION_JAPAN)
    {
        rom[DD_ROM_JP_MARK_AT / 4] = DD_ROM_JP_MARK;
        rom[DD_ROM_ID_MARK_AT / 4] = DD_ROM_ID_MARK_JP;
    }
    else if (region == DDREGION_US)
        rom[DD_ROM_ID_MARK_AT / 4] = DD_ROM_ID_MARK_US;
    dd_ipl_hle_font_build(rom + DD_IPL_HLE_FONT_START / 4,
                          (DD_IPL_HLE_WAVE_START - DD_IPL_HLE_FONT_START) / 4);
    /* the wave area stays zero: the samples play as silence */
    return DD_IPL_HLE_ROM_SIZE;
}

/* ---- boot --------------------------------------------------------------- */

static void rdram_write8(uint8_t* dram, size_t dram_size, uint32_t addr,
                         uint8_t value)
{
    addr &= UINT32_C(0x1fffffff);
    if (addr < dram_size)
        dram[addr ^ S8] = value;
}

static void rdram_write32(uint8_t* dram, size_t dram_size, uint32_t addr,
                          uint32_t value)
{
    addr &= UINT32_C(0x1fffffff);
    if (addr + 4 <= dram_size)
        *(uint32_t*)(dram + addr) = value;
}

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Copy one logical block of the disk to RDRAM. Returns the bytes copied. */
static uint32_t load_lba(const struct dd_disk* disk,
                         const struct dd_sys_data* sys, uint32_t lba,
                         uint8_t* dram, size_t dram_size, uint32_t addr)
{
    uint16_t phys = LBAToPhys(sys, lba);
    unsigned int head = (phys >> 12) & 1;
    unsigned int track = phys & 0xfff;
    unsigned int block = (phys >> 13) & 1;
    unsigned int size = zone_sec_size_phys[get_zone_from_head_track(head, track)];
    unsigned int sector, i;

    for (sector = 0; sector < SECTORS_PER_BLOCK; ++sector)
    {
        const uint8_t* src = get_sector_base(disk, head, track, block, sector);
        for (i = 0; i < size; ++i)
            rdram_write8(dram, dram_size, addr + sector * size + i,
                         src != NULL ? src[i] : 0);
    }
    return SECTORS_PER_BLOCK * size;
}

/* The first logical block of the user area: the boot code starts here. */
#define DD_BOOT_LBA        SYSTEM_LBAS

/* The IPL leaves the disk's region code one byte at a time at offset 0x10
 * of each exception vector, past the 16 bytes the OS installs there; the
 * drive library reads it back to tell a retail drive from a development
 * one. */
static const uint32_t s_region_at[4] = { 0x010, 0x090, 0x110, 0x190 };

/* Where the IPL leaves the first 0x20 bytes of the disk ID. */
#define DD_DISK_ID_ADDR    UINT32_C(0x1a0)
#define DD_DISK_ID_SIZE    0x20

/* Machine state at the jump into the boot code, measured on the retail
 * Japanese IPL. The game's own start-up overwrites most of it; it is
 * reproduced so that what it does not overwrite matches too. The FPU
 * registers, the VI origin and the PI DRAM address are what the IPL's logo
 * animation was doing when the load finished, so they depend on how long
 * the boot code took to load; the values are those of a 43-block boot
 * (Mario Artist: Talent Studio). Everything else is the same for every
 * disk, or follows from the disk below. */
static const uint64_t s_gpr[32] = {
    UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
    UINT64_C(0xffffffffa4000400), UINT64_C(0x0000000000000100),
    UINT64_C(0x0000000000000000), UINT64_C(0xffffffffa4040010),
    UINT64_C(0xffffffffa4600010), UINT64_C(0x0000000000000000),
    UINT64_C(0xffffffff80002000), UINT64_C(0xffffffff80001ff0),
    UINT64_C(0x0000000000002000), UINT64_C(0xffffffffa4800000),
    UINT64_C(0xffffffffa4500000), UINT64_C(0x0000000000000800),
    UINT64_C(0xffffffffa4300000), UINT64_C(0x0000000000000002),
    UINT64_C(0x0000000000000003), UINT64_C(0xffffffff8029cfc0),
    UINT64_C(0xffffffff80299f8c), UINT64_C(0x00000000000000c8),
    UINT64_C(0x0000000000000003), UINT64_C(0xffffffff8029d098),
    UINT64_C(0xffffffff8029cfd8), UINT64_C(0xffffffff8027e164),
    UINT64_C(0xffffffff8029d080), UINT64_C(0xffffffffa4500000),
    UINT64_C(0xffffffffa430000c), UINT64_C(0x0000000000000aaa),
    UINT64_C(0x0000000000000000), UINT64_C(0xffffffff80299f30),
    UINT64_C(0x0000000000000003), UINT64_C(0xffffffff80261c0c)
};

static const uint32_t s_cp0[32] = {
    0x0000001f, 0x0000001f, 0x02000017, 0x00000001,
    0x007ffff0, 0x00000000, 0x00000000, 0x00000000,
    0xffffffff, 0x00000000, 0x00000000, 0x00000000,
    0x30000000, 0x10000000, 0x802766c8, 0x00000b22,
    0x7006e463, 0xffffffff, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0xffffffff, 0x00000000
};

/* The even FPRs (Status.FR is clear) and the FPU control word. */
static const uint64_t s_fpr_even[16] = {
    UINT64_C(0x3ff000004765c059), UINT64_C(0x3fe7691144f10000),
    UINT64_C(0xbf29c8344f494ac2), UINT64_C(0xbf1b98e54cd678b2),
    UINT64_C(0x3fd90f03ffffd694), UINT64_C(0x3f8110ed4a62e100),
    UINT64_C(0x3fe120614f4ffe88), UINT64_C(0xbfc4c5094762e100),
    UINT64_C(0x3fe5609bc625b000), UINT64_C(0xbfb043ad4f49120a),
    0, 0, 0, 0, 0, 0
};
#define DD_HANDOVER_FCR31  UINT32_C(0x01000800)

/* Count at the first instruction of the boot code, and the distance from
 * there to the next VI interrupt. */
#define DD_HANDOVER_COUNT  UINT32_C(0x00002dde)
#define DD_HANDOVER_VI_IN  UINT32_C(796244)

/* The boot code's last seek leaves the motor active with its standby timer
 * running; this much of it is left at the jump. */
#define DD_HANDOVER_DV_IN  UINT32_C(0x0747a372)

static const uint32_t s_vi[VI_REGS_COUNT] = {
    0x00013016, 0x00394a80, 0x00000140, 0x000003ff,
    0x00000000, 0x03e52239, 0x0000020d, 0x00000c15,
    0x0c150c15, 0x00000000, 0x002501ff, 0x000e0204,
    0x00000200, 0x00000400
};

static const uint32_t s_pi[PI_REGS_COUNT] = {
    0x0033f450, 0x050003a0, 0x0000003f, 0x0000039f,
    0x00000000, 0x00000040, 0x00000007, 0x00000007,
    0x00000002, 0x00000003, 0x00000006, 0x00000006,
    0x00000002
};

static const uint32_t s_ri[RI_REGS_COUNT] = {
    0x0000000e, 0x00000040, 0x00000000, 0x00000014,
    0x007e3634, 0x00000000, 0x00000000, 0x00000000
};

static const uint32_t s_sp[4] = {
    0x00001720, 0x0027e0f0, 0x0000006f, 0x00000007
};

static const uint32_t s_si[3] = { 0x00342e30, 0x1fc007c0, 0x1fc007c0 };

static const uint32_t s_dd[DD_ASIC_REGS_COUNT] = {
    0x00250000, 0x00000000, 0x11000000, 0x60250000,
    0x00000000, 0x00000000, 0x40000000, 0x00b20000,
    0x00000000, 0x00000000, 0x000000e7, 0x00000000,
    0x00000059, 0x00000000, 0x00000000, 0x00000000,
    0x00030000, 0x00000000, 0x00000000
};

int dd_ipl_hle_boot(struct r4300_core* r4300, struct dd_controller* dd,
                    unsigned int tv_type, unsigned int reset_type)
{
    const uint8_t* image;
    const struct dd_sys_data* sys;
    uint8_t* dram;
    size_t dram_size;
    uint32_t blocks, entry, addr, lba, i;
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    struct tlb_entry* e;

    if (dd->disk == NULL || dd->idisk == NULL)
        return 0;

    image = dd->idisk->data(dd->disk);
    sys = (const struct dd_sys_data*)(image + dd->disk->offset_sys);
    blocks = ((uint32_t)image[dd->disk->offset_sys + 6] << 8)
           | image[dd->disk->offset_sys + 7];
    entry = be32(image + dd->disk->offset_sys + 0x1c);
    if (blocks == 0 || DD_BOOT_LBA + blocks > SIZE_LBA)
        return 0;

    dram = (uint8_t*)mem_base_u32(r4300->mem->base, MM_RDRAM_DRAM);
    dram_size = g_dev.rdram.dram_size;

    /* the boot code, then the disk ID */
    addr = entry;
    for (lba = DD_BOOT_LBA; lba < DD_BOOT_LBA + blocks; ++lba)
        addr += load_lba(dd->disk, sys, lba, dram, dram_size, addr);
    fill_latin();
    for (i = 0; i < DD_DISK_ID_SIZE; ++i)
        rdram_write8(dram, dram_size, DD_DISK_ID_ADDR + i,
                     image[dd->disk->offset_id + i]);

    for (i = 0; i < 4; ++i)
        rdram_write8(dram, dram_size, s_region_at[i],
                     image[dd->disk->offset_sys + i]);

    /* the OS boot parameters */
    rdram_write32(dram, dram_size, 0x300, tv_type);
    rdram_write32(dram, dram_size, 0x304, 1);                 /* DD */
    rdram_write32(dram, dram_size, 0x308, UINT32_C(0xa6000000));
    rdram_write32(dram, dram_size, 0x30c, reset_type);
    rdram_write32(dram, dram_size, 0x310, UINT32_C(0x64dd));
    rdram_write32(dram, dram_size, 0x314, 0);
    rdram_write32(dram, dram_size, 0x318, (uint32_t)dram_size);

    /* CPU */
    for (i = 0; i < 32; ++i)
        r4300_regs(r4300)[i] = (int64_t)s_gpr[i];
    r4300_regs(r4300)[4] = (int64_t)(int32_t)entry;    /* a0 */
    *r4300_mult_hi(r4300) = 0;
    *r4300_mult_lo(r4300) = 1;
    for (i = 0; i < 32; ++i)
        if (i != CP0_COUNT_REG && i != CP0_COMPARE_REG)
            cp0_regs[i] = s_cp0[i];
    cp0_regs[CP0_COMPARE_REG] = 0;
    for (i = 0; i < 32; ++i)
        r4300_cp1_regs(&r4300->cp1)[i].dword = (i & 1) ? 0 : (int64_t)s_fpr_even[i / 2];
    *r4300_cp1_fcr31(&r4300->cp1) = DD_HANDOVER_FCR31;
    set_fpr_pointers(&r4300->cp1, cp0_regs[CP0_STATUS_REG]);

    /* one global TLB entry, as the last EntryHi/EntryLo describe it */
    e = &r4300->cp0.tlb.entries[31];
    tlb_unmap(&r4300->cp0.tlb, 31);
    memset(e, 0, sizeof(*e));
    e->vpn2 = UINT32_C(0x60000);
    e->g = 1;
    e->pfn_even = UINT32_C(0x80000);
    e->c_even = 2;
    e->d_even = 1;
    e->v_even = 1;
    e->start_even = e->vpn2 << 13;
    e->end_even = e->start_even + 0xfff;
    e->phys_even = e->pfn_even << 12;
    e->start_odd = e->end_even + 1;
    e->end_odd = e->start_odd + 0xfff;
    e->phys_odd = 0;
    tlb_map(&r4300->cp0.tlb, 31);

    /* RCP; VI through its register writes, which derive the VI timing */
    for (i = 0; i < VI_REGS_COUNT; ++i)
        if (i != VI_CURRENT_REG)
            r4300_write_aligned_word(r4300, R4300_KSEG1 + MM_VI_REGS + 4*i,
                                     s_vi[i], ~UINT32_C(0));
    g_dev.vi.field = 0;
    for (i = 0; i < PI_REGS_COUNT; ++i)
        g_dev.pi.regs[i] = s_pi[i];
    for (i = 0; i < RI_REGS_COUNT; ++i)
        g_dev.ri.regs[i] = s_ri[i];
    for (i = 0; i < 4; ++i)
        g_dev.sp.regs[i] = s_sp[i];
    g_dev.si.regs[SI_DRAM_ADDR_REG] = s_si[0];
    g_dev.si.regs[SI_PIF_ADDR_RD64B_REG] = s_si[1];
    g_dev.si.regs[SI_PIF_ADDR_WR64B_REG] = s_si[2];
    g_dev.mi.regs[MI_INTR_MASK_REG] = MI_INTR_SP | MI_INTR_SI | MI_INTR_AI
                                    | MI_INTR_VI | MI_INTR_PI | MI_INTR_DP;
    r4300_write_aligned_word(r4300, R4300_KSEG1 + MM_AI_REGS + 4*AI_DACRATE_REG, 0x5f0, ~UINT32_C(0));
    r4300_write_aligned_word(r4300, R4300_KSEG1 + MM_AI_REGS + 4*AI_BITRATE_REG, 0xf, ~UINT32_C(0));
    g_dev.ai.regs[AI_CONTROL_REG] = 1;

    /* RDRAM timing as the boot code's memory sizing programs every 2 MB
     * module, and the all-ones words its probe leaves at the start of each
     * module past the first */
    for (i = 0; i < RDRAM_MAX_MODULES_COUNT && i * 0x200000u < dram_size; ++i)
    {
        if (i != 0)
        {
            rdram_write32(dram, dram_size, i * 0x200000u, UINT32_C(0xffffffff));
            rdram_write32(dram, dram_size, i * 0x200000u + 4, UINT32_C(0xffffffff));
        }
        g_dev.rdram.regs[i][RDRAM_DEVICE_ID_REG] = (uint32_t)i << 27;
        g_dev.rdram.regs[i][RDRAM_DELAY_REG] = UINT32_C(0x18082838);
        g_dev.rdram.regs[i][RDRAM_MODE_REG] = UINT32_C(0xc6c0c080);
        g_dev.rdram.regs[i][RDRAM_RAS_INTERVAL_REG] = UINT32_C(0x080c1204);
    }

    /* the drive: reset acknowledged, still locked on the track of the last
     * boot block, its buffer manager past that block's last sector */
    for (i = 0; i < DD_ASIC_REGS_COUNT; ++i)
        dd->regs[i] = s_dd[i];
    {
        uint16_t phys = LBAToPhys(sys, DD_BOOT_LBA + blocks - 1);
        uint32_t track = (uint32_t)(phys & 0x1fff) << 16;   /* head, track */
        unsigned int block = (phys >> 13) & 1;
        unsigned int zone = get_zone_from_head_track((phys >> 12) & 1, phys & 0xfff);
        dd->regs[DD_ASIC_DATA] = track;
        dd->regs[DD_ASIC_CUR_TK] = UINT32_C(0x60000000) | track;
        dd->regs[DD_ASIC_CUR_SECTOR] = (uint32_t)(block * 90 + 88) << 16;
        dd->regs[DD_ASIC_HOST_SECBYTE] = zone_sec_size_phys[zone] - 1;
    }
    if (dd->disk->development)
        dd->regs[DD_ASIC_ID_REG] = UINT32_C(0x00040000);

    /* timing: Count as it reads on the boot code's first instruction, and
     * the next VI. The CPU starts there directly, so no register is spent
     * on getting there. */
    translate_event_queue(&r4300->cp0, DD_HANDOVER_COUNT);
    remove_event(&r4300->cp0.q, VI_INT);
    add_interrupt_event_count(&r4300->cp0, VI_INT,
                              DD_HANDOVER_COUNT + DD_HANDOVER_VI_IN);
    remove_event(&r4300->cp0.q, DD_DV_INT);
    add_interrupt_event_count(&r4300->cp0, DD_DV_INT,
                              DD_HANDOVER_COUNT + DD_HANDOVER_DV_IN);

    r4300->start_address = entry;
    *r4300_cp0_last_addr(&r4300->cp0) = entry;
    return 1;
}
