/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - macros.h                                                *
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

#ifndef M64P_R4300_MACROS_H
#define M64P_R4300_MACROS_H

#define SE8(a) ((int64_t) ((int8_t) (a)))
#define SE16(a) ((int64_t) ((int16_t) (a)))
#define SE32(a) ((int64_t) ((int32_t) (a)))

#define rrt *mupencorePC->f.r.rt
#define rrd *mupencorePC->f.r.rd
#define rfs mupencorePC->f.r.nrd
#define rrs *mupencorePC->f.r.rs
#define rsa mupencorePC->f.r.sa
#define irt *mupencorePC->f.i.rt
#define ioffset mupencorePC->f.i.immediate
#define iimmediate mupencorePC->f.i.immediate
#define irs *mupencorePC->f.i.rs
#define ibase *mupencorePC->f.i.rs
#define jinst_index mupencorePC->f.j.inst_index
#define lfbase mupencorePC->f.lf.base
#define lfft mupencorePC->f.lf.ft
#define lfoffset mupencorePC->f.lf.offset
#define cfft mupencorePC->f.cf.ft
#define cffs mupencorePC->f.cf.fs
#define cffd mupencorePC->f.cf.fd

/* 32 bits macros */
#ifdef MSB_FIRST
#define rrt32 *((int32_t*) mupencorePC->f.r.rt + 1)
#define rrd32 *((int32_t*) mupencorePC->f.r.rd + 1)
#define rrs32 *((int32_t*) mupencorePC->f.r.rs + 1)
#define irs32 *((int32_t*) mupencorePC->f.i.rs + 1)
#define irt32 *((int32_t*) mupencorePC->f.i.rt + 1)
#else
#define rrt32 *((int32_t*) mupencorePC->f.r.rt)
#define rrd32 *((int32_t*) mupencorePC->f.r.rd)
#define rrs32 *((int32_t*) mupencorePC->f.r.rs)
#define irs32 *((int32_t*) mupencorePC->f.i.rs)
#define irt32 *((int32_t*) mupencorePC->f.i.rt)
#endif

#endif /* M64P_R4300_MACROS_H */

