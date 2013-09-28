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

extern retro_input_state_t input_cb;
extern struct retro_rumble_interface rumble;
extern int pad_pak_types[4];

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_common.h"
#include "m64p_config.h"

// Some stuff from n-rage plugin
#define RD_GETSTATUS        0x00        // get status
#define RD_READKEYS         0x01        // read button values
#define RD_READPAK          0x02        // read from controllerpack
#define RD_WRITEPAK         0x03        // write to controllerpack
#define RD_RESETCONTROLLER  0xff        // reset controller
#define RD_READEEPROM       0x04        // read eeprom
#define RD_WRITEEPROM       0x05        // write eeprom

#define PAK_IO_RUMBLE       0xC000      // the address where rumble-commands are sent to

/* global data definitions */
struct
{
    CONTROL *control;               // pointer to CONTROL struct in Core library
    BUTTONS buttons;
} controller[4];

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL inputPluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
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

#define ASTICK_DEADZONE 0x2000
#define CSTICK_DEADZONE 0x4000

EXPORT void CALL inputGetKeys( int Control, BUTTONS *Keys )
{
    Keys->Value = 0;
    Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

    Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

    Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
    Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
    Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

    bool hold_cstick = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
    if (hold_cstick)
    {
       Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
       Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
       Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
       Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    }
    else
    {
        Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    }

//  Keys->Value |= input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_XX)    ? 0x4000 : 0; // Mempak switch
//  Keys->Value |= input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_XX)    ? 0x8000 : 0; // Rumblepak switch

    // Analog stick range is from -80 to 80
    int16_t analogX = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
    int16_t analogY = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);

    if (abs(analogX) > ASTICK_DEADZONE)
    {
         float val = 161.0f * ((analogX + 32768) / 65536.0f);
         Keys->X_AXIS = ((int32_t)val) - 80;
    }
    else
        Keys->X_AXIS = 0;

    if (abs(analogY) > ASTICK_DEADZONE)
    {
         float val = 161.0f * ((analogY + 32768) / 65536.0f);
         Keys->Y_AXIS = 0 - (((int32_t)val) - 80);
    }
    else
        Keys->Y_AXIS = 0;

    // C buttons
    analogX = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
    analogY = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

    if (abs(analogX) > CSTICK_DEADZONE)
        Keys->Value |= (analogX < 0) ? 0x200 : 0x100;

    if (abs(analogY) > CSTICK_DEADZONE)
        Keys->Value |= (analogY < 0) ? 0x800 : 0x400;
        
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
EXPORT void CALL inputReadController(int Control, unsigned char *Command) { }

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

