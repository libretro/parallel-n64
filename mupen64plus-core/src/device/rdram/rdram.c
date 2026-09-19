/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rdram.c                                                 *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

#include "rdram.h"

#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "device/device.h"
#include "device/memory/m64p_memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rcp/mi/mi_controller.h"
#include <stdlib.h>

#include <string.h>

extern unsigned int r4300_jit_backend;

#define RDRAM_BCAST_ADDRESS_MASK UINT32_C(0x00080000)


/* XXX: deduce # of RDRAM modules from it's total size
 * Assume only 2Mo RDRAM modules.
 * Proper way of doing it would be to declare in init_rdram
 * what kind of modules we insert and deduce dram_size from
 * that configuration.
 */
static size_t get_modules_count(const struct rdram* rdram)
{
    return (rdram->dram_size) / 0x200000;
}

static uint8_t cc_value(uint32_t mode_reg)
{
    return ((mode_reg & 0x00000040) >>  6)
        |  ((mode_reg & 0x00004000) >> 13)
        |  ((mode_reg & 0x00400000) >> 20)
        |  ((mode_reg & 0x00000080) >>  4)
        |  ((mode_reg & 0x00008000) >> 11)
        |  ((mode_reg & 0x00800000) >> 18);
}


static osal_inline uint16_t idfield_value(uint32_t device_id)
{
    return ((((device_id >> 26) & 0x3f) <<  0)
          | (((device_id >> 23) & 0x01) <<  6)
          | (((device_id >> 16) & 0xff) <<  7)
          | (((device_id >>  7) & 0x01) << 15));
}

static size_t get_module(const struct rdram* rdram, uint32_t address)
{
    size_t module;
    size_t modules = get_modules_count(rdram);
    uint16_t id_field = ri_address_to_id_field(address);


    for (module = 0; module < modules; ++module) {
        if (id_field == idfield_value(rdram->regs[module][RDRAM_DEVICE_ID_REG])) {
            return module;
        }
    }

    /* can happen during memory detection because
     * it probes potentialy non present RDRAM */
    return RDRAM_MAX_MODULES_COUNT;
}

static void read_rdram_dram_corrupted(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);
    size_t module;

    *value = rdram->dram[addr];

    module = get_module(rdram, address);
    if (module == RDRAM_MAX_MODULES_COUNT) {
        *value = 0;
        return;
    }

    /* corrupt read value if CC value is not calibrated */
    uint32_t mode = rdram->regs[module][RDRAM_MODE_REG] ^ UINT32_C(0xc0c0c0c0);
    if ((mode & 0x80000000) && (cc_value(mode) == 0)) {
        *value = 0;
    }
}

static void map_corrupt_rdram(struct rdram* rdram, int corrupt)
{
    struct mem_mapping mapping;

    mapping.begin = MM_RDRAM_DRAM;
    mapping.end = MM_RDRAM_DRAM + rdram->dram_size - 1;
    mapping.type = M64P_MEM_RDRAM;
    mapping.handler.opaque = rdram;
    mapping.handler.read32 = (corrupt)
        ? read_rdram_dram_corrupted
        : read_rdram_dram;
    mapping.handler.write32 = write_rdram_dram;

    apply_mem_mapping(rdram->r4300->mem, &mapping);
    /* The Hacktarux dynarec must drop fast_memory during RDRAM control
     * calibration so its generated loads/stores route through the corrupted
     * handler (which is how IPL3 detects the RAM size); ari64 handles this in
     * its own memory map and must not touch recomp.fast_memory. Dispatch at
     * runtime since both backends are compiled in. */
    if (r4300_jit_backend == R4300_JIT_HACKTARUX)
    {
        rdram->r4300->recomp.fast_memory = (corrupt) ? 0 : 1;
        invalidate_r4300_cached_code(rdram->r4300, 0, 0);
    }
}


void init_rdram(struct rdram* rdram,
                uint32_t* dram,
                size_t dram_size,
                struct r4300_core* r4300)
{
    rdram->dram = dram;
    rdram->dram_size = dram_size;
    rdram->r4300 = r4300;
}

void poweron_rdram(struct rdram* rdram)
{
    size_t module;
    size_t modules = get_modules_count(rdram);
    memset(rdram->regs, 0, RDRAM_MAX_MODULES_COUNT*RDRAM_REGS_COUNT*sizeof(uint32_t));
    memset(rdram->dram, 0, rdram->dram_size);

    DebugMessage(M64MSG_INFO, "Initializing %u RDRAM modules for a total of %u MB",
        (uint32_t) modules, (uint32_t) rdram->dram_size / (1024*1024));

    for (module = 0; module < modules; ++module) {
        rdram->regs[module][RDRAM_CONFIG_REG] = UINT32_C(0xb5190010);
        rdram->regs[module][RDRAM_DEVICE_ID_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_DELAY_REG] = UINT32_C(0x230b0223);
        rdram->regs[module][RDRAM_MODE_REG] = UINT32_C(0xc4c0c0c0);
        rdram->regs[module][RDRAM_REF_ROW_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_MIN_INTERVAL_REG] = UINT32_C(0x0040c0e0);
        rdram->regs[module][RDRAM_ADDR_SELECT_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_DEVICE_MANUF_REG] = UINT32_C(0x00000500);
    }
}


