/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*   Mupen64plus - dd_disk.c                                               *
*   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
*   Copyright (C) 2015 LuigiBlood                                         *
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

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dd_controller.h"
#include "dd_disk.h"
#include "pi/pi_controller.h"

#include "api/callbacks.h"
#include "api/config.h"
#include "api/m64p_config.h"
#include "api/m64p_types.h"
#include "main/main.h"
#include "main/rom.h"
#include "main/util.h"
#include "r4300/cp0.h"
#include "r4300/cp0_private.h"
#include "r4300/r4300_core.h"

/* Global loaded disk memory space. */
unsigned char* g_dd_disk = NULL;
/* Global loaded disk size. */
int g_dd_disk_size = 0;

int dd_bm_mode_read;	//BM MODE 0 = WRITE, MODE 1 = READ
int CUR_BLOCK;			//Current Block
int dd_sector55;
int dd_zone;			//Current Zone
int dd_track_offset;	//Offset to Track

const unsigned int ddZoneSecSize[16] = { 232, 216, 208, 192, 176, 160, 144, 128,
                                       216, 208, 192, 176, 160, 144, 128, 112 };

const unsigned int ddZoneTrackSize[16] = { 158, 158, 149, 149, 149, 149, 149, 114,
                                         158, 158, 149, 149, 149, 149, 149, 114 };

const unsigned int ddStartOffset[16] =
            { 0x0, 0x5F15E0, 0xB79D00, 0x10801A0, 0x1523720, 0x1963D80, 0x1D414C0, 0x20BBCE0,
            0x23196E0, 0x28A1E00, 0x2DF5DC0, 0x3299340, 0x36D99A0, 0x3AB70E0, 0x3E31900, 0x4149200 };

void connect_dd_disk(struct dd_disk* dd_disk,
	                 uint8_t* disk, size_t disk_size)
{
	dd_disk->disk = disk;
	dd_disk->disk_size = disk_size;
}

m64p_error open_dd_disk(const unsigned char* diskimage, unsigned int size)
{
	/* allocate new buffer for ROM and copy into this buffer */
	g_dd_disk_size = size;
	g_dd_disk = (unsigned char *)malloc(size);
	if (g_dd_disk == NULL)
		return M64ERR_NO_MEMORY;
	memcpy(g_dd_disk, diskimage, size);

	DebugMessage(M64MSG_STATUS, "64DD Disk loaded!");

	return M64ERR_SUCCESS;
}

m64p_error close_dd_disk(void)
{
	if (g_dd_disk == NULL)
		return M64ERR_INVALID_STATE;

	free(g_dd_disk);
	g_dd_disk = NULL;

	DebugMessage(M64MSG_STATUS, "64DD Disk closed.");

	return M64ERR_SUCCESS;
}

void dd_set_zone_and_track_offset(void *opaque)
{
	int tr_off;
	struct dd_controller *dd = (struct dd_controller *) opaque;

	int head = ((dd->regs[ASIC_CUR_TK] & 0x10000000U) >> 25);	//Head * 8
	int track = ((dd->regs[ASIC_CUR_TK] & 0x0FFF0000U) >> 16);

	if (track >= 0x425)
	{
		dd_zone = 7 + head;
		tr_off = track - 0x425;
	}
	else if (track >= 0x390)
	{
		dd_zone = 6 + head;
		tr_off = track - 0x390;
	}
	else if (track >= 0x2FB)
	{
		dd_zone = 5 + head;
		tr_off = track - 0x2FB;
	}
	else if (track >= 0x266)
	{
		dd_zone = 4 + head;
		tr_off = track - 0x266;
	}
	else if (track >= 0x1D1)
	{
		dd_zone = 3 + head;
		tr_off = track - 0x1D1;
	}
	else if (track >= 0x13C)
	{
		dd_zone = 2 + head;
		tr_off = track - 0x13C;
	}
	else if (track >= 0x9E)
	{
		dd_zone = 1 + head;
		tr_off = track - 0x9E;
	}
	else
	{
		dd_zone = 0 + head;
		tr_off = track;
	}

	dd_track_offset = ddStartOffset[dd_zone] + tr_off*ddZoneSecSize[dd_zone] * SECTORS_PER_BLOCK*BLOCKS_PER_TRACK;
}

