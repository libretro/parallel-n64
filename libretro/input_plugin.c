/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-input-sdl - plugin.c                                      *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2008-2011 Richard Goedeken                              *
 *   Copyright (C) 2008 Tillin9                                            *
 *   Copyright (C) 2002 Blight                                             *
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
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

extern retro_environment_t environ_cb;
extern retro_input_state_t input_cb;
extern struct retro_rumble_interface rumble;
extern int pad_pak_types[4];
extern uint16_t button_orientation;

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_common.h"
#include "m64p_config.h"

#include "../mupen64plus-core/src/main/rom.h"
extern m64p_rom_header ROM_HEADER;

// Some stuff from n-rage plugin
#define RD_GETSTATUS        0x00        // get status
#define RD_READKEYS         0x01        // read button values
#define RD_READPAK          0x02        // read from controllerpack
#define RD_WRITEPAK         0x03        // write to controllerpack
#define RD_RESETCONTROLLER  0xff        // reset controller
#define RD_READEEPROM       0x04        // read eeprom
#define RD_WRITEEPROM       0x05        // write eeprom

#define PAK_IO_RUMBLE       0xC000      // the address where rumble-commands are sent to

#define FRAME_DURATION 24

/* global data definitions */
struct
{
    CONTROL *control;               // pointer to CONTROL struct in Core library
    BUTTONS buttons;
} controller[4];

static void inputGetKeys_default( int Control, BUTTONS *Keys );
typedef void (*get_keys_t)(int, BUTTONS*);
get_keys_t getKeys = inputGetKeys_default;

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL inputPluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
   getKeys = inputGetKeys_default;
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL inputPluginShutdown(void)
{
    abort();
    return 0;
}

EXPORT m64p_error CALL inputPluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    // This function should never be called in libretro version                    
    return M64ERR_SUCCESS;
}

static unsigned char DataCRC( unsigned char *Data, int iLenght )
{
    unsigned char Remainder = Data[0];

    int iByte = 1;
    unsigned char bBit = 0;

    while( iByte <= iLenght )
    {
        int HighBit = ((Remainder & 0x80) != 0);
        Remainder = Remainder << 1;

        Remainder += ( iByte < iLenght && Data[iByte] & (0x80 >> bBit )) ? 1 : 0;

        Remainder ^= (HighBit) ? 0x85 : 0;

        bBit++;
        iByte += bBit/8;
        bBit %= 8;
    }

    return Remainder;
}

/******************************************************************
  Function: ControllerCommand
  Purpose:  To process the raw data that has just been sent to a
            specific controller.
  input:    - Controller Number (0 to 3) and -1 signalling end of
              processing the pif ram.
            - Pointer of data to be processed.
  output:   none

  note:     This function is only needed if the DLL is allowing raw
            data, or the plugin is set to raw

            the data that is being processed looks like this:
            initilize controller: 01 03 00 FF FF FF
            read controller:      01 04 01 FF FF FF FF
*******************************************************************/
EXPORT void CALL inputControllerCommand(int Control, unsigned char *Command)
{
    unsigned char *Data = &Command[5];

    if (Control == -1)
        return;

    switch (Command[2])
    {
        case RD_GETSTATUS:
            break;
        case RD_READKEYS:
            break;
        case RD_READPAK:
            if (controller[Control].control->Plugin == PLUGIN_RAW)
            {
                unsigned int dwAddress = (Command[3] << 8) + (Command[4] & 0xE0);

                if(( dwAddress >= 0x8000 ) && ( dwAddress < 0x9000 ) )
                    memset( Data, 0x80, 32 );
                else
                    memset( Data, 0x00, 32 );

                Data[32] = DataCRC( Data, 32 );
            }
            break;
        case RD_WRITEPAK:
            if (controller[Control].control->Plugin == PLUGIN_RAW)
            {
                unsigned int dwAddress = (Command[3] << 8) + (Command[4] & 0xE0);
                Data[32] = DataCRC( Data, 32 );
                
                if ((dwAddress == PAK_IO_RUMBLE) && (rumble.set_rumble_state))
                {
                    if (*Data)
                    {
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_WEAK, 0xFFFF);
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_STRONG, 0xFFFF);
                    }
                    else
                    {
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_WEAK, 0);
                        rumble.set_rumble_state(Control, RETRO_RUMBLE_STRONG, 0);
                    }
                }
            }
            
            break;
        case RD_RESETCONTROLLER:
            break;
        case RD_READEEPROM:
            break;
        case RD_WRITEEPROM:
            break;
        }
}

