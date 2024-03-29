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

#include "pif.h"
#include "n64_cic_nus_6105.h"
#include "game_controller.h"
#include "si_controller.h"

#include "../api/m64p_types.h"
#include "../api/callbacks.h"
#include "../memory/memory.h"
#include "../plugin/plugin.h"
#include "r4300/r4300_core.h"

#include "../plugin/emulate_game_controller_via_input_plugin.h"
#include "../plugin/rumble_via_input_plugin.h"

#include <string.h>

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

static void process_cart_command(struct pif* pif, uint8_t* cmd)
{
   switch (cmd[2])
   {
      case PIF_CMD_STATUS: eeprom_status_command(&pif->eeprom, cmd); break;
      case PIF_CMD_EEPROM_READ: eeprom_read_command(&pif->eeprom, cmd); break;
      case PIF_CMD_EEPROM_WRITE: eeprom_write_command(&pif->eeprom, cmd); break;
      case PIF_CMD_AF_RTC_STATUS: af_rtc_status_command(&pif->af_rtc, cmd); break;
      case PIF_CMD_AF_RTC_READ: af_rtc_read_command(&pif->af_rtc, cmd); break;
      case PIF_CMD_AF_RTC_WRITE: af_rtc_write_command(&pif->af_rtc, cmd); break;
      default:
         DebugMessage(M64MSG_ERROR, "unknown PIF command: %02x", cmd[2]);
   }
}

static void game_controller_dummy_save(void *user_data)
{
}

void init_pif(struct pif *pif,
      void *eeprom_user_data,
      void (*eeprom_save)(void*),
      uint8_t *eeprom_data,
      size_t eeprom_size,
      uint16_t eeprom_id,
      void* af_rtc_user_data,
      const struct tm* (*af_rtc_get_time)(void*),
      const uint8_t *ipl3
      )
{
   size_t i;

   for (i = 0; i < GAME_CONTROLLERS_COUNT; ++i)
   {
      static int32_t channels[] = { 0, 1, 2, 3 };
      init_game_controller(
            &pif->controllers[i], 
            (void*)&channels[i],
            egcvip_is_connected,
            egcvip_get_input,
            NULL,
            &game_controller_dummy_save,
            &saved_memory.mempack[i][0],
            &channels[i],
            rvip_rumble
            );
   }

   init_eeprom(&pif->eeprom,
         eeprom_user_data, eeprom_save, eeprom_data, eeprom_size, eeprom_id);
   init_af_rtc(&pif->af_rtc, af_rtc_user_data, af_rtc_get_time);
   init_cic_using_ipl3(&pif->cic, ipl3);
}

void poweron_pif(struct pif* pif)
{
   memset(pif->ram, 0, PIF_RAM_SIZE);
}

int read_pif_ram(void* opaque, uint32_t address, uint32_t* value)
{
   struct si_controller* si = (struct si_controller*)opaque;
   uint32_t addr            = PIF_RAM_ADDR(address);

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
   uint32_t addr            = PIF_RAM_ADDR(address);

   if (addr >= PIF_RAM_SIZE)
   {
      DebugMessage(M64MSG_ERROR, "Invalid PIF address: %08x", address);
      return -1;
   }

   si->pif.ram[addr] = MASKED_WRITE((uint32_t*)(&si->pif.ram[addr]), sl(value), sl(mask));

   if ((addr == 0x3c) && (mask & 0xff))
   {
      if (si->pif.ram[0x3f] == 0x08)
      {
         si->pif.ram[0x3f] = 0;
         cp0_update_count();
         add_interrupt_event(SI_INT, /*0x100*/0x900);
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
   int i=0, channel=0;
   struct pif* pif = &si->pif;

   pif->cic_challenge = 0;

   if (pif->ram[0x3F] > 1)
   {
      int8_t challenge[30], response[30];

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
            pif->cic_challenge = 1;
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
                     process_controller_command(&pif->controllers[channel], &pif->ram[i]);
               }
               else if (channel == 4)
                  process_cart_command(pif, &pif->ram[i]);
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
   
   /* notify the INPUT plugin that we're at the end of PIF ram processing */
   input.controllerCommand(-1, NULL);
}

void update_pif_read(struct si_controller *si)
{
   struct pif* pif = &si->pif;

   int i=0, channel=0;

   /* When PIF ram contains a CIC challenge result, do not
    * process the memory as if it were normal commands. */
   if (pif->cic_challenge)
      return;

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
                     read_controller(&pif->controllers[channel], &pif->ram[i]);
               }
               i += pif->ram[i] + (pif->ram[(i+1)] & 0x3F) + 1;
               channel++;
            }
            else
               i=0x40;
      }
      i++;
   }

   /* notify the INPUT plugin that we're at the end of PIF ram processing */
   input.readController(-1, NULL);
}
