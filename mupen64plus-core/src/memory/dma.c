/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dma.c                                                   *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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
#include <stdio.h>

#include "api/m64p_types.h"

#include "dma.h"
#include "memory.h"
#include "pif.h"
#include "flashram.h"

#include "r4300/r4300.h"
#include "r4300/interupt.h"
#include "r4300/macros.h"
#include "r4300/ops.h"
#include "../r4300/new_dynarec/new_dynarec.h"

#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_config.h"
#include "api/config.h"
#include "api/callbacks.h"
#include "main/main.h"
#include "main/rom.h"
#include "main/util.h"

int32_t delay_si = 0;

void dma_pi_read(void)
{
    uint32_t i;

    if (pi_register.pi_cart_addr_reg >= 0x08000000
            && pi_register.pi_cart_addr_reg < 0x08010000)
    {
        if (flashram_info.use_flashram != 1)
        {
            for (i=0; i < (pi_register.pi_rd_len_reg & 0xFFFFFF)+1; i++)
            {
                saved_memory.sram[((pi_register.pi_cart_addr_reg-0x08000000)+i)^S8] =
                    ((uint8_t*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8];
            }

            flashram_info.use_flashram = -1;
        }
        else
        {
            dma_write_flashram();
        }
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "Unknown dma read in dma_pi_read()");
    }

    pi_register.read_pi_status_reg |= 1;
    update_count();
    add_interupt_event(PI_INT, 0x1000/*pi_register.pi_rd_len_reg*/);
}

