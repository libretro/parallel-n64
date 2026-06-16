/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - game_controller.c                                       *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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

#include "game_controller.h"
#include "../pif/pif.h"
#include "../rcp/si/si_controller.h"

#include "../../api/m64p_types.h"
#include "../../api/m64p_plugin.h"
#include "../../api/callbacks.h"

#include "../../backends/api/controller_input_backend.h"

#include "../../../libretro/libretro_memory.h"

#include <stdint.h>
#include <string.h>

enum { PAK_CHUNK_SIZE = 0x20 };

static uint8_t pak_data_crc(uint8_t *data)
{
   int i;
   uint8_t crc = 0;

   for (i = 0; i <= PAK_CHUNK_SIZE; i++)
   {
      int mask;
      for (mask = 0x80; mask >= 1; mask >>= 1)
      {
         int xor_tap = (crc & 0x80) ? 0x85 : 0x00;
         crc <<= 1;
         if (i != PAK_CHUNK_SIZE && (data[i] & mask)) crc |= 1;
         crc ^= xor_tap;
      }
   }
   return crc;
}

static void read_controller_read_buttons(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    int connected = game_controller_is_connected(cont, &pak);

    if (connected != CONT_JOYPAD && connected != CONT_MOUSE)
        return;

    *((uint32_t*)(cmd + 3)) = game_controller_get_input(cont);
}

static void read_controller_read_gcn_buttons(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    int connected = game_controller_is_connected(cont, &pak);

    if (connected != CONT_GCN)
        return;
    
    uint8_t analogMode = cmd[3];

    *((BUTTONS_GCN*)(cmd + 5)) = game_controller_gcn_get_input(cont, analogMode);
}


static void controller_status_command(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    int connected = game_controller_is_connected(cont, &pak);

    if (cmd[1] & 0x80)
        return;

    if (!connected)
    {
        cmd[1] |= 0x80;
        return;
    }

    if (connected == CONT_JOYPAD)
      cmd[3] = 0x05;
    else if (connected == CONT_MOUSE)
      cmd[3] = 0x02;
    else if (connected == CONT_GCN)
      cmd[3] = 0x09;
    cmd[4] = 0x00;

    switch(pak)
    {
    case PAK_MEM:
    case PAK_RUMBLE:
    case PAK_TRANSFER:
    case PAK_BIO:
        cmd[5] = 1;
        break;

    case PAK_NONE:
    default:
        cmd[5] = 0;
    }
}

static void controller_read_buttons_command(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    int connected = game_controller_is_connected(cont, &pak);

    if (!connected)
        cmd[1] |= 0x80;

    /* NOTE: buttons reading is done in read_controller_read_buttons instead */
}

static void controller_read_pak_command(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    uint16_t address;
    uint8_t* data;
    uint8_t* crc;
    int connected    = game_controller_is_connected(cont, &pak);

    if (!connected)
    {
        cmd[1] |= 0x80;
        return;
    }

    address = (cmd[3] << 8) | (cmd[4] & 0xe0);
    data    = &cmd[5];
    crc     = &cmd[5 + PAK_CHUNK_SIZE];

    switch (pak)
    {
       case PAK_NONE:
          memset(data, 0, PAK_CHUNK_SIZE);
          break;
       case PAK_MEM:
          mempak_read_command(&cont->mempak, address, data, PAK_CHUNK_SIZE);
          break;
       case PAK_RUMBLE:
          rumblepak_read_command(&cont->rumblepak, address, data, PAK_CHUNK_SIZE);
          break;
       case PAK_TRANSFER:
          transferpak_read_command(&cont->transferpak, address, data, PAK_CHUNK_SIZE);
          break;
       case PAK_BIO:
          biopak_read_command(&cont->biopak, address, data, PAK_CHUNK_SIZE);
          break;
       default:
          DebugMessage(M64MSG_WARNING, "Unknown plugged pak %d", (int)pak);
    }

    *crc = (pak == PAK_NONE) ? ~pak_data_crc(data) : pak_data_crc(data);
}

