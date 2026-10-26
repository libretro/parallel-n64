/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - pi_controller.c                                         *
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

#include "pi_controller.h"

#define M64P_CORE_PROTOTYPES 1
#include <stdint.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "device/device.h"
#include "device/dd/dd_controller.h"
#include "device/memory/m64p_memory.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

int validate_pi_request(struct pi_controller* pi)
{
    if (pi->regs[PI_STATUS_REG] & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY)) {
        pi->regs[PI_STATUS_REG] |= PI_STATUS_ERROR;
        return 0;
    }

    return 1;
}

/* Bytes of a PI DMA that fall inside RDRAM.  Addresses past the end of
 * RDRAM are not backed by memory, so that part of the transfer moves no
 * data. */
static uint32_t pi_dram_span(const struct pi_controller* pi,
                             uint32_t dram_addr, uint32_t length)
{
    size_t dram_size = pi->ri->rdram->dram_size;

    if (dram_addr >= dram_size)
        return 0;
    if (length > dram_size - dram_addr)
        return (uint32_t)(dram_size - dram_addr);
    return length;
}

int g_dma_bus_timing = 0;

unsigned int pi_dma_duration(const uint32_t* regs, uint32_t cart_addr, uint32_t length)
{
    unsigned int domain = cart_addr >> 24;
    unsigned int base = (domain == 5 || (domain >= 8 && domain <= 15))
        ? PI_BSD_DOM2_LAT_REG : PI_BSD_DOM1_LAT_REG;
    uint64_t page_size = UINT64_C(1) << ((regs[base + 2] & 15) + 2);
    uint64_t first = cart_addr & ~UINT32_C(1);
    uint64_t bytes = (length + 1u) & ~1u;
    uint64_t last, pages, buffers = 0, partial = 0, clocks;

    if (bytes == 0)
        return 0;
    last  = first + bytes - 2;
    pages = last / page_size - first / page_size + 1;

    if (pages == 1) {
        if (bytes == 128)
            buffers = 1;
        else
            partial = bytes;
    }
    else {
        if (!(first & (page_size - 1)))
            ++buffers;
        else
            partial += page_size - (first & (page_size - 1));
        if (!((last + 2) & (page_size - 1)))
            ++buffers;
        else
            partial += (last & (page_size - 1)) + 2;
        if (pages > 2)
            buffers += (pages - 2) * page_size / 128;
    }

    /* RCP clocks, then count cycles at three to every four */
    clocks = (15 + (regs[base] & 255)) * pages
           + ((regs[base + 1] & 255) + (regs[base + 3] & 3) + 2) * bytes / 2
           + buffers * 28 + partial;
    return (unsigned int)((clocks * 3 + 3) / 4);
}

static void dma_pi_read(struct pi_controller* pi)
{
    uint32_t cart_addr;
    uint32_t dram_addr;
    uint32_t length;
    uint32_t span;
    unsigned int cycles;
    const uint8_t* dram;
    const struct pi_dma_handler* handler = NULL;
    void* opaque = NULL;

    if (!validate_pi_request(pi))
        return;

    cart_addr = pi->regs[PI_CART_ADDR_REG] & ~UINT32_C(1);
    dram_addr = pi->regs[PI_DRAM_ADDR_REG] & 0xfffffe;
    length    = (pi->regs[PI_RD_LEN_REG] & UINT32_C(0x00ffffff)) + 1;
    dram      = (const uint8_t*)pi->ri->rdram->dram;

    pi->get_pi_dma_handler(pi->cart, pi->dd, cart_addr, &opaque, &handler);

    if (handler == NULL) {
        DebugMessage(M64MSG_WARNING, "Unknown PI DMA read: 0x%" PRIX32 " -> 0x%" PRIX32 " (0x%" PRIX32 ")", dram_addr, cart_addr, length);
        return;
    }

    pre_framebuffer_read(&pi->dp->fb, dram_addr);

    /* PI seems to treat the first 128 bytes differently, see https://n64brew.dev/wiki/Peripheral_Interface#Unaligned_DMA_transfer */
    if (length >= 0x7f && (length & 1))
        length += 1;

    span   = pi_dram_span(pi, dram_addr, length);
    cycles = handler->dma_read(opaque, dram, dram_addr, cart_addr, span);
    if (g_dma_bus_timing)
        cycles = pi_dma_duration(pi->regs, cart_addr, length);

    /* Mark DMA as busy */
    pi->regs[PI_STATUS_REG] |= PI_STATUS_DMA_BUSY;
    /* Update PI_DRAM_ADDR_REG and PI_CART_ADDR_REG */
    pi->regs[PI_DRAM_ADDR_REG] = (pi->regs[PI_DRAM_ADDR_REG] + length + 7) & ~7;
    pi->regs[PI_CART_ADDR_REG] = (pi->regs[PI_CART_ADDR_REG] + length + 1) & ~1;

    /* schedule end of dma interrupt event */
    cp0_update_count(pi->mi->r4300);
    add_interrupt_event(&pi->mi->r4300->cp0, PI_INT, cycles);
}

