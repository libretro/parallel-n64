/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - game_controller.h                                       *
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

#ifndef M64P_SI_GAME_CONTROLLER_H
#define M64P_SI_GAME_CONTROLLER_H

#include <stdint.h>
#include <stddef.h>

#include "mempak.h"
#include "rumblepak.h"
#include "transferpak.h"
#include "biopak.h"

#include "../../api/m64p_plugin.h"
#include "../../backends/api/joybus.h"

struct game_controller;
struct controller_input_backend_interface;

enum pak_type
{
    PAK_NONE,
    PAK_MEM,
    PAK_RUMBLE,
    PAK_TRANSFER,
    PAK_BIO
};

enum cont_type
{
    CONT_NONE = 0,
    CONT_JOYPAD = 1,
    CONT_MOUSE = 2,
    CONT_GCN = 3,
};

/* mupen64plus-next controller polymorphism (flavor) descriptor. Imported as part
 * of the joybus controller convergence (region 12c). */
struct game_controller_flavor
{
    const char* name;
    uint16_t type;

    /* controller reset procedure */
    void (*reset)(struct game_controller* cont);
};

/* mupen64plus-next generic pak vtable. parallel-n64 keeps its concrete nested
 * paks (mempak/rumblepak/transferpak/biopak) for its existing dispatch; this
 * interface lets the joybus controller device address a pak polymorphically. */
struct pak_interface
{
    const char* name;
    void (*plug)(void* pak);
    void (*unplug)(void* pak);
    void (*read)(void* pak, uint16_t address, uint8_t* data, size_t size);
    void (*write)(void* pak, uint16_t address, const uint8_t* data, size_t size);
};

struct game_controller
{
    /* external controller input (parallel-n64's existing callback model) */
    void* user_data;
    int (*is_connected)(void*,enum pak_type*);
    uint32_t (*get_input)(void*);
    BUTTONS_GCN (*get_gcn_input)(void*, int analogMode);

    struct mempak mempak;
    struct rumblepak rumblepak;
    struct transferpak transferpak;
    struct biopak biopak;

    /* mupen64plus-next joybus-device state (region 12c). These coexist with the
     * fields above: pn64's flat process_controller_command keeps using the
     * callbacks/nested paks, while the joybus g_ijoybus_device_controller path
     * uses these. */
    uint8_t status;
    const struct game_controller_flavor* flavor;

    void* cin;
    const struct controller_input_backend_interface* icin;

    void* pak;
    const struct pak_interface* ipak;

    /* VRU */
    uint8_t voice_state;
    uint8_t load_offset;
    uint8_t voice_init;
    uint16_t word[40];
};

void init_game_controller(struct game_controller *cont,
      void *cont_user_data,
      int (*cont_is_connected)(void*,enum pak_type*),
      uint32_t (*cont_get_input)(void*),
      BUTTONS_GCN (*cont_get_gcn_input)(void*, int),
      void* mpk_user_data,
      void (*mpk_save)(void*),
      uint8_t* mpk_data,
      void* rpk_user_data,
      void (*rpk_rumble)(void*,enum rumble_action));

int game_controller_is_connected(struct game_controller* cont, enum pak_type* pak);
uint32_t game_controller_get_input(struct game_controller* cont);
BUTTONS_GCN game_controller_gcn_get_input(struct game_controller* cont, int analogMode);

void process_controller_command(struct game_controller* cont, uint8_t* cmd);
void read_controller(struct game_controller* cont, uint8_t* cmd);

/* mupen64plus-next joybus controller device + flavors (region 12c). The joybus
 * process handler coexists with pn64's flat process_controller_command above;
 * it is wired into the PIF channel dispatch in the PIF convergence step. */
extern const struct joybus_device_interface
    g_ijoybus_device_controller;

extern const struct game_controller_flavor g_standard_controller_flavor;
extern const struct game_controller_flavor g_mouse_controller_flavor;

#endif
