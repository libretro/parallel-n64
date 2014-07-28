/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cp0.h                                                   *
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

#ifndef M64P_R4300_CP0_H
#define M64P_R4300_CP0_H

/* registers macros */
#define Index g_cp0_regs[0]
#define Random g_cp0_regs[1]
#define EntryLo0 g_cp0_regs[2]
#define EntryLo1 g_cp0_regs[3]
#define Context g_cp0_regs[4]
#define PageMask g_cp0_regs[5]
#define Wired g_cp0_regs[6]
#define BadVAddr g_cp0_regs[8]
#define Count g_cp0_regs[9]
#define EntryHi g_cp0_regs[10]
#define Compare g_cp0_regs[11]
#define Status g_cp0_regs[12]
#define Cause g_cp0_regs[13]
#define EPC g_cp0_regs[14]
#define PRevID g_cp0_regs[15]
#define Config g_cp0_regs[16]
#define LLAddr g_cp0_regs[17]
#define WatchLo g_cp0_regs[18]
#define WatchHi g_cp0_regs[19]
#define XContext g_cp0_regs[20]
#define PErr g_cp0_regs[26]
#define CacheErr g_cp0_regs[27]
#define TagLo g_cp0_regs[28]
#define TagHi g_cp0_regs[29]
#define ErrorEPC g_cp0_regs[30]

extern unsigned int g_cp0_regs[32];

int check_cop1_unusable(void);
void update_count(void);

#endif /* M64P_R4300_CP0_H */
