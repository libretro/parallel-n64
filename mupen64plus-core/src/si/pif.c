/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - pif.c                                                   *
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "pif.h"
#include "n64_cic_nus_6105.h"

#include "si_controller.h"

#include "../api/m64p_types.h"
#include "../api/callbacks.h"
#include "../api/debugger.h"
#include "../main/main.h"
#include "../main/rom.h"
#include "../main/util.h"
#include "../memory/memory.h"
#include "../plugin/plugin.h"
#include "r4300/cp0.h"
#include "r4300/r4300.h"
#include "r4300/interupt.h"

//#define DEBUG_PIF
#ifdef DEBUG_PIF
void print_pif(struct pif* pif)
{
   int i;
   for (i=0; i<(64/8); i++)
      DebugMessage(M64MSG_INFO, "%x %x %x %x | %x %x %x %x",
            pif->ram[i*8+0], pif->ram[i*8+1], pif->ram[i*8+2], pif->ram[i*8+3],
            pif->ram[i*8+4], pif->ram[i*8+5], pif->ram[i*8+6], pif->ram[i*8+7]);
}
#endif

void eeprom_status_command(struct pif *pif, int channel, uint8_t *cmd)
{
   /* check size */
   if (cmd[1] != 3)
   {
      cmd[1] |= 0x40;
      if ((cmd[1] & 3) > 0)
         cmd[3] = 0;
      if ((cmd[1] & 3) > 1)
         cmd[4] = (ROM_SETTINGS.savetype != EEPROM_16KB) ? 0x80 : 0xc0;
      if ((cmd[1] & 3) > 2)
         cmd[5] = 0;
   }
   else
   {
      cmd[3] = 0;
      cmd[4] = (ROM_SETTINGS.savetype != EEPROM_16KB) ? 0x80 : 0xc0;
      cmd[5] = 0;
   }
}

void eeprom_read_command(struct pif *pif, int channel, uint8_t *cmd)
{
   /* read 8-byte block. */
   uint16_t addr = cmd[3] * 8;

   if (addr < 0x200)
      memcpy(&cmd[4], saved_memory.eeprom + addr, 8);
   else
      memcpy(&cmd[4], saved_memory.eeprom2 + addr - 0x200, 8);
}

void eeprom_write_command(struct pif *pif, int channel, uint8_t *cmd)
{
   /* write 8-byte block. */
   uint16_t addr = cmd[3]*8;

   if (addr < 0x200)
      memcpy(saved_memory.eeprom + addr, &cmd[4], 8);
   else
      memcpy(saved_memory.eeprom2 + addr - 0x200, &cmd[4], 8);
}

static uint8_t pak_crc(uint8_t *data)
{
   int i;
   uint8_t crc = 0;

   for (i = 0; i <= 0x20; i++)
   {
      int mask;
      for (mask = 0x80; mask >= 1; mask >>= 1)
      {
         int xor_tap = (crc & 0x80) ? 0x85 : 0x00;
         crc <<= 1;
         if (i != 0x20 && (data[i] & mask)) crc |= 1;
         crc ^= xor_tap;
      }
   }
   return crc;
}

static void read_controller_read_buttons(struct pif *pif, int channel, uint8_t *cmd)
{
   BUTTONS Keys;
   if (!Controls[channel].Present)
      return;

   input.getKeys(channel, &Keys);
   *((uint32_t*)(cmd + 3)) = Keys.Value;

#ifdef COMPARE_CORE
   CoreCompareDataSync(4, cmd + 3);
#endif
}

static void read_controller_read_pak(struct pif *pif, int channel, uint8_t *cmd)
{
   if (!Controls[channel].Present)
      return;
   if (Controls[channel].Plugin != PLUGIN_RAW)
      return;

   if (input.readController)
      input.readController(channel, cmd);
}

static void read_controller_write_pak(struct pif *pif, int channel, uint8_t *cmd)
{
   if (!Controls[channel].Present)
      return;
   if (Controls[channel].Plugin != PLUGIN_RAW)
      return;

   input.readController(channel, cmd);
}

static void read_controller(struct pif *pif, int channel, uint8_t *cmd)
{
   switch (cmd[2])
   {
      case 1:
         read_controller_read_buttons(pif, channel, cmd);
         break;
      case 2:
         read_controller_read_pak(pif, channel, cmd);
         break;
      case 3:
         read_controller_write_pak(pif, channel, cmd);
         break;
   }
}

