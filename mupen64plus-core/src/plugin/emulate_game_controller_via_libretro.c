/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - emulate_game_controller_via_input_plugin.c              *
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

#include "emulate_game_controller_via_input_plugin.h"
#include "plugin/plugin.h"

#include "api/m64p_plugin.h"
#include "device/aleck64/aleck64.h"
#include "device/controllers/game_controller.h"
#include <libretro.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* snprintf not available in MSVC 2010 and earlier */
#include "api/msvc_compat.h"

extern retro_environment_t environ_cb;
extern retro_input_state_t input_cb;
extern struct retro_rumble_interface rumble;
extern int pad_pak_types[4];
extern int pad_present[4];
extern int astick_deadzone;
extern int astick_sensitivity;
extern int astick_snap_active;
extern int astick_snap_max_angle;
extern int astick_snap_min_displacement_percent;
extern int r_cbutton;
extern int l_cbutton;
extern int d_cbutton;
extern int u_cbutton;
extern bool alternate_mapping;
extern bool mouse_mode;
extern int mouse_sensitivity_x;
extern int mouse_sensitivity_y;
extern int mouse_left_btn;
extern int mouse_right_btn;
extern int mouse_middle_btn;
extern int mouse_wheel_up_btn;
extern int mouse_wheel_down_btn;
static bool libretro_supports_bitmasks = false;

extern m64p_rom_header ROM_HEADER;

/* Controller presence values, as written by libretro.c into pad_present[]
 * (the new-core game_controller.h no longer defines them). */
#ifndef CONT_MOUSE
#define CONT_MOUSE 2
#endif

/* Frames a "Controls: ..." message stays up; also the debounce for the
 * SELECT-driven per-game control profile toggle. */
#define FRAME_DURATION 24
static int timeout = 0;

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

static void inputGetKeys_default( int Control, BUTTONS *Keys );
typedef void (*get_keys_t)(int, BUTTONS*);
static get_keys_t getKeys = inputGetKeys_default;

void inputInitiateCallback(const char *headername);

/* Entry point for the core's controller input backend: polls through the
 * currently selected control profile (default, or a per-game alternate
 * layout chosen by inputInitiateCallback). */
void inputGetKeys(int Control, BUTTONS *Keys)
{
   getKeys(Control, Keys);
}