void dd_update_bm(void *opaque)
{
	struct dd_controller *dd = (struct dd_controller *) opaque;
	//printf("DD_UPDATE_BM\n");
	if ((dd->regs[ASIC_BM_STATUS_CTL] & 0x80000000) == 0)
		return;
	else
	{
		int Cur_Sector = dd->regs[ASIC_CUR_SECTOR] >> 16;
		if (Cur_Sector >= 0x5A)
		{
			CUR_BLOCK = 1;
			Cur_Sector -= 0x5A;
		}
		else
			CUR_BLOCK = 0;

		if (!dd_bm_mode_read)		//WRITE MODE
		{
			//printf("--DD_UPDATE_BM WRITE Block %d Sector %X\n", ((dd->regs[ASIC_CUR_TK] & 0x0FFF0000U) >> 15) + CUR_BLOCK, Cur_Sector);

			if (Cur_Sector < SECTORS_PER_BLOCK)
			{
				dd_write_sector(dd);
				Cur_Sector++;
				dd->regs[ASIC_CMD_STATUS] |= 0x40000000;
			}
			else if (Cur_Sector < SECTORS_PER_BLOCK + 1)
			{
				if (dd->regs[ASIC_BM_STATUS_CTL] & 0x01000000)
				{
					CUR_BLOCK = 1 - CUR_BLOCK;
					Cur_Sector = 0;
					dd_write_sector(dd);
					//next block
					Cur_Sector += 1;
					dd->regs[ASIC_BM_STATUS_CTL] &= ~0x01000000;
					dd->regs[ASIC_CMD_STATUS] |= 0x40000000;
				}
				else
				{
					Cur_Sector++;
					dd->regs[ASIC_BM_STATUS_CTL] &= ~0x80000000;
				}
			}
		}
		else						//READ MODE
		{
			int Cur_Track = (dd->regs[ASIC_CUR_TK] & 0x1FFF0000U) >> 16;

			printf("--DD_UPDATE_BM READ Block %d Sector %X\n", ((dd->regs[ASIC_CUR_TK] & 0x1FFF0000U) >> 15) + CUR_BLOCK, Cur_Sector);

			if (Cur_Track == 6 && CUR_BLOCK == 0)
			{
				dd->regs[ASIC_CMD_STATUS] &= ~0x40000000;
				dd->regs[ASIC_BM_STATUS_CTL] |= 0x02000000;
			}
			else if (Cur_Sector < SECTORS_PER_BLOCK)		//user sector
			{
				dd_read_sector(dd);
				Cur_Sector++;
				dd->regs[ASIC_CMD_STATUS] |= 0x40000000;
			}
			else if (Cur_Sector < SECTORS_PER_BLOCK + 4)	//C2
			{
				Cur_Sector++;
				if (Cur_Sector == SECTORS_PER_BLOCK + 4)
				{
					//dd_read_C2(opaque);
					dd->regs[ASIC_CMD_STATUS] |= 0x10000000;
				}
			}
			else if (Cur_Sector == SECTORS_PER_BLOCK + 4)	//Gap
			{
				if (dd->regs[ASIC_BM_STATUS_CTL] & 0x01000000)
				{
					CUR_BLOCK = 1 - CUR_BLOCK;
					Cur_Sector = 0;
					dd->regs[ASIC_BM_STATUS_CTL] &= ~0x01000000;
				}
				else
					dd->regs[ASIC_BM_STATUS_CTL] &= ~0x80000000;
			}
		}

		dd->regs[ASIC_CUR_SECTOR] = (Cur_Sector + (0x5A * CUR_BLOCK)) << 16;
		
		dd->regs[ASIC_CMD_STATUS] |= 0x04000000;
		cp0_update_count();
		add_interupt_event(CART_INT, 1000);
	}
}

void dd_write_sector(void *opaque)
{
   unsigned i;
   int Cur_Sector, offset;
	struct dd_controller *dd = (struct dd_controller *) opaque;

	/* WRITE SECTOR */
	Cur_Sector = dd->regs[ASIC_CUR_SECTOR] >> 16;
	if (Cur_Sector >= 0x5A)
		Cur_Sector -= 0x5A;

	offset = dd_track_offset;
	offset += CUR_BLOCK * SECTORS_PER_BLOCK * ddZoneSecSize[dd_zone];
	offset += (Cur_Sector - 1) * ddZoneSecSize[dd_zone];

	for (i = 0; i <= (int)(dd->regs[ASIC_HOST_SECBYTE] >> 16); i++)
		dd->disk.disk[offset + i] = dd->sec_buf[i];
}

void dd_read_sector(void *opaque)
{
   unsigned i;
   int offset, Cur_Sector;
	struct dd_controller *dd = (struct dd_controller *) opaque;

	/*READ SECTOR */
	Cur_Sector = dd->regs[ASIC_CUR_SECTOR] >> 16;
	if (Cur_Sector >= 0x5A)
		Cur_Sector -= 0x5A;

	offset = dd_track_offset;
	offset += CUR_BLOCK * SECTORS_PER_BLOCK * ddZoneSecSize[dd_zone];
	offset += Cur_Sector * ddZoneSecSize[dd_zone];

#if 0
	printf("--DD_UPDATE_BM READ Block %d Sector %X -- Offset: 0x%08X\n", ((dd->regs[ASIC_CUR_TK] & 0x1FFF0000U) >> 15) + CUR_BLOCK, Cur_Sector, offset);
#endif

	for (i = 0; i <= (int)(dd->regs[ASIC_HOST_SECBYTE] >> 16); i++)
		dd->sec_buf[i] = dd->disk.disk[offset + i];
}
