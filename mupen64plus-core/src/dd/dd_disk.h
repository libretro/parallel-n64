/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*   Mupen64plus - dd_disk.h                                               *
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

#ifndef __DDDISK_H__
#define __DDDISK_H__

#include <stdint.h>

#include "api/m64p_types.h"

#define SECTORS_PER_BLOCK     85
#define BLOCKS_PER_TRACK      2

extern int dd_bm_mode_read;	//BM MODE 0 = WRITE, MODE 1 = READ
extern int CUR_BLOCK;			//Current Block
extern int dd_sector55;

struct dd_disk
{
	uint8_t* disk;
	size_t disk_size;
};

void connect_dd_disk(struct dd_disk* dd_disk,
                     uint8_t* disk, size_t disk_size);

/* Disk Loading and Saving functions */

m64p_error open_dd_disk(const unsigned char* diskimage, unsigned int size);
m64p_error close_dd_disk(void);

extern unsigned char* g_dd_disk;
extern int g_dd_disk_size;

/* Disk Read / Write functions */

void dd_set_zone_and_track_offset(void *data);
void dd_update_bm(void *data);
void dd_write_sector(void *data);
void dd_read_sector(void *data);

#endif