void inputGetKeys_default_descriptor(void)
{
   if (alternate_mapping)
   {
      #define independent_cbuttons_map(PAD) \
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "A Button" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "B Button" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "C-Right" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "C-Left" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "C-Down" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "C-Up" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Z Trigger" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "R Shoulder" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "L Shoulder" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Control Stick X" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Control Stick Y" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "C Buttons X" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "C Buttons Y" },

      static struct retro_input_descriptor desc[] = {
         independent_cbuttons_map(0)
         independent_cbuttons_map(1)
         independent_cbuttons_map(2)
         independent_cbuttons_map(3)
         { 0 },
      };
      environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
   }
   else
   {
      #define standard_map(PAD) \
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "A Button (C3)" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "B Button (C2)" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "(C1)" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "(C4)" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "C Buttons Mode" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Z Trigger" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "R Shoulder" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "L Shoulder" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },\
      { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Control Stick X" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Control Stick Y" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "C Buttons X" },\
      { PAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "C Buttons Y" },

      static struct retro_input_descriptor desc[] = {
         standard_map(0)
         standard_map(1)
         standard_map(2)
         standard_map(3)
         { 0 },
      };
      environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
   }
}

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL inputPluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;
   getKeys = inputGetKeys_default;
   inputGetKeys_default_descriptor();
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL inputPluginShutdown(void)
{
   libretro_supports_bitmasks = false;
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

// System analog stick range is -0x8000 to 0x8000
#define ASTICK_MAX 0x8000
#define CSTICK_DEADZONE 0x4000

#define CSTICK_RIGHT 0x200
#define CSTICK_LEFT 0x100
#define CSTICK_UP 0x800
#define CSTICK_DOWN 0x400


/* ----------------------------------------------------------------------
 *  Deterministic fixed-point analog deadzone
 *
 *  The main N64 stick deadzone ran in float through a polar
 *  sqrt/atan2/sin/cos round-trip, feeding the emulated controller axes.
 *  float sqrt/atan2/trig is not bit-reproducible across architectures or
 *  libm implementations, so two netplay peers could derive different
 *  axis values from identical stick input and desync.
 *
 *  The polar round-trip collapses to a plain radial scale: since
 *  cos(atan2(y,x)) == x / radius and sin(atan2(y,x)) == y / radius, the
 *  rescaled cartesian output is just (x,y) * radius_final / radius, with
 *  radius_final = (radius - dz) * 80 * sensitivity / ((ASTICK_MAX - dz)
 *  * 100) (the ASTICK_MAX of the deadzone rescale cancels the 1/ASTICK_MAX
 *  of the N64 range conversion).  No trig, one integer sqrt, fully
 *  deterministic.  The old ROUND() was floor(v + 0.5), reproduced here as
 *  a floor-division of (2*num + den) / (2*den).
 * -------------------------------------------------------------------- */

/* 64-bit integer square root (binary, no FPU); returns floor(sqrt(x)). */
static uint32_t analog_isqrt64(uint64_t x)
{
   uint64_t res = 0;
   uint64_t bit = (uint64_t)1 << 62;

   while (bit > x)
      bit >>= 2;

   while (bit != 0)
   {
      if (x >= res + bit)
      {
         x   -= res + bit;
         res  = (res >> 1) + bit;
      }
      else
         res >>= 1;
      bit >>= 2;
   }
   return (uint32_t)res;
}

/* floor(a / b) for b > 0 (true floor, toward -inf; C '/' truncates). */
static int32_t analog_floor_div(int64_t a, int64_t b)
{
   int64_t q = a / b;
   if ((a % b) != 0 && a < 0)
      q--;
   return (int32_t)q;
}

/* ----------------------------------------------------------------------
 *  Deterministic angle snapping ("Snap Controller Angle" core option)
 *
 *  Circular-gate controllers cannot hit the N64 stick's cardinal/diagonal
 *  extremes cleanly; snapping pulls a deflection that is within
 *  astick_snap_max_angle degrees of a multiple of 45 onto that multiple,
 *  keeping its radius, once the stick is past
 *  astick_snap_min_displacement_percent of its travel.
 *
 *  The original did this with atan2/round/cos/sin in float, rounding the
 *  angle to a whole degree first, so it snapped anything strictly inside
 *  (max_angle + 0.5) degrees of a multiple of 45.  The angle test only
 *  needs the ratio of the two axis magnitudes against a fixed tangent, so
 *  it is done here with a 16.16 tangent table in half-degree steps
 *  (0..45) and integer compares, and the snapped vector is rebuilt from
 *  the integer radius: (radius, 0) for a cardinal, (radius/sqrt2,
 *  radius/sqrt2) for a diagonal.  No libm, bit-reproducible across peers.
 * -------------------------------------------------------------------- */
static const uint32_t analog_tan_half_deg_16_16[91] = {
        0,    572,   1144,   1716,   2289,   2861,   3435,   4008,   4583,   5158,
     5734,   6310,   6888,   7467,   8047,   8628,   9210,   9794,  10380,  10967,
    11556,  12146,  12739,  13333,  13930,  14529,  15130,  15734,  16340,  16949,
    17560,  18175,  18792,  19413,  20036,  20663,  21294,  21928,  22566,  23208,
    23853,  24503,  25157,  25815,  26478,  27146,  27818,  28496,  29179,  29866,
    30560,  31259,  31964,  32675,  33392,  34116,  34846,  35583,  36327,  37078,
    37837,  38604,  39378,  40161,  40951,  41751,  42560,  43377,  44205,  45042,
    45889,  46746,  47615,  48494,  49385,  50288,  51202,  52130,  53070,  54024,
    54991,  55973,  56970,  57981,  59009,  60053,  61113,  62191,  63287,  64402,
    65536
};
#define ANALOG_INV_SQRT2_16_16 46341

static void analog_snap_angle(int32_t *px, int32_t *py, uint32_t radius)
{
   int32_t  x = *px, y = *py;
   uint32_t ax = (uint32_t)(x < 0 ? -x : x);
   uint32_t ay = (uint32_t)(y < 0 ? -y : y);
   uint32_t hi = ax > ay ? ax : ay;
   uint32_t lo = ax > ay ? ay : ax;
   int      max_angle;
   int64_t  disp_num, disp_den;

   if (!astick_snap_active || radius == 0)
      return;

   /* Displacement gate: 100 * (radius - dz) / (ASTICK_MAX - dz) >= pct,
    * i.e. the deadzone-rescaled radius as a percentage of full travel. */
   disp_num = (int64_t)100 * ((int64_t)radius - astick_deadzone);
   disp_den = (int64_t)astick_snap_min_displacement_percent
            * (ASTICK_MAX - astick_deadzone);
   if (disp_num < disp_den)
      return;

   max_angle = astick_snap_max_angle;
   if (max_angle < 0)
      max_angle = 0;
   if (max_angle > 22)   /* beyond 22.5 the cardinal/diagonal windows overlap */
      max_angle = 22;

   /* Within max_angle of a cardinal: atan(lo/hi) < max_angle + 0.5 */
   if ((uint64_t)lo << 16 < (uint64_t)hi * analog_tan_half_deg_16_16[2 * max_angle + 1])
   {
      if (ax > ay)
      {
         *px = x < 0 ? -(int32_t)radius : (int32_t)radius;
         *py = 0;
      }
      else
      {
         *px = 0;
         *py = y < 0 ? -(int32_t)radius : (int32_t)radius;
      }
      return;
   }

   /* Within max_angle of a diagonal: atan(lo/hi) > 44.5 - max_angle */
   if ((uint64_t)lo << 16 > (uint64_t)hi * analog_tan_half_deg_16_16[89 - 2 * max_angle])
   {
      int32_t d = (int32_t)(((uint64_t)radius * ANALOG_INV_SQRT2_16_16) >> 16);
      *px = x < 0 ? -d : d;
      *py = y < 0 ? -d : d;
   }
}

static void apply_mouse_button(BUTTONS* Keys, int btn_mapping)
{
   switch (btn_mapping)
   {
      case 1:  Keys->Z_TRIG       = 1; break;
      case 2:  Keys->A_BUTTON     = 1; break;
      case 3:  Keys->B_BUTTON     = 1; break;
      case 4:  Keys->L_TRIG       = 1; break;
      case 5:  Keys->R_TRIG       = 1; break;
      case 6:  Keys->START_BUTTON = 1; break;
      case 7:  Keys->U_CBUTTON    = 1; break;
      case 8:  Keys->D_CBUTTON    = 1; break;
      case 9:  Keys->L_CBUTTON    = 1; break;
      case 10: Keys->R_CBUTTON    = 1; break;
      default: break;
   }
}

/* Deadzone, snap and radial scale of a raw [-0x8000, 0x8000) stick into
 * [-maximum, maximum] (80 for the N64 stick; the GameCube modes use 100
 * and 95).  Re-scales to negate the deadzone and applies sensitivity as a
 * single rational radial scale:
 *   out = (x,y) * (radius - dz) * maximum * sens
 *              / (radius * (ASTICK_MAX - dz) * 100)
 * ROUND(v) == floor(v + 0.5) == floor_div(2*num + den, 2*den).
 * outY is returned already negated (N64 up is positive). */
static void analog_scale(int32_t x, int32_t y, int maximum, int32_t* outX, int32_t* outY)
{
   uint32_t radius;
   int32_t  sx = x, sy = y;

   // Integer stick radius (replaces the polar sqrt/atan2 round-trip)
   radius = analog_isqrt64( (uint64_t)((int64_t)x * x)
                          + (uint64_t)((int64_t)y * y) );

   analog_snap_angle(&sx, &sy, radius);

   if ((int)radius > astick_deadzone)
   {
      int denom_dz = ASTICK_MAX - astick_deadzone;
      int64_t num_scale, den, nx, ny;
      if (denom_dz < 1)
         denom_dz = 1;

      num_scale = (int64_t)((int)radius - astick_deadzone) * maximum * astick_sensitivity;
      den       = (int64_t)radius * denom_dz * 100;

      nx = (int64_t)sx * num_scale;
      ny = (int64_t)sy * num_scale;

      *outX = +analog_floor_div(2 * nx + den, 2 * den);
      *outY = -analog_floor_div(2 * ny + den, 2 * den);
   }
   else
   {
      *outX = 0;
      *outY = 0;
   }
}

/* Shared tail of every control profile: N64 stick, mouse-to-stick mode,
 * d-pad, Start, and the SELECT-driven profile toggle. */
static void inputGetKeys_reuse(int16_t analogX, int16_t analogY, int Control, BUTTONS* Keys)
{
   int32_t sx, sy;
   //  Keys->Value |= input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_XX)    ? 0x4000 : 0; // Mempak switch
   //  Keys->Value |= input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_XX)    ? 0x8000 : 0; // Rumblepak switch

   analogX = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
   analogY = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);

   analog_scale(analogX, analogY, 80, &sx, &sy);
   Keys->X_AXIS = sx;
   Keys->Y_AXIS = sy;

   /* Mouse-to-analog-stick mode (player 1 only): the mouse drives the
    * stick whenever the real stick is neutral. Deltas are scaled by
    * sensitivity percent (negative inverts the axis; the Y default is
    * negative because positive mouse Y is downward) and clamped to the
    * N64 cardinal range. Integer arithmetic only: delta * sensitivity
    * stays well inside 32 bits and truncates toward zero exactly like
    * the float expression it replaces. */
   if (mouse_mode && Control == 0 && Keys->X_AXIS == 0 && Keys->Y_AXIS == 0)
   {
      int stickX = (int)input_cb(Control, RETRO_DEVICE_MOUSE, 0,
            RETRO_DEVICE_ID_MOUSE_X) * mouse_sensitivity_x / 50;
      int stickY = (int)input_cb(Control, RETRO_DEVICE_MOUSE, 0,
            RETRO_DEVICE_ID_MOUSE_Y) * mouse_sensitivity_y / 50;

      Keys->X_AXIS = (stickX > 80) ? 80 : (stickX < -80) ? -80 : stickX;
      Keys->Y_AXIS = (stickY > 80) ? 80 : (stickY < -80) ? -80 : stickY;

      if (input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
         apply_mouse_button(Keys, mouse_left_btn);
      if (input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
         apply_mouse_button(Keys, mouse_right_btn);
      if (input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
         apply_mouse_button(Keys, mouse_middle_btn);
      if (input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
         apply_mouse_button(Keys, mouse_wheel_up_btn);
      if (input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
         apply_mouse_button(Keys, mouse_wheel_down_btn);
   }

   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   /* Some Aleck64 games (Eleven Beat) probe the pad's d-pad to detect the
    * cabinet joystick type and error out unless all four bits read held,
    * like ares' dpadDisabled game config does. */
   if (g_aleck64_dpad_disabled)
   {
      Keys->R_DPAD = 1;
      Keys->L_DPAD = 1;
      Keys->D_DPAD = 1;
      Keys->U_DPAD = 1;
   }

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   /* SELECT toggles between the default and the per-game alternate
    * control profile (only meaningful when a game has one). In the
    * independent-C-button layout SELECT is L, so leave it alone there. */
   if (!alternate_mapping && input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT) && --timeout <= 0)
      inputInitiateCallback((const char*)ROM_HEADER.Name);
}

static void inputGetKeys_6ButtonFighters(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Biofreaks(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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

static void inputGetKeys_ISS(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Mace(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MischiefMakers(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MKTrilogy(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_MK4(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_Ready2Rumble(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_WWF(int Control, BUTTONS *Keys)
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->D_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   Keys->L_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   Keys->R_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   Keys->U_CBUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   Keys->L_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   Keys->R_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

   inputGetKeys_reuse(analogX, analogY, Control, Keys);
}

static void inputGetKeys_RR64( int Control, BUTTONS *Keys )
{
   int16_t analogX = 0;
   int16_t analogY = 0;
   Keys->Value = 0;

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

static void inputGetKeys_mouse( int Control, BUTTONS *Keys )
{
   int mouseX = 0;
   int mouseY = 0;
   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)  != 0;
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)  != 0;
   mouseX = input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
   mouseY = -input_cb(Control, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

   if (mouseX > 127)
      mouseX = 127;
   if (mouseY > 127)
      mouseY = 127;
   if (mouseX < -128)
      mouseX = -128;
   if (mouseY < -128)
      mouseY = -128;

   Keys->X_AXIS = mouseX;
   Keys->Y_AXIS = mouseY;
}

static void inputGetKeys_default( int Control, BUTTONS *Keys )
{
   unsigned i;
   bool cbuttons_mode = false;
   int16_t ret        = 0;
   int16_t analogX    = 0;
   int16_t analogY    = 0;
   Keys->Value        = 0;

   if (controller[Control].control->Present == CONT_MOUSE)
   {
      inputGetKeys_mouse(Control, Keys);
      return;
   }

   if (libretro_supports_bitmasks)
      ret = input_cb(Control, RETRO_DEVICE_JOYPAD,
            0, RETRO_DEVICE_ID_JOYPAD_MASK);
   else
   {
      for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
      {
         if (input_cb(Control, RETRO_DEVICE_JOYPAD, 0, i))
            ret |= (1 << i);
      }
   }

   Keys->Z_TRIG       = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2)));

   if (alternate_mapping)
   {
      Keys->A_BUTTON  = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)));
      Keys->B_BUTTON  = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)));
      Keys->R_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)));
      Keys->L_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)));
      Keys->D_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_A)));
      Keys->U_CBUTTON = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_X)));
      Keys->R_TRIG    = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)));
      Keys->L_TRIG    = !!((ret & (1 <<RETRO_DEVICE_ID_JOYPAD_SELECT)));
   }
   else
   {
      Keys->R_TRIG    = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)));
      Keys->L_TRIG    = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)));

      cbuttons_mode   = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)));

      if (cbuttons_mode)
      {
         Keys->R_CBUTTON = !!((ret & (1 << r_cbutton)));
         Keys->L_CBUTTON = !!((ret & (1 << l_cbutton)));
         Keys->D_CBUTTON = !!((ret & (1 << d_cbutton)));
         Keys->U_CBUTTON = !!((ret & (1 << u_cbutton)));
      }
      else
      {
         Keys->A_BUTTON  = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)));
         Keys->B_BUTTON  = !!((ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)));
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