static void process_controller_command(struct pif *pif, int channel, uint8_t *cmd)
{
   switch (cmd[2])
   {
      case PIF_CMD_STATUS:
      case PIF_CMD_RESET:
         if ((cmd[1] & 0x80))
            break;
#ifdef DEBUG_PIF
         DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command %02x check pack present", Control, Command[2]);
#endif
         if (Controls[channel].Present)
         {
            cmd[3] = 0x05;
            cmd[4] = 0x00;
            switch (Controls[channel].Plugin)
            {
               case PLUGIN_MEMPAK:
                  cmd[5] = 1;
                  break;
               case PLUGIN_RAW:
                  cmd[5] = 1;
                  break;
               default:
                  cmd[5] = 0;
                  break;
            }
         }
         else
            cmd[1] |= 0x80;
         break;
      case PIF_CMD_CONTROLLER_READ:
         if (!Controls[channel].Present)
            cmd[1] |= 0x80;
         break;
      case PIF_CMD_PAK_READ:
         if (Controls[channel].Present)
         {
            switch (Controls[channel].Plugin)
            {
               case PLUGIN_MEMPAK:
                  {
                     int address = (cmd[3] << 8) | cmd[4];
#ifdef DEBUG_PIF
                     DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command 2 read mempack address %04x", Control, address);
#endif
                     if (address == 0x8001)
                     {
                        memset(&cmd[5], 0, 0x20);
                        cmd[0x25] = pak_crc(&cmd[5]);
                     }
                     else
                     {
                        address &= 0xFFE0;
                        if (address <= 0x7FE0)
                           memcpy(&cmd[5], &saved_memory.mempack[channel][address], 0x20);
                        else
                           memset(&cmd[5], 0, 0x20);
                        cmd[0x25] = pak_crc(&cmd[5]);
                     }
                  }
                  break;
               case PLUGIN_RAW:
#ifdef DEBUG_PIF
                  DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command 2 controllerCommand (in Input plugin)", Control);
#endif
                  input.controllerCommand(channel, cmd);
                  break;
               default:
#ifdef DEBUG_PIF
                  DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command 2 (no pack plugged in)", Control);
#endif
                  memset(&cmd[5], 0, 0x20);
                  cmd[0x25] = 0;
            }
         }
         else
            cmd[1] |= 0x80;
         break;
      case PIF_CMD_PAK_WRITE: // write controller pack
         if (Controls[channel].Present)
         {
            switch (Controls[channel].Plugin)
            {
               case PLUGIN_MEMPAK:
                  {
                     int address = (cmd[3] << 8) | cmd[4];
#ifdef DEBUG_PIF
                     DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command 3 write mempack address %04x", Control, address);
#endif
                     if (address == 0x8001)
                        cmd[0x25] = pak_crc(&cmd[5]);
                     else
                     {
                        address &= 0xFFE0;
                        if (address <= 0x7FE0)
                        {
                           memcpy(&saved_memory.mempack[channel][address], &cmd[5], 0x20);
                        }
                        cmd[0x25] = pak_crc(&cmd[5]);
                     }
                  }
                  break;
               case PLUGIN_RAW:
#ifdef DEBUG_PIF
                  DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command 3 controllerCommand (in Input plugin)", Control);
#endif
                  input.controllerCommand(channel, cmd);
                  break;
               default:
#ifdef DEBUG_PIF
                  DebugMessage(M64MSG_INFO, "internal_ControllerCommand() Channel %i Command 3 (no pack plugged in)", Control);
#endif
                  cmd[0x25] = pak_crc(&cmd[5]);
            }
         }
         else
            cmd[1] |= 0x80;
         break;
   }
}

static void process_cart_command(struct pif* pif, int channel, uint8_t* cmd)
{
   switch (cmd[2])
   {
      case PIF_CMD_STATUS:
         eeprom_status_command(pif, channel, cmd);
         break;
      case PIF_CMD_EEPROM_READ:
         eeprom_read_command(pif, channel, cmd);
         break;
      case PIF_CMD_EEPROM_WRITE:
         eeprom_write_command(pif, channel, cmd);
         break;
      case PIF_CMD_AF_RTC_STATUS:
         af_rtc_status_command(pif, channel, cmd);
         break;
      case PIF_CMD_AF_RTC_READ:
         af_rtc_read_command(pif, channel, cmd);
         break;
      case PIF_CMD_AF_RTC_WRITE:
         af_rtc_write_command(pif, channel, cmd);
         break;
      default:
         DebugMessage(M64MSG_ERROR, "unknown PIF command: %02x", cmd[2]);
   }
}

void init_pif(struct pif* pif)
{
   memset(pif->ram, 0, PIF_RAM_SIZE);
}

