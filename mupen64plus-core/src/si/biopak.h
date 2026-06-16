/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - biopak.h                                                *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2017 Bobby Smiles                                       *
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

#ifndef M64P_SI_BIOPAK_H
#define M64P_SI_BIOPAK_H

#include <stddef.h>
#include <stdint.h>

/* N64 Bio Sensor (heart rate sensor, used by Tetris 64). The pak reports a
 * square wave at the configured pulse rate; the game derives BPM from the
 * edges. Implementation based on raphnet's notes:
 * http://www.raphnet.net/divers/n64_bio_sensor/index_en.php */
struct biopak
{
   unsigned int bpm;
};

void init_biopak(struct biopak* bpk, unsigned int bpm);

void biopak_read_command(struct biopak* bpk, uint16_t address, uint8_t* data, size_t size);
void biopak_write_command(struct biopak* bpk, uint16_t address, const uint8_t* data, size_t size);

#endif
