/*
 * mupen64plus-rsp-cxd4 - RSP Interpreter
 * Copyright (C) 2012-2013  RJ 'Iconoclast' Swedlow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

void SP_DMA_WRITE(void)
{
    register unsigned int length;
    register unsigned int count;
    register unsigned int skip;

    length = (*RSP.SP_WR_LEN_REG & 0x00000FFF) >>  0;
    count  = (*RSP.SP_WR_LEN_REG & 0x000FF000) >> 12;
    skip   = (*RSP.SP_WR_LEN_REG & 0xFFF00000) >> 20;
    /* length |= 07; // already corrected by mtc0 */
    ++length;
    ++count;
    skip += length;
    do
    { /* `count` always starts > 0, so we begin with `do` instead of `while`. */
        unsigned int offC, offD; /* SP cache and dynamic DMA pointers */
        register unsigned int i = 0;

        --count;
        do
        {
            offC = (count*length + *RSP.SP_MEM_ADDR_REG + i) & 0x00001FF8;
            offD = (count*skip + *RSP.SP_DRAM_ADDR_REG + i) & 0x00FFFFF8;
            memcpy(RSP.RDRAM + offD, RSP.DMEM + offC, 8);
            i += 0x000008;
        } while (i < length);
    } while (count);
    *RSP.SP_DMA_BUSY_REG = 0x00000000;
    *RSP.SP_STATUS_REG &= ~0x00000004; /* SP_STATUS_DMABUSY */
}

void SP_DMA_READ(void)
{
    register unsigned int length;
    register unsigned int count;
    register unsigned int skip;

    length = (*RSP.SP_RD_LEN_REG & 0x00000FFF) >>  0;
    count  = (*RSP.SP_RD_LEN_REG & 0x000FF000) >> 12;
    skip   = (*RSP.SP_RD_LEN_REG & 0xFFF00000) >> 20;
    /* length |= 07; // already corrected by mtc0 */
    ++length;
    ++count;
    skip += length;
    do
    { /* `count` always starts > 0, so we begin with `do` instead of `while`. */
        unsigned int offC, offD; /* SP cache and dynamic DMA pointers */
        register unsigned int i = 0;

        --count;
        do
        {
            offC = (count*length + *RSP.SP_MEM_ADDR_REG + i) & 0x00001FF8;
            offD = (count*skip + *RSP.SP_DRAM_ADDR_REG + i) & 0x00FFFFF8;
            memcpy(RSP.DMEM + offC, RSP.RDRAM + offD, 8);
            i += 0x008;
        } while (i < length);
    } while (count);
    *RSP.SP_DMA_BUSY_REG = 0x00000000;
    *RSP.SP_STATUS_REG &= ~0x00000004; /* SP_STATUS_DMABUSY */
}

void MTC0(int rt, int rd)
{
    switch (rd)
    {
        case 0x0:
            *RSP.SP_MEM_ADDR_REG = SR[rt] & 0xFFFFFFF8;
            return; /* Reserved upper bits are filtered out on DMA R/W. */
        case 0x1: /* 24-bit RDRAM pointer */
            *RSP.SP_DRAM_ADDR_REG = SR[rt] & 0xFFFFFFF8;
            return; /* Again, we don't *yet* care about the reserved bits. */
        case 0x2:
            *RSP.SP_RD_LEN_REG = SR[rt] | 07;
            SP_DMA_READ();
            return;
        case 0x3:
            *RSP.SP_WR_LEN_REG = SR[rt] | 07;
            SP_DMA_WRITE();
            return;
        case 0x4:
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000001) <<  0);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000002) <<  0);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000004) <<  1);
            *RSP.MI_INTR_REG &= ~((SR[rt] & 0x00000008) >> 3); /* SP_CLR_INTR */
            *RSP.MI_INTR_REG |=  ((SR[rt] & 0x00000010) >> 4); /* SP_SET_INTR */
            *RSP.SP_STATUS_REG |= (SR[rt] & 0x00000010) >> 4; /* int set halt */
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000020) <<  5);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000040) <<  5);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000080) <<  6);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000100) <<  6);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000200) <<  7);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000400) <<  7);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000800) <<  8);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00001000) <<  8);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00002000) <<  9);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00004000) <<  9);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00008000) << 10);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00010000) << 10);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00020000) << 11);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00040000) << 11);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00080000) << 12);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00100000) << 12);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00200000) << 13);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00400000) << 13);
            *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00800000) << 14);
            *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x01000000) << 14);
            return;
        case 0x5: /* read-only register, cannot directly write using MTC0 */
            message("MTC0\nDMA_FULL", 2);
            return;
        case 0x6: /* read-only register, cannot directly write using MTC0 */
            message("MTC0\nDMA_BUSY", 2);
            return;
        case 0x7:
            *RSP.SP_SEMAPHORE_REG = 0x00000000; /* Forced (zilmar + dox). */
            return;
        case 0x8:
            if (*RSP.DPC_BUFBUSY_REG) /* lock hazards not implemented */
                message("MTC0\nCMD_START", 3);
            *RSP.DPC_START_REG   = SR[rt] & ~07; /* Funnelcube demo--marshall */
            *RSP.DPC_CURRENT_REG = *RSP.DPC_START_REG;
            *RSP.DPC_END_REG     = *RSP.DPC_START_REG;
            return;
        case 0x9:
            if (*RSP.DPC_BUFBUSY_REG)
            {
                message("MTC0\nCMD_END", 3);
                return; /* lock hazards not implemented */
            }
            *RSP.DPC_END_REG = SR[rt] & 0xFFFFFFF8;
            if (RSP.ProcessRdpList == NULL) return; /* zilmar GFX #1.2 */
            RSP.ProcessRdpList();
            return;
        case 0xA: /* read-only register, cannot directly write using MTC0 */
            message("MTC0\nCMD_CURRENT", 2);
            return;
        case 0xB:
            if (SR[rt] & 0xFFFFFD80) /* unsupported or reserved bits */
                message("MTC0\nCMD_STATUS", 2);
            *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000001) << 0);
            *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000002) << 0);
            *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000004) << 1);
            *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000008) << 1);
            *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000010) << 2);
            *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000020) << 2);
/* Some NUS-CIC-6105 SP tasks try to clear some zeroed DPC registers. */
            *RSP.DPC_TMEM_REG     &= -((SR[rt] & 0x00000040) == 0x00000000);
         /* *RSP.DPC_PIPEBUSY_REG &= -((SR[rt] & 0x00000080) == 0x00000000); */
         /* *RSP.DPC_BUFBUSY_REG  &= -((SR[rt] & 0x00000100) == 0x00000000); */
            *RSP.DPC_CLOCK_REG    &= -((SR[rt] & 0x00000200) == 0x00000000);
            return;
        case 0xC: /* ??? is this read-only or not, hard to tell */
            message("MTC0\nCMD_CLOCK", 2);
            *RSP.DPC_CLOCK_REG = SR[rt];
            return; /* Doc appendix says this is RW; elsewhere it says R. */
        case 0xD: /* read-only register, cannot directly write using MTC0 */
            message("MTC0\nCMD_BUSY", 2);
            return;
        case 0xE: /* read-only register, cannot directly write using MTC0 */
            message("MTC0\nCMD_PIPE_BUSY", 2);
            return;
        case 0xF: /* read-only register, cannot directly write using MTC0 */
            message("MTC0\nCMD_TMEM_BUSY", 2);
            return;
    }
}