static void controller_write_pak_command(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    uint16_t address;
    uint8_t* data;
    uint8_t* crc;
    int       connected = game_controller_is_connected(cont, &pak);

    if (!connected)
    {
        cmd[1] |= 0x80;
        return;
    }

    address = (cmd[3] << 8) | (cmd[4] & 0xe0);
    data = &cmd[5];
    crc = &cmd[5 + PAK_CHUNK_SIZE];

    switch (pak)
    {
       case PAK_NONE:
          /* do nothing */
          break;
       case PAK_MEM:
          mempak_write_command(&cont->mempak, address, data, PAK_CHUNK_SIZE);
          break;
       case PAK_RUMBLE:
          rumblepak_write_command(&cont->rumblepak, address, data, PAK_CHUNK_SIZE);
          break;
       case PAK_TRANSFER:
          transferpak_write_command(&cont->transferpak, address, data, PAK_CHUNK_SIZE);
          break;
       case PAK_BIO:
          biopak_write_command(&cont->biopak, address, data, PAK_CHUNK_SIZE);
          break;
       default:
          DebugMessage(M64MSG_WARNING, "Unknown plugged pak %d", (int)pak);
    }

    *crc = (pak == PAK_NONE) ? ~pak_data_crc(data) : pak_data_crc(data);
}

static void controller_gcn_shortpoll_command(struct game_controller* cont, uint8_t* cmd)
{
    enum pak_type pak;
    int connected = game_controller_is_connected(cont, &pak);

    if (connected != CONT_GCN)
    {
        cmd[1] |= 0x80;
        return;
    }

    /* NOTE: buttons reading is done in read_controller_read_gcn_buttons instead */
}

extern struct si_controller g_si;

void init_game_controller(struct game_controller *cont,
      void *cont_user_data,
      int (*cont_is_connected)(void*,enum pak_type*),
      uint32_t (*cont_get_input)(void*),
      BUTTONS_GCN (*cont_get_gcn_input)(void*, int),
      void* mpk_user_data,
      void (*mpk_save)(void*),
      uint8_t* mpk_data,
      void* rpk_user_data,
      void (*rpk_rumble)(void*,enum rumble_action))
{
   /* connect external game controllers */
   cont->user_data    = cont_user_data;
   cont->is_connected = cont_is_connected;
   cont->get_input    = cont_get_input;
   cont->get_gcn_input = cont_get_gcn_input;

   init_mempak(&cont->mempak, mpk_user_data, mpk_save, mpk_data);
   init_rumblepak(&cont->rumblepak, rpk_user_data, rpk_rumble);

   /* The bio sensor synthesises its pulse from the wall clock and needs no
    * external input, so it is initialised here with a resting default rate
    * (72 BPM) rather than threading a new parameter through every caller. */
   init_biopak(&cont->biopak, 72);
}

int game_controller_is_connected(struct game_controller* cont, enum pak_type* pak)
{
    return cont->is_connected(cont->user_data, pak);
}

uint32_t game_controller_get_input(struct game_controller* cont)
{
    return cont->get_input(cont->user_data);
}

BUTTONS_GCN game_controller_gcn_get_input(struct game_controller* cont, int analogMode)
{
    return cont->get_gcn_input(cont->user_data, analogMode);
}

void process_controller_command(struct game_controller* cont, uint8_t* cmd)
{
   switch (cmd[2])
   {
      case PIF_CMD_STATUS:
      case PIF_CMD_RESET:
         controller_status_command(cont, cmd);
         break;
      case PIF_CMD_CONTROLLER_READ:
         controller_read_buttons_command(cont, cmd);
         break;
      case PIF_CMD_PAK_READ:
         controller_read_pak_command(cont, cmd);
         break;
      case PIF_CMD_PAK_WRITE:
         controller_write_pak_command(cont, cmd);
         break;
      case PIF_CMD_GCN_SHORTPOLL:
         controller_gcn_shortpoll_command(cont, cmd);
         break;
   }
}

void read_controller(struct game_controller* cont, uint8_t* cmd)
{
    switch (cmd[2])
    {
       case PIF_CMD_CONTROLLER_READ:
          read_controller_read_buttons(cont, cmd); break;
       case PIF_CMD_GCN_SHORTPOLL:
          read_controller_read_gcn_buttons(cont, cmd); break;
    }
}


/* ----------------------------------------------------------------------------
 * mupen64plus-next joybus controller device (region 12c).
 *
 * This coexists with parallel-n64's flat process_controller_command above. It
 * addresses the controller's pak polymorphically through cont->ipak/cont->pak
 * and reads input through cont->icin/cont->cin (the controller_input_backend
 * bridge wired up at PIF init). The PIF channel dispatch is switched over to
 * g_ijoybus_device_controller in the PIF convergence step; until then this is
 * dormant.
 * ------------------------------------------------------------------------- */