/* GameCube controller through a joybus adapter: answers the 0x40 short
 * poll. Assumes the independent-C-button layout: RetroPad face/shoulder
 * buttons drive the GameCube C-stick when pressed analog, L/R triggers
 * come from SELECT/R2 pressed analog, and the analog sticks are scaled
 * into the GameCube's 0..255 (centre 128) ranges. */
#define GCN_MAX_ANALOG 100
#define GCN_MAX_CSTICK 95

static int32_t clamp16(int32_t input) {
   if (input > SHRT_MAX) {
      input = SHRT_MAX;
   }
   if (input < SHRT_MIN) {
      input = SHRT_MIN;
   }
   return input;
}

void inputGetKeysGCN(int Control, int analogMode, BUTTONS_GCN *Keys)
{
   bool hold_cstick = false;
   int32_t analogX = 0;
   int32_t analogY = 0;
   int32_t cstickX, cstickY;
   int32_t scaledX, scaledY;
   int32_t trigL, trigR;
   memset(Keys, 0, sizeof(*Keys));

   // Assumes alternate_mapping is set
   
   analogX =
      input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R) - 
      input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L);

   analogY =
      input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_A) -
      input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_X);
      
   if( analogX == 0 && analogY == 0 ) {
      // Check for keyboard input
      
      analogX = 0x7FFF * (
         input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) - 
         input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L)
      );
      
      analogY = 0x7FFF * (
         input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) -
         input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X)
      );
   }
      
   analog_scale(clamp16(analogX), clamp16(analogY), GCN_MAX_CSTICK, &cstickX, &cstickY);

   cstickX += 128;
   cstickY += 128;

   Keys->A_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   Keys->B_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   Keys->X_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
   Keys->Y_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
   Keys->Z_TRIG = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);

   trigL = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_SELECT) / (SHRT_MAX / UCHAR_MAX);
   if (trigL == 0) {
      trigL = 255 * input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   }
   trigR = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2) / (SHRT_MAX / UCHAR_MAX);
   if (trigR == 0) {
      trigR = 255 * input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   }

   switch (analogMode) {
      case 0:
         Keys->MODE0.C_X = cstickX;
         Keys->MODE0.C_Y = cstickY; 
         Keys->MODE0.L_TRIG = trigL >> 4;
         Keys->MODE0.R_TRIG = trigR >> 4;
         break;
      case 1:
         Keys->MODE1.C_X = cstickX  >> 4;
         Keys->MODE1.C_Y = cstickY  >> 4; 
         Keys->MODE1.L_TRIG = trigL;
         Keys->MODE1.R_TRIG = trigR;
         break;
      case 2:
         Keys->MODE2.C_X = cstickX >> 4;
         Keys->MODE2.C_Y = cstickY >> 4; 
         Keys->MODE2.L_TRIG = trigL >> 4;
         Keys->MODE2.R_TRIG = trigR >> 4;
         break;
      case 3:
         Keys->MODE3.C_X = cstickX;
         Keys->MODE3.C_Y = cstickY; 
         Keys->MODE3.L_TRIG = trigL;
         Keys->MODE3.R_TRIG = trigR;
         break;
      case 4:
         Keys->MODE4.C_X = cstickX;
         Keys->MODE4.C_Y = cstickY; 
         break;
      default:
         // error
         break;
   }

   
   Keys->L_BUTTON = (trigL > 225) ? 1 : 0;
   Keys->R_BUTTON = (trigR > 225) ? 1 : 0;

   analogX = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
   analogY = input_cb(Control, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);

   analog_scale(analogX, analogY, GCN_MAX_ANALOG, &scaledX, &scaledY);
   Keys->X_AXIS = scaledX + 128;
   Keys->Y_AXIS = scaledY + 128;

   Keys->R_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   Keys->L_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   Keys->D_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   Keys->U_DPAD = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);

   Keys->START_BUTTON = input_cb(Control, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
}