static void dma_pi_write(struct pi_controller* pi)
{
    uint32_t cart_addr;
    uint32_t dram_addr;
    uint32_t length;
    uint32_t span;
    unsigned int cycles;
    uint8_t* dram;
    const struct pi_dma_handler* handler = NULL;
    void* opaque = NULL;

    if (!validate_pi_request(pi))
        return;

    cart_addr = pi->regs[PI_CART_ADDR_REG] & ~UINT32_C(1);
    dram_addr = pi->regs[PI_DRAM_ADDR_REG] & 0xfffffe;
    length    = (pi->regs[PI_WR_LEN_REG] & UINT32_C(0x00ffffff)) + 1;
    dram      = (uint8_t*)pi->ri->rdram->dram;

    pi->get_pi_dma_handler(pi->cart, pi->dd, cart_addr, &opaque, &handler);

    if (handler == NULL) {
        DebugMessage(M64MSG_WARNING, "Unknown PI DMA write: 0x%" PRIX32 " -> 0x%" PRIX32 " (0x%" PRIX32 ")", cart_addr, dram_addr, length);
        return;
    }

    /* PI seems to treat the first 128 bytes differently, see https://n64brew.dev/wiki/Peripheral_Interface#Unaligned_DMA_transfer
     * A first block shorter than the DRAM misalignment transfers nothing. */
    if (length >= 0x7f && (length & 1))
        length += 1;
    if (length <= 0x80)
    {
        uint32_t misalign = dram_addr & 0x7;
        length = (length > misalign) ? length - misalign : 0;
    }

    span   = pi_dram_span(pi, dram_addr, length);
    cycles = handler->dma_write(opaque, dram, dram_addr, cart_addr, span);
    if (g_dma_bus_timing)
        cycles = pi_dma_duration(pi->regs, cart_addr, length);

    post_framebuffer_write(&pi->dp->fb, dram_addr, span);

    /* whatever the source (ROM, SRAM, FlashRAM, 64DD), the bytes written
     * may replace code the recompiler has already translated */
    if (span != 0) {
        invalidate_r4300_cached_code(pi->mi->r4300, R4300_KSEG0 + dram_addr, span);
        invalidate_r4300_cached_code(pi->mi->r4300, R4300_KSEG1 + dram_addr, span);
    }

    /* Mark DMA as busy */
    pi->regs[PI_STATUS_REG] |= PI_STATUS_DMA_BUSY;
    /* Update PI_DRAM_ADDR_REG and PI_CART_ADDR_REG */
    pi->regs[PI_DRAM_ADDR_REG] = (pi->regs[PI_DRAM_ADDR_REG] + length + 7) & ~7;
    pi->regs[PI_CART_ADDR_REG] = (pi->regs[PI_CART_ADDR_REG] + length + 1) & ~1;

    /* schedule end of dma interrupt event */
    cp0_update_count(pi->mi->r4300);
    add_interrupt_event(&pi->mi->r4300->cp0, PI_INT, cycles);
}