void read_rdram_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t reg = rdram_reg(address);
    size_t module;

    /* rdram_reg() maps the whole 1 KiB window onto an index, so anything
     * past the ten registers a module actually has - 0x03f00200 among them,
     * which is what libdragon samples for entropy - indexed off the end of
     * the array and returned whatever host memory sat there.  That made the
     * value depend on where the process happened to be mapped, so the guest
     * saw a different value from one launch to the next. */
    if (reg >= RDRAM_REGS_COUNT) {
        *value = 0;
        return;
    }

    if (address & RDRAM_BCAST_ADDRESS_MASK) {
        DebugMessage(M64MSG_WARNING, "Reading from broadcast address is unsupported %08x", address);
        return;
    }

    module = get_module(rdram, address);
    if (module == RDRAM_MAX_MODULES_COUNT) {
        *value = 0;
        return;
    }

    *value = rdram->regs[module][reg];

    /* some bits are inverted when read */
    if (reg == RDRAM_MODE_REG) {
        *value ^= UINT32_C(0xc0c0c0c0);
    }
}

void write_rdram_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t reg = rdram_reg(address);
    size_t module;
    size_t modules = get_modules_count(rdram);

    /* Out of range for the same reason as the read side; writing would run
     * off the end of the module's registers. */
    if (reg >= RDRAM_REGS_COUNT) {
        return;
    }

    /* HACK: Detect when current Control calibration is about to start,
     * so we can set corrupted rdram_dram handler
     */
    if (address & RDRAM_BCAST_ADDRESS_MASK && reg == RDRAM_DELAY_REG) {
        map_corrupt_rdram(rdram, 1);
    }

    /* HACK: Detect when current Control calibration is over,
     * so we can restore the original rdram_dram handler
     * and let dynarec have it's fast_memory enabled.
     */
    if (address & RDRAM_BCAST_ADDRESS_MASK && reg == RDRAM_MODE_REG) {
        map_corrupt_rdram(rdram, 0);

        /* HACK: In the IPL3 procedure, at this point,
         * the amount of detected memory can be found in s4 */
        size_t ipl3_rdram_size = r4300_regs(rdram->r4300)[20] & UINT32_C(0x0fffffff);
        if (ipl3_rdram_size != rdram->dram_size) {
            DebugMessage(M64MSG_WARNING, "IPL3 detected %u MB of RDRAM != %u MB",
                (uint32_t) ipl3_rdram_size / (1024*1024), (uint32_t) rdram->dram_size / (1024*1024));
        }
    }


    if (address & RDRAM_BCAST_ADDRESS_MASK) {
        for (module = 0; module < modules; ++module) {
            masked_write(&rdram->regs[module][reg], value, mask);
        }
    }
    else {
        module = get_module(rdram, address);
        if (module != RDRAM_MAX_MODULES_COUNT) {
            masked_write(&rdram->regs[module][reg], value, mask);
        }
    }
}


/* ---------------------------------------------------------------------
 * The ninth bit, and the two MI_MODE modes that reach it.
 *
 * Every RDRAM byte is nine bits wide. The CPU never sees the ninth bit
 * of a byte directly: a CPU write sets it, for each byte written, to the
 * least significant bit of the value being stored, and only the RDP
 * writes it independently (framebuffer coverage, see the video plugin).
 * MI_MODE's EBUS test mode makes an RDRAM read return the four ninth
 * bits of the addressed word in its low nibble, first byte highest.
 *
 * MI_MODE's init mode repeats the next RDRAM write over init_length + 1
 * bytes and then clears itself; libdragon builds its hardware memset on
 * it. A 64-bit store arrives here as two 32-bit writes, so the second
 * half of one that started a repeat is folded into the repeated pattern.
 *
 * The store holds two bits per 16-bit word, the first byte's bit on top -
 * the layout the angrylion renderer keeps its hidden bits in, so that a
 * renderer which has them can share its array (rdram_set_hidden_store).
 * Without one the core keeps its own.
 *
 * Both modes act on "the next RDRAM access", and a recompiler inlines its
 * RDRAM accesses - KSEG1 as well as KSEG0 - so they never arrive here.
 * Under one, the access the mode was meant for goes straight to memory
 * and the next access that does come through a handler is an unrelated
 * one; repeating that over 128 bytes would corrupt memory. The modes are
 * therefore honoured only under the interpreters, where every access is
 * a handler call; under a recompiler MI_MODE keeps its bits and RDRAM
 * behaves as it did before.
 * ------------------------------------------------------------------- */