enum {
    CONT_STATUS_PAK_PRESENT = 0x01,
    CONT_STATUS_PAK_CHANGED = 0x02
};

static void jb_pak_read_block(struct game_controller* cont,
    const uint8_t* addr_acrc, uint8_t* data, uint8_t* dcrc)
{
    uint16_t address = (addr_acrc[0] << 8) | (addr_acrc[1] & 0xe0);

    if (cont->ipak != NULL) {
        cont->ipak->read(cont->pak, address, data, PAK_CHUNK_SIZE);
        *dcrc = pak_data_crc(data);
    } else {
        *dcrc = ~pak_data_crc(data);
    }
}

static void jb_pak_write_block(struct game_controller* cont,
    const uint8_t* addr_acrc, const uint8_t* data, uint8_t* dcrc)
{
    uint16_t address = (addr_acrc[0] << 8) | (addr_acrc[1] & 0xe0);
    uint8_t tmp[PAK_CHUNK_SIZE];

    if (cont->ipak != NULL) {
        cont->ipak->write(cont->pak, address, data, PAK_CHUNK_SIZE);
        memcpy(tmp, data, PAK_CHUNK_SIZE);
        *dcrc = pak_data_crc(tmp);
    } else {
        memcpy(tmp, data, PAK_CHUNK_SIZE);
        *dcrc = ~pak_data_crc(tmp);
    }
}

static void standard_controller_reset(struct game_controller* cont)
{
    cont->status = 0x00;

    if (cont->ipak != NULL) {
        cont->status |= CONT_STATUS_PAK_PRESENT;
    } else {
        cont->status |= CONT_STATUS_PAK_CHANGED;
    }
}

static void mouse_controller_reset(struct game_controller* cont)
{
    cont->status = 0x00;
}

const struct game_controller_flavor g_standard_controller_flavor =
{
    "Standard controller",
    JDT_JOY_ABS_COUNTERS | JDT_JOY_PORT,
    standard_controller_reset
};

const struct game_controller_flavor g_mouse_controller_flavor =
{
    "Mouse controller",
    JDT_JOY_REL_COUNTERS,
    mouse_controller_reset
};

static void poweron_game_controller(void* jbd)
{
    struct game_controller* cont = (struct game_controller*)jbd;

    if (cont->flavor != NULL)
        cont->flavor->reset(cont);

    if (cont->flavor == &g_standard_controller_flavor && cont->ipak != NULL) {
        cont->ipak->plug(cont->pak);
    }
}

static void process_controller_command_joybus(void* jbd,
    const uint8_t* tx, const uint8_t* tx_buf,
    uint8_t* rx, uint8_t* rx_buf)
{
    struct game_controller* cont = (struct game_controller*)jbd;
    uint32_t input_ = 0;
    uint8_t cmd = tx_buf[0];

    /* if controller can't be polled, consider it absent */
    if (cont->icin == NULL
     || cont->icin->get_input(cont->cin, &input_) != M64ERR_SUCCESS) {
        *rx |= 0x80;
        return;
    }

    switch (cmd)
    {
    case JCMD_RESET:
        if (cont->flavor != NULL)
            cont->flavor->reset(cont);
        /* fall through */
    case JCMD_STATUS: {
        JOYBUS_CHECK_COMMAND_FORMAT(1, 3)

        rx_buf[0] = (uint8_t)(cont->flavor->type >> 0);
        rx_buf[1] = (uint8_t)(cont->flavor->type >> 8);
        rx_buf[2] = cont->status;
    } break;

    case JCMD_CONTROLLER_READ: {
        JOYBUS_CHECK_COMMAND_FORMAT(1, 4)

        *((uint32_t*)(rx_buf)) = input_;
    } break;

    case JCMD_PAK_READ: {
        JOYBUS_CHECK_COMMAND_FORMAT(3, 33)
        jb_pak_read_block(cont, &tx_buf[1], &rx_buf[0], &rx_buf[32]);
    } break;

    case JCMD_PAK_WRITE: {
        JOYBUS_CHECK_COMMAND_FORMAT(35, 1)
        jb_pak_write_block(cont, &tx_buf[1], &tx_buf[3], &rx_buf[0]);
    } break;

    default:
        DebugMessage(M64MSG_WARNING, "cont: Unknown command %02x %02x %02x",
            *tx, *rx, cmd);
    }
}

const struct joybus_device_interface g_ijoybus_device_controller =
{
    poweron_game_controller,
    process_controller_command_joybus,
    NULL
};
