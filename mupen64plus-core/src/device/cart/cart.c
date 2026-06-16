/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cart.c                                                  *
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

#include "cart.h"

#include "../../api/callbacks.h"
#include "../../api/m64p_types.h"

#include <stdint.h>
#include <string.h>

/* Joybus cart device: handles the eeprom and AF-RTC PIF-channel commands,
 * imported from mupen64plus-next. Routes through parallel-n64's chip block
 * accessors (added in region 12a). */
static void process_cart_command(void* jbd,
    const uint8_t* tx, const uint8_t* tx_buf,
    uint8_t* rx, uint8_t* rx_buf)
{
    struct cart* cart = (struct cart*)jbd;

    uint8_t cmd = tx_buf[0];

    switch (cmd)
    {
    case JCMD_RESET:
        /* TODO: perform internal reset */
        /* fall through */
    case JCMD_STATUS: {
        JOYBUS_CHECK_COMMAND_FORMAT(1, 3)

        if (cart->eeprom.type) {
            /* set type, status, and extra */
            rx_buf[0] = (uint8_t)(cart->eeprom.type >> 0);
            rx_buf[1] = (uint8_t)(cart->eeprom.type >> 8);
            rx_buf[2] = 0x00;
        }
        else {
            *rx |= 0x80;
        }
    } break;

    case JCMD_EEPROM_READ: {
        JOYBUS_CHECK_COMMAND_FORMAT(2, 8)
        eeprom_read_block(&cart->eeprom, tx_buf[1], &rx_buf[0]);
    } break;

    case JCMD_EEPROM_WRITE: {
        JOYBUS_CHECK_COMMAND_FORMAT(10, 1)
        eeprom_write_block(&cart->eeprom, tx_buf[1], &tx_buf[2], &rx_buf[0]);
    } break;

    case JCMD_AF_RTC_STATUS: {
        JOYBUS_CHECK_COMMAND_FORMAT(1, 3)

        /* set type and status */
        rx_buf[0] = (uint8_t)(JDT_AF_RTC >> 0);
        rx_buf[1] = (uint8_t)(JDT_AF_RTC >> 8);
        rx_buf[2] = 0x00;
    } break;

    case JCMD_AF_RTC_READ: {
        JOYBUS_CHECK_COMMAND_FORMAT(2, 9)
        af_rtc_read_block(&cart->af_rtc, tx_buf[1], &rx_buf[0], &rx_buf[8]);
    } break;

    case JCMD_AF_RTC_WRITE: {
        JOYBUS_CHECK_COMMAND_FORMAT(10, 1)
        af_rtc_write_block(&cart->af_rtc, tx_buf[1], &tx_buf[2], &rx_buf[0]);
    } break;

    default:
        DebugMessage(M64MSG_WARNING, "cart: Unknown command %02x %02x %02x",
            *tx, *rx, cmd);
    }
}

const struct joybus_device_interface
    g_ijoybus_device_cart =
{
    NULL,
    process_cart_command,
    NULL
};

/* PI dom2 (save chip) MMIO dispatch: route to sram or flashram by use_flashram,
 * matching next. The address-format check that next performs for the flashram
 * branch lives in parallel-n64's read_flashram/write_flashram accessors. */
void read_cart_dom2(void* opaque, uint32_t address, uint32_t* value)
{
    struct cart* cart = (struct cart*)opaque;

    if (cart->use_flashram == -1)
    {
        read_sram(&cart->sram, address, value);
    }
    else
    {
        cart->use_flashram = 1;
        read_flashram(&cart->flashram, address, value);
    }
}

void write_cart_dom2(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct cart* cart = (struct cart*)opaque;

    if (cart->use_flashram == -1)
    {
        write_sram(&cart->sram, address, value, mask);
    }
    else
    {
        cart->use_flashram = 1;
        write_flashram(&cart->flashram, address, value, mask);
    }
}

unsigned int cart_dom2_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    struct cart* cart = (struct cart*)opaque;
    unsigned int cycles;

    if (cart->use_flashram != 1)
    {
        cycles = sram_dma_read(&cart->sram, dram, dram_addr, cart_addr, length);
        cart->use_flashram = -1;
    }
    else
    {
        cycles = flashram_dma_read(&cart->flashram, dram, dram_addr, cart_addr, length);
    }

    return cycles;
}

unsigned int cart_dom2_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    struct cart* cart = (struct cart*)opaque;
    unsigned int cycles;

    if (cart->use_flashram != 1)
    {
        cycles = sram_dma_write(&cart->sram, dram, dram_addr, cart_addr, length);
        cart->use_flashram = -1;
    }
    else
    {
        cycles = flashram_dma_write(&cart->flashram, dram, dram_addr, cart_addr, length);
    }

    return cycles;
}

unsigned int cart_dom3_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    struct cart* cart = (struct cart*)opaque;
    return cart_rom_dma_read(&cart->cart_rom, dram, dram_addr, cart_addr, length);
}

unsigned int cart_dom3_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    struct cart* cart = (struct cart*)opaque;
    return cart_rom_dma_write(&cart->cart_rom, dram, dram_addr, cart_addr, length);
}