void init_pi(struct pi_controller* pi,
             pi_dma_handler_getter get_pi_dma_handler,
             struct cart* cart,
             struct dd_controller* dd,
             struct mi_controller* mi,
             struct ri_controller* ri,
             struct rdp_core* dp)
{
    pi->get_pi_dma_handler = get_pi_dma_handler;
    pi->cart = cart;
    pi->dd = dd;
    pi->mi = mi;
    pi->ri = ri;
    pi->dp = dp;
}

void poweron_pi(struct pi_controller* pi)
{
    memset(pi->regs, 0, PI_REGS_COUNT*sizeof(uint32_t));
}

void read_pi_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct pi_controller* pi = (struct pi_controller*)opaque;
    uint32_t reg = pi_reg(address);

    if (reg >= PI_REGS_COUNT) {
        *value = 0;
        return;
    }

    *value = pi->regs[reg];

    if (reg == PI_WR_LEN_REG || reg == PI_RD_LEN_REG)
        *value = 0x7F;
    else if (reg == PI_CART_ADDR_REG)
        *value &= 0xFFFFFFFE;
    else if (reg == PI_DRAM_ADDR_REG)
        *value &= 0xFFFFFE;
}

void write_pi_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct pi_controller* pi = (struct pi_controller*)opaque;
    uint32_t reg = pi_reg(address);

    if (reg >= PI_REGS_COUNT)
        return;

    switch (reg)
    {
    case PI_CART_ADDR_REG:
        if (pi->dd != NULL) {
            masked_write(&pi->regs[PI_CART_ADDR_REG], value, mask);
            return;
        }
        break;

    case PI_RD_LEN_REG:
        masked_write(&pi->regs[PI_RD_LEN_REG], value, mask);
        dma_pi_read(pi);
        return;

    case PI_WR_LEN_REG:
        masked_write(&pi->regs[PI_WR_LEN_REG], value, mask);
        dma_pi_write(pi);
        return;

    case PI_STATUS_REG:
        if (value & mask & PI_STATUS_CLR_INTR)
        {
            pi->regs[reg] &= ~PI_STATUS_INTERRUPT;
            clear_rcp_interrupt(pi->mi, MI_INTR_PI);
        }
        if (value & mask & PI_STATUS_RESET)
        {
            /* the reset abandons a transfer in flight: it never completes.
             * It clears the busy and error bits only; a pending PI
             * interrupt stays pending (and visible) until CLR_INTR. */
            if (pi->regs[PI_STATUS_REG] & PI_STATUS_DMA_BUSY)
                remove_event(&pi->mi->r4300->cp0.q, PI_INT);
            pi->regs[PI_STATUS_REG] &= ~(PI_STATUS_DMA_BUSY | PI_STATUS_ERROR);
        }
        return;

    /* latency and pulse width are 8 bits, page size 4, release 2 */
    case PI_BSD_DOM1_LAT_REG:
    case PI_BSD_DOM1_PWD_REG:
    case PI_BSD_DOM2_LAT_REG:
    case PI_BSD_DOM2_PWD_REG:
        masked_write(&pi->regs[reg], value & 0xff, mask);
        return;

    case PI_BSD_DOM1_PGS_REG:
    case PI_BSD_DOM2_PGS_REG:
        masked_write(&pi->regs[reg], value & 0x0f, mask);
        return;

    case PI_BSD_DOM1_RLS_REG:
    case PI_BSD_DOM2_RLS_REG:
        masked_write(&pi->regs[reg], value & 0x03, mask);
        return;
    }

    masked_write(&pi->regs[reg], value, mask);
}

void pi_end_of_dma_event(void* opaque)
{
    struct pi_controller* pi = (struct pi_controller*)opaque;

    /* Only a DMA interrupts on completion; a CPU write to the bus just
     * stops being busy. */
    if (!(pi->regs[PI_STATUS_REG] & PI_STATUS_DMA_BUSY))
    {
        pi->regs[PI_STATUS_REG] &= ~PI_STATUS_IO_BUSY;
        return;
    }

    pi->regs[PI_STATUS_REG] &= ~(PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY);
    pi->regs[PI_STATUS_REG] |= PI_STATUS_INTERRUPT;

    raise_rcp_interrupt(pi->mi, MI_INTR_PI);
}