static uint8_t* g_rdram_hidden;
static size_t   g_rdram_hidden_size;
static uint8_t* g_rdram_hidden_own;

/* the second half of a 64-bit init-mode store still to come */
static uint32_t g_mi_repeat_base, g_mi_repeat_len;
static int      g_mi_repeat_pending;

void rdram_set_hidden_store(uint8_t* store, size_t size)
{
    g_rdram_hidden = store;
    g_rdram_hidden_size = size;
}

static int rdram_mi_modes_honoured(const struct rdram* rdram)
{
    return rdram->r4300->emumode != EMUMODE_DYNAREC;
}

static void rdram_hidden_ensure(const struct rdram* rdram)
{
    if (g_rdram_hidden != NULL)
        return;
    if (g_rdram_hidden_own == NULL)
        g_rdram_hidden_own = (uint8_t*)calloc(rdram->dram_size / 2, 1);
    g_rdram_hidden = g_rdram_hidden_own;
    g_rdram_hidden_size = g_rdram_hidden_own ? rdram->dram_size / 2 : 0;
}

static void rdram_hidden_set(uint32_t byte_addr, unsigned bit)
{
    uint32_t idx = byte_addr >> 1;
    uint8_t m = (byte_addr & 1) ? 1 : 2;
    if (idx < g_rdram_hidden_size)
        g_rdram_hidden[idx] = (uint8_t)((g_rdram_hidden[idx] & ~m) | (bit ? m : 0));
}

static unsigned rdram_hidden_get(uint32_t byte_addr)
{
    uint32_t idx = byte_addr >> 1;
    if (idx >= g_rdram_hidden_size)
        return 0;
    return (g_rdram_hidden[idx] >> ((byte_addr & 1) ? 0 : 1)) & 1;
}

void read_rdram_dram(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);

    if (address < rdram->dram_size
        && (rdram->r4300->mi->regs[MI_INIT_MODE_REG] & 0x100)
        && rdram_mi_modes_honoured(rdram))
    {
        /* EBUS test mode: the word's four ninth bits */
        uint32_t base = address & ~UINT32_C(3);
        rdram_hidden_ensure(rdram);
        *value = (rdram_hidden_get(base) << 3) | (rdram_hidden_get(base + 1) << 2)
               | (rdram_hidden_get(base + 2) << 1) | rdram_hidden_get(base + 3);
    }
    else if (address < rdram->dram_size)
    {
        *value = rdram->dram[addr];
    }
    else
    {
        *value = 0;
    }
}

void write_rdram_dram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);

    if (address < rdram->dram_size)
    {
        uint32_t* init_mode = &rdram->r4300->mi->regs[MI_INIT_MODE_REG];
        uint32_t base = address & ~UINT32_C(3), b;

        masked_write(&rdram->dram[addr], value, mask);

        /* each byte written takes the stored value's low bit as its ninth */
        if (mask != 0)
        {
            unsigned low = 0, lsb;
            while (!((mask >> low) & 1)) low++;
            lsb = (value >> low) & 1;
            rdram_hidden_ensure(rdram);
            for (b = 0; b < 4; b++)
                if ((mask >> ((3 - b) * 8)) & 0xff)
                    rdram_hidden_set(base + b, lsb);
        }

        if (g_mi_repeat_pending && mask == ~UINT32_C(0)
            && base == g_mi_repeat_base + 4)
        {
            /* low half of the 64-bit store that started the repeat */
            uint32_t a;
            for (a = base; a < g_mi_repeat_base + g_mi_repeat_len && a < rdram->dram_size; a += 8)
            {
                rdram->dram[a >> 2] = value;
                for (b = 0; b < 4; b++)
                    rdram_hidden_set(a + b, value & 1);
            }
        }
        g_mi_repeat_pending = 0;

        if ((*init_mode & 0x80) && mask == ~UINT32_C(0) && rdram_mi_modes_honoured(rdram))
        {
            uint32_t len = (*init_mode & 0x7f) + 1, a;
            for (a = base; a < base + len && a < rdram->dram_size; a++)
            {
                /* the stored word repeats along the bytes that follow */
                uint32_t sh = (3 - ((a - base) & 3)) * 8;
                uint32_t* w = &rdram->dram[a >> 2];
                uint32_t wsh = (3 - (a & 3)) * 8;
                *w = (*w & ~(UINT32_C(0xff) << wsh)) | (((value >> sh) & 0xff) << wsh);
                rdram_hidden_set(a, value & 1);
            }
            *init_mode &= ~UINT32_C(0x80);
            g_mi_repeat_base = base;
            g_mi_repeat_len = len;
            g_mi_repeat_pending = 1;
        }
    }
}
