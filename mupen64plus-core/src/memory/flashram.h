/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* Mupen64plus - flashram.h *
* Mupen64Plus homepage: http://code.google.com/p/mupen64plus/ *
* Copyright (C) 2014 Bobby Smiles *
* Copyright (C) 2002 Hacktarux *
* *
* This program is free software; you can redistribute it and/or modify *
* it under the terms of the GNU General Public License as published by *
* the Free Software Foundation; either version 2 of the License, or *
* (at your option) any later version. *
* *
* This program is distributed in the hope that it will be useful, *
* but WITHOUT ANY WARRANTY; without even the implied warranty of *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the *
* GNU General Public License for more details. *
* *
* You should have received a copy of the GNU General Public License *
* along with this program; if not, write to the *
* Free Software Foundation, Inc., *
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. *
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef M64P_PI_FLASHRAM_H
#define M64P_PI_FLASHRAM_H

#include <stdint.h>

struct pi_controller;

enum { FLASHRAM_SIZE = 0x20000 };

enum flashram_mode
{
   FLASHRAM_MODE_NOPES = 0,
   FLASHRAM_MODE_ERASE,
   FLASHRAM_MODE_WRITE,
   FLASHRAM_MODE_READ,
   FLASHRAM_MODE_STATUS
};

typedef struct _flashram_info
{
	int32_t use_flashram;
	enum flashram_mode mode;
	uint64_t status;
	unsigned int erase_offset;
   unsigned int write_pointer;
} Flashram_info;

extern Flashram_info flashram_info;

void init_flashram(void);

void flashram_command(uint32_t command);
uint32_t flashram_status(void);

void dma_read_flashram(void);
void dma_write_flashram(void);

#endif