/******************************************************************
  Function: GetKeys
  Purpose:  To get the current state of the controllers buttons.
  input:    - Controller Number (0 to 3)
            - A pointer to a BUTTONS structure to be filled with
            the controller state.
  output:   none
*******************************************************************/

// System analog stick range -0x8000 to 0x8000
#define ASTICK_MAX 0x8000
#define ASTICK_DEADZONE 0x2000
#define CSTICK_DEADZONE 0x4000

#define CSTICK_RIGHT 0x200
#define CSTICK_LEFT 0x100
#define CSTICK_UP 0x800
#define CSTICK_DOWN 0x400

int timeout = 0;

extern void inputInitiateCallback(const char *headername);

static void inputGetKeys_reuse(int16_t analogX, int16_t analogY, int Control, BUTTONS* Keys)
{
   //  Keys->Value |= input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_XX)    ? 0x4000 : 0; // Mempak switch
   //  Keys->Value |= input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_XX)    ? 0x8000 : 0; // Rumblepak switch
   
   // Analog stick range is from -80 to 80
   analogX = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
   analogY = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);

   if (abs(analogX) > ASTICK_DEADZONE)
   {
      int sign = (analogX > 0) - (analogX < 0);
      // Rescale the analog stick range to negate the deadzone (makes slow movements possible)
      float val = ((analogX - sign * ASTICK_DEADZONE)*(ASTICK_MAX/(ASTICK_MAX - ASTICK_DEADZONE)));
      // Scale the analog stick value down to N64 range
      val *= 80.0f / ASTICK_MAX;
      Keys->X_AXIS = (int32_t)val;
   }
   else
      Keys->X_AXIS = 0;

   if (abs(analogY) > ASTICK_DEADZONE)
   {
      int sign = (analogY > 0) - (analogY < 0);
      float val = ((analogY - sign * ASTICK_DEADZONE)*(ASTICK_MAX/(ASTICK_MAX - ASTICK_DEADZONE)));
      val *= 80.0f / ASTICK_MAX;
      Keys->Y_AXIS = -(int32_t)val;
   }
   else
      Keys->Y_AXIS = 0;

   if (input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT) && --timeout <= 0)
      inputInitiateCallback(ROM_HEADER.Name);
}