void dma_pi_write(void)
{
    uint32_t longueur;
    int32_t i;

    if (pi_register.pi_cart_addr_reg < 0x10000000)
    {
        if (pi_register.pi_cart_addr_reg >= 0x08000000
                && pi_register.pi_cart_addr_reg < 0x08010000)
        {
            if (flashram_info.use_flashram != 1)
            {
                int32_t i;

                for (i=0; i< (int32_t)(pi_register.pi_wr_len_reg & 0xFFFFFF)+1; i++)
                {
                    ((uint8_t*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
                        saved_memory.sram[(((pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF)+i)^S8];
                }

                flashram_info.use_flashram = -1;
            }
            else
            {
                dma_read_flashram();
            }
        }
        else if (pi_register.pi_cart_addr_reg >= 0x06000000
                 && pi_register.pi_cart_addr_reg < 0x08000000)
        {
        }
        else
        {
            DebugMessage(M64MSG_WARNING, "Unknown dma write 0x%x in dma_pi_write()", (int32_t)pi_register.pi_cart_addr_reg);
        }

        pi_register.read_pi_status_reg |= 1;
        update_count();
        add_interupt_event(PI_INT, /*pi_register.pi_wr_len_reg*/0x1000);

        return;
    }

    if (pi_register.pi_cart_addr_reg >= 0x1fc00000) // for paper mario
    {
        pi_register.read_pi_status_reg |= 1;
        update_count();
        add_interupt_event(PI_INT, 0x1000);

        return;
    }

    longueur = (pi_register.pi_wr_len_reg & 0xFFFFFF)+1;
    i = (pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF;
    longueur = (i + (int32_t)longueur) > rom_size ?
               (rom_size - i) : longueur;
    longueur = (pi_register.pi_dram_addr_reg + longueur) > 0x7FFFFF ?
               (0x7FFFFF - pi_register.pi_dram_addr_reg) : longueur;

    if (i>rom_size || pi_register.pi_dram_addr_reg > 0x7FFFFF)
    {
        pi_register.read_pi_status_reg |= 3;
        update_count();
        add_interupt_event(PI_INT, longueur/8);

        return;
    }

    if (r4300emu != CORE_PURE_INTERPRETER)
    {
        for (i=0; i<(int32_t)longueur; i++)
        {
            uint32_t rdram_address1 = pi_register.pi_dram_addr_reg+i+0x80000000;
            uint32_t rdram_address2 = pi_register.pi_dram_addr_reg+i+0xa0000000;
            ((uint8_t*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
                rom[(((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)+i)^S8];

            if (!invalid_code[rdram_address1>>12])
            {
                if (!blocks[rdram_address1>>12] ||
                    blocks[rdram_address1>>12]->block[(rdram_address1&0xFFF)/4].ops !=
                    current_instruction_table.NOTCOMPILED)
                {
                    invalid_code[rdram_address1>>12] = 1;
                }
#ifdef NEW_DYNAREC
                invalidate_block(rdram_address1>>12);
#endif
            }
            if (!invalid_code[rdram_address2>>12])
            {
                if (!blocks[rdram_address1>>12] ||
                    blocks[rdram_address2>>12]->block[(rdram_address2&0xFFF)/4].ops !=
                    current_instruction_table.NOTCOMPILED)
                {
                    invalid_code[rdram_address2>>12] = 1;
                }
            }
        }
    }
    else
    {
        for (i=0; i<(int32_t)longueur; i++)
        {
            ((uint8_t*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
                rom[(((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)+i)^S8];
        }
    }

    // Set the RDRAM memory size when copying main ROM code
    // (This is just a convenient way to run this code once at the beginning)
    if (pi_register.pi_cart_addr_reg == 0x10001000)
    {
        switch (CIC_Chip)
        {
        case 1:
        case 2:
        case 3:
        case 6:
        {
            if (ConfigGetParamInt(g_CoreConfig, "DisableExtraMem"))
            {
                rdram[0x318/4] = 0x400000;
            }
            else
            {
                rdram[0x318/4] = 0x800000;
            }
            break;
        }
        case 5:
        {
            if (ConfigGetParamInt(g_CoreConfig, "DisableExtraMem"))
            {
                rdram[0x3F0/4] = 0x400000;
            }
            else
            {
                rdram[0x3F0/4] = 0x800000;
            }
            break;
        }
        }
    }

    pi_register.read_pi_status_reg |= 3;
    update_count();
    add_interupt_event(PI_INT, longueur/8);

    return;
}

void dma_sp_write(void)
{
    uint32_t i,j;

    uint32_t l = sp_register.sp_rd_len_reg;

    uint32_t length = ((l & 0xfff) | 7) + 1;
    uint32_t count = ((l >> 12) & 0xff) + 1;
    uint32_t skip = ((l >> 20) & 0xfff);
 
    uint32_t memaddr = sp_register.sp_mem_addr_reg & 0xfff;
    uint32_t dramaddr = sp_register.sp_dram_addr_reg & 0xffffff;

    uint8_t *spmem = ((sp_register.sp_mem_addr_reg & 0x1000) != 0) ? (uint8_t*)SP_IMEM : (uint8_t*)SP_DMEM;
    uint8_t *dram = (uint8_t*)rdram;

    for(j=0; j<count; j++) {
        for(i=0; i<length; i++) {
            spmem[memaddr^S8] = dram[dramaddr^S8];
            memaddr++;
            dramaddr++;
        }
        dramaddr+=skip;
    }
}

void dma_sp_read(void)
{
    uint32_t i,j;

    uint32_t l = sp_register.sp_wr_len_reg;

    uint32_t length = ((l & 0xfff) | 7) + 1;
    uint32_t count = ((l >> 12) & 0xff) + 1;
    uint32_t skip = ((l >> 20) & 0xfff);

    uint32_t memaddr = sp_register.sp_mem_addr_reg & 0xfff;
    uint32_t dramaddr = sp_register.sp_dram_addr_reg & 0xffffff;

    uint8_t *spmem = ((sp_register.sp_mem_addr_reg & 0x1000) != 0) ? (uint8_t*)SP_IMEM : (uint8_t*)SP_DMEM;
    uint8_t *dram = (uint8_t*)rdram;

    for(j=0; j<count; j++) {
        for(i=0; i<length; i++) {
            dram[dramaddr^S8] = spmem[memaddr^S8];
            memaddr++;
            dramaddr++;
        }
        dramaddr+=skip;
    }
}

void dma_si_write(void)
{
    int32_t i;

    if (si_register.si_pif_addr_wr64b != 0x1FC007C0)
    {
        DebugMessage(M64MSG_ERROR, "dma_si_write(): unknown SI use");
        stop=1;
    }

    for (i=0; i<(64/4); i++)
    {
        PIF_RAM[i] = sl(rdram[si_register.si_dram_addr/4+i]);
    }

    update_pif_write();

    // TODO: under what circumstances should bits 1 or 3 be set?
    //si_register.si_stat |= 1;

    update_count();

    if (delay_si)
       add_interupt_event(SI_INT, /*0x100*/0x900);
    else
    {
       MI_register.mi_intr_reg |= 0x02; // SI
       si_register.si_stat |= 0x1000; // INTERRUPT
       check_interupt();
    }
}

void dma_si_read(void)
{
    int32_t i;

    if (si_register.si_pif_addr_rd64b != 0x1FC007C0)
    {
        DebugMessage(M64MSG_ERROR, "dma_si_read(): unknown SI use");
        stop=1;
    }

    update_pif_read();

    for (i=0; i<(64/4); i++)
    {
        rdram[si_register.si_dram_addr/4+i] = sl(PIF_RAM[i]);
    }

    // TODO: under what circumstances should bits 1 or 3 be set?
    //si_register.si_stat |= 1;

    update_count();

    if (delay_si)
       add_interupt_event(SI_INT, /*0x100*/0x900);
    else
    {
       MI_register.mi_intr_reg |= 0x02; // SI
       si_register.si_stat |= 0x1000; // INTERRUPT
       check_interupt();
    }
}