int read_pif_ram(void* opaque, uint32_t address, uint32_t* value)
{
   struct si_controller* si = (struct si_controller*)opaque;
   uint32_t addr = pif_ram_address(address);
   if (addr >= PIF_RAM_SIZE)
   {
      DebugMessage(M64MSG_ERROR, "Invalid PIF address: %08x", address);
      *value = 0;
      return -1;
   }
   memcpy(value, si->pif.ram + addr, sizeof(*value));
   *value = sl(*value);
   return 0;
}

int write_pif_ram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
   struct si_controller* si = (struct si_controller*)opaque;
   uint32_t addr = pif_ram_address(address);
   if (addr >= PIF_RAM_SIZE)
   {
      DebugMessage(M64MSG_ERROR, "Invalid PIF address: %08x", address);
      return -1;
   }
   masked_write((uint32_t*)(&si->pif.ram[addr]), sl(value), sl(mask));
   if ((addr == 0x3c) && (mask & 0xff))
   {
      if (si->pif.ram[0x3f] == 0x08)
      {
         si->pif.ram[0x3f] = 0;
         update_count();
         add_interupt_event(SI_INT, /*0x100*/0x900);
      }
      else
      {
         update_pif_write(si);
      }
   }
   return 0;
}

void update_pif_write(struct si_controller *si)
{
   int8_t challenge[30], response[30];
   int i=0, channel=0;

   struct pif* pif = &si->pif;

   if (pif->ram[0x3F] > 1)
   {
      switch (pif->ram[0x3F])
      {
         case 0x02:
#ifdef DEBUG_PIF
            DebugMessage(M64MSG_INFO, "update_pif_write() pif_ram[0x3f] = 2 - CIC challenge");
#endif
            // format the 'challenge' message into 30 nibbles for X-Scale's CIC code
            for (i = 0; i < 15; i++)
            {
               challenge[i*2] =   (pif->ram[48+i] >> 4) & 0x0f;
               challenge[i*2+1] =  pif->ram[48+i]       & 0x0f;
            }
            // calculate the proper response for the given challenge (X-Scale's algorithm)
            n64_cic_nus_6105(challenge, response, CHL_LEN - 2);
            pif->ram[46] = 0;
            pif->ram[47] = 0;
            // re-format the 'response' into a byte stream
            for (i = 0; i < 15; i++)
               pif->ram[48+i] = (response[i*2] << 4) + response[i*2+1];
            // the last byte (2 nibbles) is always 0
            pif->ram[63] = 0;
            break;
         case 0x08:
#ifdef DEBUG_PIF
            DebugMessage(M64MSG_INFO, "update_pif_write() pif_ram[0x3f] = 8");
#endif
            pif->ram[0x3F] = 0;
            break;
         default:
            DebugMessage(M64MSG_ERROR, "error in update_pif_write(): %x", pif->ram[0x3F]);
      }
      return;
   }
   while (i<0x40)
   {
      switch (pif->ram[i])
      {
         case 0x00:
            channel++;
            if (channel > 6) i=0x40;
            break;
         case 0xFF:
            break;
         default:
            if (!(pif->ram[i] & 0xC0))
            {
               if (channel < 4)
               {
                  if (Controls[channel].Present && Controls[channel].RawData)
                     input.controllerCommand(channel, &pif->ram[i]);
                  else
                     process_controller_command(pif, channel, &pif->ram[i]);
               }
               else if (channel == 4)
                  process_cart_command(pif, channel, &pif->ram[i]);
               else
                  DebugMessage(M64MSG_ERROR, "channel >= 4 in update_pif_write");
               i += pif->ram[i] + (pif->ram[(i+1)] & 0x3F) + 1;
               channel++;
            }
            else
               i=0x40;
      }
      i++;
   }
   //pif->ram[0x3F] = 0;
   input.controllerCommand(-1, NULL);
}

void update_pif_read(struct si_controller *si)
{
   struct pif* pif = &si->pif;

   int i=0, channel=0;

   while (i<0x40)
   {
      switch (pif->ram[i])
      {
         case 0x00:
            channel++;
            if (channel > 6) i=0x40;
            break;
         case 0xFE:
            i = 0x40;
            break;
         case 0xFF:
            break;
         case 0xB4:
         case 0x56:
         case 0xB8:
            break;
         default:
            if (!(pif->ram[i] & 0xC0))
            {
               if (channel < 4)
               {
                  if (Controls[channel].Present &&
                        Controls[channel].RawData)
                     input.readController(channel, &pif->ram[i]);
                  else
                     read_controller(pif, channel, &pif->ram[i]);
               }
               i += pif->ram[i] + (pif->ram[(i+1)] & 0x3F) + 1;
               channel++;
            }
            else
               i=0x40;
      }
      i++;
   }
   input.readController(-1, NULL);
}