static void inputGetKeys_6ButtonFighters(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   
   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_XENA(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Biofreaks(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_DarkRift(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_ISS(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Mace(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MischiefMakers(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MKTrilogy(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;

   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MK4(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MKMythologies(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Rampage(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Ready2Rumble(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Wipeout64(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_WWF(int Control, BUTTONS *Keys)
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);


   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_RR64( int Control, BUTTONS *Keys )
{
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);

   //Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   //Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   //Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_default( int Control, BUTTONS *Keys )
{
   bool hold_cstick;
   int16_t analogX, analogY;

   Keys->Value = 0;
   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   hold_cstick = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   if (hold_cstick)
   {
      Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
      Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
      Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
      Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   }
   else
   {
      if (button_orientation == 1)
      {
         Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
         Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
      }
      else
      {
         Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
         Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
      }
   }

   // C buttons
   analogX = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
   analogY = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

   if (abs(analogX) > CSTICK_DEADZONE)
      Keys->Value |= (analogX < 0) ? CSTICK_RIGHT : CSTICK_LEFT;

   if (abs(analogY) > CSTICK_DEADZONE)
      Keys->Value |= (analogY < 0) ? CSTICK_UP : CSTICK_DOWN;

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

EXPORT void CALL inputGetKeys( int Control, BUTTONS *Keys )
{
   if (Keys != NULL && getKeys)
      getKeys(Control, Keys);
}


void inputInitiateCallback(const char *headername)
{
   struct retro_message msg; 
   char msg_local[256];

   if (getKeys != &inputGetKeys_default)
   {
      getKeys = inputGetKeys_default;
      snprintf(msg_local, sizeof(msg_local), "Controls: Default");
      msg.msg = msg_local;
      msg.frames = FRAME_DURATION;
      timeout = FRAME_DURATION / 2;
      if (environ_cb)
         environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
      return;
   }

    if (
             (strcmp(headername, "KILLER INSTINCT GOLD") == 0) ||
          (strcmp(headername, "Killer Instinct Gold") == 0) ||
          (strcmp(headername, "CLAYFIGHTER 63") == 0) ||
          (strcmp(headername, "Clayfighter SC") == 0) ||
          (strcmp(headername, "RAKUGAKIDS") == 0))
       getKeys = inputGetKeys_6ButtonFighters;
    else if (strcmp(headername, "BIOFREAKS") == 0)
       getKeys = inputGetKeys_Biofreaks;
    else if (strcmp(headername, "DARK RIFT") == 0)
       getKeys = inputGetKeys_DarkRift;
    else if (strcmp(headername, "XENAWARRIORPRINCESS") == 0)
       getKeys = inputGetKeys_XENA;
    else if (strcmp(headername, "RIDGE RACER 64") == 0)
       getKeys = inputGetKeys_RR64;
   else if ((strcmp(headername, "I S S 64") == 0) ||
         (strcmp(headername, "J WORLD SOCCER3") == 0) ||
         (strcmp(headername, "J.WORLD CUP 98") == 0) ||
         (strcmp(headername, "I.S.S.98") == 0) ||
         (strcmp(headername, "PERFECT STRIKER2") == 0) ||
         (strcmp(headername, "I.S.S.2000") == 0))
      getKeys = inputGetKeys_ISS;
    else if (strcmp(headername, "MACE") == 0)
       getKeys = inputGetKeys_Mace;
    else if ((strcmp(headername, "MISCHIEF MAKERS") == 0) ||
          (strcmp(headername, "TROUBLE MAKERS") == 0))
       getKeys = inputGetKeys_MischiefMakers;
   else if ((strcmp(headername, "MortalKombatTrilogy") == 0) ||
         (strcmp(headername, "WAR GODS") == 0))
       getKeys = inputGetKeys_MKTrilogy;
   else if (strcmp(headername, "MORTAL KOMBAT 4") == 0)
       getKeys = inputGetKeys_MK4;
   else if (strcmp(headername, "MK_MYTHOLOGIES") == 0)
       getKeys = inputGetKeys_MKMythologies;
   else if ((strcmp(headername, "RAMPAGE") == 0) ||
         (strcmp(headername, "RAMPAGE2") == 0))
       getKeys = inputGetKeys_Rampage;
   else if ((strcmp(headername, "READY 2 RUMBLE") == 0) ||
         (strcmp(headername, "Ready to Rumble") == 0))
       getKeys = inputGetKeys_Ready2Rumble;
   else if (strcmp(headername, "Wipeout 64") == 0)
       getKeys = inputGetKeys_Wipeout64;
   else if ((strcmp(headername, "WRESTLEMANIA 2000") == 0) ||
         (strcmp(headername, "WWF No Mercy") == 0))
       getKeys = inputGetKeys_WWF;

   if (getKeys == &inputGetKeys_default)
      return;

   snprintf(msg_local, sizeof(msg_local), "Controls: Alternate");
   msg.msg = msg_local;
   msg.frames = FRAME_DURATION;
   timeout = FRAME_DURATION / 2;
   if (environ_cb)
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
}


/******************************************************************
  Function: InitiateControllers
  Purpose:  This function initialises how each of the controllers
            should be handled.
  input:    - The handle to the main window.
            - A controller structure that needs to be filled for
              the emulator to know how to handle each controller.
  output:   none
*******************************************************************/
EXPORT void CALL inputInitiateControllers(CONTROL_INFO ControlInfo)
{
    int i;

    for( i = 0; i < 4; i++ )
    {
       controller[i].control = ControlInfo.Controls + i;
       controller[i].control->Present = 1;
       controller[i].control->RawData = 0;
       
       if (pad_pak_types[i] == PLUGIN_MEMPAK)
          controller[i].control->Plugin = PLUGIN_MEMPAK;
       else if (pad_pak_types[i] == PLUGIN_RAW)
          controller[i].control->Plugin = PLUGIN_RAW;
       else
          controller[i].control->Plugin = PLUGIN_NONE;
    }

}

/******************************************************************
  Function: ReadController
  Purpose:  To process the raw data in the pif ram that is about to
            be read.
  input:    - Controller Number (0 to 3) and -1 signalling end of
              processing the pif ram.
            - Pointer of data to be processed.
  output:   none
  note:     This function is only needed if the DLL is allowing raw
            data.
*******************************************************************/
EXPORT void CALL inputReadController(int Control, unsigned char *Command)
{
   inputControllerCommand(Control, Command);
}

/******************************************************************
  Function: RomClosed
  Purpose:  This function is called when a rom is closed.
  input:    none
  output:   none
*******************************************************************/
EXPORT void CALL inputRomClosed(void) { }

/******************************************************************
  Function: RomOpen
  Purpose:  This function is called when a rom is open. (from the
            emulation thread)
  input:    none
  output:   none
*******************************************************************/
EXPORT int CALL inputRomOpen(void) { return 1; }

