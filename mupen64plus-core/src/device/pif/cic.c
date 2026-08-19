/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cic.c                                                   *
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

#include "cic.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/m64p_types.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>


/* Returns the cics[] index for a known IPL3 checksum, or -1 when the
 * bootcode is not a retail CIC - homebrew IPL3s (libdragon) land here.
 * Kept in one place so callers that need to know whether the cartridge
 * carries retail bootcode (and therefore SGI-toolchain microcode that
 * an HLE RSP can recognize) share the table with the CIC setup. */
int cic_ipl3_crc_known(uint64_t crc)
{
    switch(crc)
    {
        case UINT64_C(0x000000D057C85244): return 2;
        case UINT64_C(0x000000D0027FDF31):
        case UINT64_C(0x000000CFFB631223): return 1;
        case UINT64_C(0x000000D6497E414B): return 3;
        case UINT64_C(0x0000011A49F60E96): return 4;
        case UINT64_C(0x000000D6D5BE5580): return 5;
        case UINT64_C(0x000001053BC19870): return 6;
        case UINT64_C(0x000000A5F80BF620): return 0;
        case UINT64_C(0x000000D2E53EF008): return 7;
        case UINT64_C(0x000000D23829ED4C): return 7;
        case UINT64_C(0x000000D2E53EF39F): return 8;
        case UINT64_C(0x000000D2E53E5DDA): return 9;
        default: return -1;
    }
}

int cic_ipl3_known(const void* ipl3)
{
    size_t i;
    uint64_t crc = 0;

    for (i = 0; i < 0xfc0/4; i++)
        crc += ((const uint32_t*)ipl3)[i];

    return cic_ipl3_crc_known(crc) >= 0;
}

void init_cic_using_ipl3(struct cic* cic, const void* ipl3)
{
    size_t i;
    uint64_t crc = 0;

    static const struct cic cics[] =
    {
        { "5101", CIC_5101, 0xac },
        { "X101", CIC_X101, 0x3f },
        { "X102", CIC_X102, 0x3f },
        { "X103", CIC_X103, 0x78 },
        { "X105", CIC_X105, 0x91 },
        { "X106", CIC_X106, 0x85 },
        { "5167", CIC_5167, 0xdd },
        { "8303", CIC_8303, 0xdd },
        { "8401", CIC_8401, 0xdd },
        { "8501", CIC_8501, 0xde }
    };

    for (i = 0; i < 0xfc0/4; i++)
        crc += ((uint32_t*)ipl3)[i];

    if (cic_ipl3_crc_known(crc) < 0)
        DebugMessage(M64MSG_WARNING, "Unknown CIC type (%016" PRIX64 ")! using CIC 6102.", crc);

    switch(crc)
    {
        default:
            /* unknown: warned above, use CIC 6102 */
            /* fall through */
        case UINT64_C(0x000000D057C85244): i = 2; break; /* CIC_X102 */
        case UINT64_C(0x000000D0027FDF31):               /* CIC_X101 */
        case UINT64_C(0x000000CFFB631223): i = 1; break; /* CIC_X101 */
        case UINT64_C(0x000000D6497E414B): i = 3; break; /* CIC_X103 */
        case UINT64_C(0x0000011A49F60E96): i = 4; break; /* CIC_X105 */
        case UINT64_C(0x000000D6D5BE5580): i = 5; break; /* CIC_X106 */
        case UINT64_C(0x000001053BC19870): i = 6; break; /* CIC 5167 */
        case UINT64_C(0x000000A5F80BF620): i = 0; break; /* CIC 5101 */
        case UINT64_C(0x000000D2E53EF008): i = 7; break; /* CIC 8303 */
        case UINT64_C(0x000000D23829ED4C): i = 7; break; /* CIC 8303 (alt 64DD IPL: deviplcart) */
        case UINT64_C(0x000000D2E53EF39F): i = 8; break; /* CIC 8401 */
        case UINT64_C(0x000000D2E53E5DDA): i = 9; break; /* CIC 8501 */
    }

    memcpy(cic, &cics[i], sizeof(*cic));

    DebugMessage(M64MSG_INFO, "Using CIC type %s", cic->name);
}