void inputInitiateCallback(const char *headername)
{
   struct retro_message msg;
   char msg_local[256];

   if (getKeys != &inputGetKeys_default)
   {
      getKeys = inputGetKeys_default;
      inputGetKeys_default_descriptor();
      snprintf(msg_local, sizeof(msg_local), "Controls: Default");
      msg.msg = msg_local;
      msg.frames = FRAME_DURATION;
      timeout = FRAME_DURATION / 2;
      if (environ_cb)
         environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);
      return;
   }

    if (
             (!strcmp(headername, "KILLER INSTINCT GOLD")) ||
          (!strcmp(headername, "Killer Instinct Gold")) ||
          (!strcmp(headername, "CLAYFIGHTER 63")) ||
          (!strcmp(headername, "Clayfighter SC")) ||
          (!strcmp(headername, "RAKUGAKIDS")))
    {
       #define six_button_fighter_map(PAD) { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A [Low Kick]" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C-Down [Medium Kick]" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "C-Left [Medium Punch]" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "B [Low Punch]" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C-Up [Fierce Punch]" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "C-Right [Fierce Kick]" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Z-Trigger" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Change Controls" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

       static struct retro_input_descriptor desc[] = {
          six_button_fighter_map(0)
          six_button_fighter_map(1)
          six_button_fighter_map(2)
          six_button_fighter_map(3)
          { 0 },
       };
       environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
       getKeys = inputGetKeys_6ButtonFighters;
    }
    else if (!strcmp(headername, "BIOFREAKS"))
       getKeys = inputGetKeys_Biofreaks;
    else if (!strcmp(headername, "DARK RIFT"))
       getKeys = inputGetKeys_DarkRift;
    else if (!strcmp(headername, "XENAWARRIORPRINCESS"))
       getKeys = inputGetKeys_XENA;
    else if (!strcmp(headername, "RIDGE RACER 64"))
    {
       #define RR64_map(PAD) { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "C-Up" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "B" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Change Controls" },\
          { PAD, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

       static struct retro_input_descriptor desc[] = {
          RR64_map(0)
          RR64_map(1)
          RR64_map(2)
          RR64_map(3)
          { 0 },
       };
       environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
       getKeys = inputGetKeys_RR64;
    }
   else if ((!strcmp(headername, "I S S 64")) ||
         (!strcmp(headername, "J WORLD SOCCER3")) ||
         (!strcmp(headername, "J.WORLD CUP 98")) ||
         (!strcmp(headername, "I.S.S.98")) ||
         (!strcmp(headername, "PERFECT STRIKER2")) ||
         (!strcmp(headername, "I.S.S.2000")))
      getKeys = inputGetKeys_ISS;
    else if (!strcmp(headername, "MACE"))
       getKeys = inputGetKeys_Mace;
    else if ((!strcmp(headername, "MISCHIEF MAKERS")) ||
          (!strcmp(headername, "TROUBLE MAKERS")))
       getKeys = inputGetKeys_MischiefMakers;
   else if ((!strcmp(headername, "MortalKombatTrilogy")) ||
         (!strcmp(headername, "WAR GODS")))
       getKeys = inputGetKeys_MKTrilogy;
   else if (!strcmp(headername, "MORTAL KOMBAT 4"))
       getKeys = inputGetKeys_MK4;
   else if (!strcmp(headername, "MK_MYTHOLOGIES"))
       getKeys = inputGetKeys_MKMythologies;
   else if ((!strcmp(headername, "RAMPAGE")) ||
         (!strcmp(headername, "RAMPAGE2")))
       getKeys = inputGetKeys_Rampage;
   else if ((!strcmp(headername, "READY 2 RUMBLE")) ||
         (!strcmp(headername, "Ready to Rumble")))
       getKeys = inputGetKeys_Ready2Rumble;
   else if (!strcmp(headername, "Wipeout 64"))
       getKeys = inputGetKeys_Wipeout64;
   else if ((!strcmp(headername, "WRESTLEMANIA 2000")) ||
         (!strcmp(headername, "WWF No Mercy")))
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
       controller[i].control->Present = pad_present[i];
       controller[i].control->RawData = 0;

       if (pad_pak_types[i] == PLUGIN_MEMPAK)
          controller[i].control->Plugin = PLUGIN_MEMPAK;
       else if (pad_pak_types[i] == PLUGIN_RAW)
          controller[i].control->Plugin = PLUGIN_RAW;
       else if (pad_pak_types[i] == PLUGIN_TRANSFER_PAK)
          controller[i].control->Plugin = PLUGIN_TRANSFER_PAK;
       else if (pad_pak_types[i] == PLUGIN_BIO_PAK)
          controller[i].control->Plugin = PLUGIN_BIO_PAK;
       else
          controller[i].control->Plugin = PLUGIN_NONE;
    }

   getKeys = inputGetKeys_default;
   inputGetKeys_default_descriptor();
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


int egcvip_is_connected(void* opaque, enum pak_type* pak)
{
    int channel = *(int*)opaque;

    CONTROL* c = &Controls[channel];

    switch(c->Plugin)
    {
    /*case PLUGIN_NONE: *pak = PAK_NONE; break;
    case PLUGIN_MEMPAK: *pak = PAK_MEM; break;
    case PLUGIN_RUMBLE_PAK: *pak = PAK_RUMBLE; break;
    case PLUGIN_TRANSFER_PAK: *pak = PAK_TRANSFER; break;*/

   // case PLUGIN_RAW:
        /* historically PLUGIN_RAW has been mostly (exclusively ?) used for rumble,
         * so we just reproduce that behavior */
        //*pak = PAK_RUMBLE; break;
    }

    return c->Present;
}

uint32_t egcvip_get_input(void* opaque)
{
    BUTTONS keys = { 0 };
    int channel = *(int*)opaque;

    if (getKeys)
       getKeys(channel, &keys);

    return keys.Value;

}
