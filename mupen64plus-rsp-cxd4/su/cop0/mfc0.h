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

void MFC0(int rt, int rd)
{
/*
    if (rt == 0)
        return; // zero permanence already handled in main CPU loop
*/
    switch (rd)
    {
        case 0x0:
            SR[rt] = *RSP.SP_MEM_ADDR_REG;
            return;
        case 0x1:
            SR[rt] = *RSP.SP_DRAM_ADDR_REG;
            return;
        case 0x2: /* have not verified / been able to test yet ? */
            message("MFC0\nDMA_READ_LENGTH", 2);
            SR[rt] = *RSP.SP_RD_LEN_REG;
            return;
        case 0x3:
            message("MFC0\nDMA_WRITE_LENGTH", 3);
            return; /* dunno what to do, so error */
        case 0x4:
            SR[rt] = *RSP.SP_STATUS_REG;
#ifdef WAIT_FOR_CPU_HOST
            ++MFC0_count[rt];
            if (MFC0_count[rt] > 07)
                *RSP.SP_STATUS_REG |= 0x00000001; /* Let OS restart the task. */
#endif
            return;
        case 0x5: /* if (*RSP.SP_DMA_FULL_REG != 0x00000000), check the flag? */
            SR[rt] = *RSP.SP_DMA_FULL_REG;
            return;
        case 0x6: /* if (*RSP.SP_DMA_BUSY_REG != 0x00000000), check the flag? */
            SR[rt] = *RSP.SP_DMA_FULL_REG;
            return;
        case 0x7:
            SR[rt] = *RSP.SP_SEMAPHORE_REG;
#ifdef SEMAPHORE_LOCK_CORRECTIONS
            *RSP.SP_SEMAPHORE_REG = 0x00000001;
            *RSP.SP_STATUS_REG |= 0x00000001; /* temporary bit to break CPU */
            return; /* Break the SP task (zilmar). */
#else
            return; /* old method, inaccurate */
#endif
        case 0x8:
            SR[rt] = *RSP.DPC_START_REG;
            return;
        case 0x9:
            SR[rt] = *RSP.DPC_END_REG;
            return;
        case 0xA:
            SR[rt] = *RSP.DPC_CURRENT_REG;
            return;
        case 0xB:
            if (*RSP.DPC_STATUS_REG & 0x00000600) /* end/start valid ? */
            { /* CP0 register locking/unlocking not tested yet. */
                message("MFC0\nCMD_STATUS", 2);
                *RSP.DPC_STATUS_REG &= ~0x00000600;
            }
            SR[rt] = *RSP.DPC_STATUS_REG;
            return;
        case 0xC:
            SR[rt] = *RSP.DPC_CLOCK_REG;
            return;
        case 0xD:
            SR[rt] = *RSP.DPC_BUFBUSY_REG;
            return;
        case 0xE:
            SR[rt] = *RSP.DPC_PIPEBUSY_REG;
            return;
        case 0xF:
            SR[rt] = *RSP.DPC_TMEM_REG;
            return;
    }
}
