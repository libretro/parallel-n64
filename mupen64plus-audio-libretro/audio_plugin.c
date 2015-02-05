/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-libretro-audio - main.c                                   *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2007-2009 Richard Goedeken                              *
 *   Copyright (C) 2007-2008 Ebenblues                                     *
 *   Copyright (C) 2003 JttL                                               *
 *   Copyright (C) 2002 Hactarux                                           *
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
#include <stdarg.h>

#include "libretro.h"
extern retro_audio_sample_batch_t audio_batch_cb;

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_common.h"
#include "m64p_config.h"

#include "audio_resampler_driver.h"
#include "utils.h"

#ifndef MAX_AUDIO_FRAMES
#define MAX_AUDIO_FRAMES 2048
#endif

/* Read header for type definition */
static AUDIO_INFO AudioInfo;
static int GameFreq = 33600;

static const rarch_resampler_t *resampler;
static void *resampler_audio_data;
static float *audio_in_buffer_float;
static float *audio_out_buffer_float;
static int16_t *audio_out_buffer_s16;

void (*audio_convert_s16_to_float_arm)(float *out,
      const int16_t *in, size_t samples, float gain);
void (*audio_convert_float_to_s16_arm)(int16_t *out,
      const float *in, size_t samples);

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL audioPluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    // This function should never be called in libretro version                    
    return M64ERR_SUCCESS;
}

/* ----------- Audio Functions ------------- */
EXPORT void CALL audioAiDacrateChanged( int SystemType )
{
    switch (SystemType)
    {
        case SYSTEM_NTSC: GameFreq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1); break;
        case SYSTEM_PAL:  GameFreq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1); break;
        case SYSTEM_MPAL: GameFreq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1); break;
    }
}

bool no_audio;

EXPORT void CALL audioAiLenChanged(void)
{
   uint8_t *p;
   int16_t *raw_data, *out;
   size_t frames, max_frames, remain_frames;
	uint32_t i, len;
   double ratio;
   struct resampler_data data = {0};

   len = *AudioInfo.AI_LEN_REG;
   //if (log_cb)
      //log_cb(RETRO_LOG_INFO, "AI_LEN_REG: %d\n", len);
	p = (uint8_t*)(AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));

   for (i = 0; i < len; i += 4)
   {
      p[i    ] ^= p[i + 2];
      p[i + 2] ^= p[i    ];
      p[i    ] ^= p[i + 2];

      p[i + 1] ^= p[i + 3];
      p[i + 3] ^= p[i + 1];
      p[i + 1] ^= p[i + 3];
   }

   raw_data = (int16_t*)p;
   frames = len / 4;

audio_batch:
   out = NULL;
	ratio = 44100.0 / GameFreq;
	max_frames = GameFreq > 44100 ? MAX_AUDIO_FRAMES : (size_t)(MAX_AUDIO_FRAMES / ratio - 1);
   remain_frames = 0;
   
   if (no_audio)
      return;
   
   if (frames > max_frames)
   {
      remain_frames = frames - max_frames;
      frames = max_frames;
   }

   data.data_in = audio_in_buffer_float;
   data.data_out = audio_out_buffer_float;
   data.input_frames = frames;
   data.ratio = ratio;

   audio_convert_s16_to_float(audio_in_buffer_float, raw_data, frames * 2, 1.0f);

   resampler->process(resampler_audio_data, &data);

   audio_convert_float_to_s16(audio_out_buffer_s16, audio_out_buffer_float, data.output_frames * 2);
   out = audio_out_buffer_s16;
   while (data.output_frames)
   {
      size_t ret = audio_batch_cb(out, data.output_frames);
      data.output_frames -= ret;
      out += ret * 2;
   }

   if (remain_frames)
   {
      raw_data = raw_data + frames * 2;
      frames = remain_frames;
      goto audio_batch;
   }
}


EXPORT m64p_error CALL audioPluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context, void (*DebugCallback)(void *, int, const char *))
{
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL audioPluginShutdown(void)
{
    if (resampler && resampler_audio_data)
    {
       resampler->free(resampler_audio_data);
       resampler = NULL;
       resampler_audio_data = NULL;
       free(audio_in_buffer_float);
       free(audio_out_buffer_float);
       free(audio_out_buffer_s16);
    }
   return M64ERR_SUCCESS;
}

EXPORT int CALL audioInitiateAudio(AUDIO_INFO Audio_Info)
{
   AudioInfo = Audio_Info;

   rarch_resampler_realloc(&resampler_audio_data, &resampler, "CC", 1.0);
   audio_in_buffer_float = malloc(2 * MAX_AUDIO_FRAMES * sizeof(float));
   audio_out_buffer_float = malloc(2 * MAX_AUDIO_FRAMES * sizeof(float));
   audio_out_buffer_s16 = malloc(2 * MAX_AUDIO_FRAMES * sizeof(int16_t));
   audio_convert_init_simd();
   return 1;
}

EXPORT int CALL audioRomOpen(void){return 1;}
EXPORT void CALL audioRomClosed(void){}
EXPORT void CALL audioProcessAList(void){}
EXPORT void CALL audioSetSpeedFactor(int percentage){}
EXPORT void CALL audioVolumeMute(void){}
EXPORT void CALL audioVolumeUp(void){}
EXPORT void CALL audioVolumeDown(void){}
EXPORT int CALL audioVolumeGetLevel(void){return 100;}
EXPORT void CALL audioVolumeSetLevel(int level){}
EXPORT const char * CALL audioVolumeGetString(void){return "Not Supported";}

