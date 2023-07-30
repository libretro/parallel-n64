#include "libpl.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "../util/array_io.h"
#include "../util/random.h"
#include "../util/version.h"
#include "../../../Graphics/plugin.h"
#include "../../../mupen64plus-video-paraLLEl/parallel.h"

#define LIBPL_PIPE_BUFFER_SIZE 4096

static uint32_t g_libplBuffer[0x4000];
static uint32_t g_savestateToken = 0;
static FILE* g_outPipe = NULL;
static FILE* g_inPipe = NULL;

static uint8_t g_usedCheats = 0;
uint8_t g_frameCheatStatus = 0;
uint8_t g_cheatStatus = 0;

static char g_outPipeBuffer[LIBPL_PIPE_BUFFER_SIZE];
static char g_inPipeBuffer[LIBPL_PIPE_BUFFER_SIZE];

extern uint32_t LegacySm64ToolsHacks;
extern uint32_t EnableFBEmulation;
extern uint32_t EnableN64DepthCompare;

#define PLUGIN_ID_PARALLELN64     0x00010000u
#define PLUGIN_ID_GLIDEN64        0x00020000u
#define PLUGIN_ID_OGRE            0x00030000u
#define PLUGIN_ID_GLIDE64         0x00040000u
#define PLUGIN_ID_ANGRYLION       0x00050000u
#define PLUGIN_ID_RICE            0x00060000u
#define PLUGIN_ID_GLN64           0x00070000u

#define PLUGIN_CAPABILITY_UPSCALE 0x0001u
#define PLUGIN_CAPABILITY_FBE     0x0002u
#define PLUGIN_CAPABILITY_DEPTH   0x0004u

static inline void handle_emu_cmd( uint16_t commandId, uint16_t payloadSize ) {
	switch( commandId ) {
		case 0:
			g_libplBuffer[0] = 0x00500000u;
			break;
		case 1:
			g_libplBuffer[0] = 0x00000006u;
			g_libplBuffer[1] = (CORE_VERSION_MAJOR << 16) | CORE_VERSION_MINOR;
			g_libplBuffer[2] = (CORE_VERSION_PATCH << 16) | (g_libplBuffer[2] & 0xFFFFu);
			break;
		case 2:
			g_libplBuffer[0] = 0x00000004u;
			g_libplBuffer[1] = g_savestateToken;
			break;
		case 3:
			g_libplBuffer[0] = (uint32_t)g_usedCheats << 16;
			break;
		case 4:
			switch( gfx_plugin ) {
				case GFX_GLIDE64: {
					g_libplBuffer[0] = 12;
					g_libplBuffer[1] = PLUGIN_ID_GLIDE64 | PLUGIN_CAPABILITY_UPSCALE;
					g_libplBuffer[2] = 0x476c6964u;
					g_libplBuffer[3] = 0x65363400u;
					break;
				}
				case GFX_RICE: {
					g_libplBuffer[0] = 9;
					g_libplBuffer[1] = PLUGIN_ID_RICE | PLUGIN_CAPABILITY_UPSCALE;
					g_libplBuffer[2] = 0x52696365u;
					g_libplBuffer[3] = 0;
					break;
				}
				case GFX_GLN64: {
					g_libplBuffer[0] = 10;
					g_libplBuffer[1] = PLUGIN_ID_GLN64 | PLUGIN_CAPABILITY_UPSCALE;
					g_libplBuffer[2] = 0x676c6e36u;
					g_libplBuffer[3] = 0x34000000u;
					break;
				}
				case GFX_ANGRYLION: {
					g_libplBuffer[0] = 14;
					g_libplBuffer[1] = PLUGIN_ID_ANGRYLION | PLUGIN_CAPABILITY_FBE | PLUGIN_CAPABILITY_DEPTH;
					g_libplBuffer[2] = 0x416e6772u;
					g_libplBuffer[3] = 0x796c696fu;
					g_libplBuffer[4] = 0x6e000000u;
					break;
				}
				case GFX_PARALLEL: {
					g_libplBuffer[0] = 13;
					g_libplBuffer[1] = PLUGIN_ID_PARALLELN64 | PLUGIN_CAPABILITY_FBE | PLUGIN_CAPABILITY_DEPTH;
					if( parallel_get_upscaling() ) g_libplBuffer[1] |= PLUGIN_CAPABILITY_UPSCALE;
					g_libplBuffer[2] = 0x50617261u;
					g_libplBuffer[3] = 0x4c4c456cu;
					g_libplBuffer[4] = 0;
					break;
				}
				case GFX_GLIDEN64: {
					if( LegacySm64ToolsHacks ) {
						g_libplBuffer[0] = 9;
						g_libplBuffer[1] = PLUGIN_ID_OGRE | PLUGIN_CAPABILITY_UPSCALE;
						g_libplBuffer[2] = 0x4f475245u;
						g_libplBuffer[3] = 0;
					} else {
						g_libplBuffer[0] = 13;
						g_libplBuffer[1] = PLUGIN_ID_GLIDEN64 | PLUGIN_CAPABILITY_UPSCALE;
						g_libplBuffer[2] = 0x476c6964u;
						g_libplBuffer[3] = 0x654e3634u;
						g_libplBuffer[4] = 0;
					}
					if( EnableFBEmulation ) g_libplBuffer[1] |= PLUGIN_CAPABILITY_FBE;
					if( EnableN64DepthCompare ) g_libplBuffer[1] |= PLUGIN_CAPABILITY_DEPTH;
					break;
				}
				default: {
					g_libplBuffer[0] = 0x00010000u;
					break;
				}
			}
			break;
		case 5:
			g_libplBuffer[0] = (uint32_t)g_cheatStatus << 16;
			break;
		case 6: {
			switch( payloadSize ) {
				case 0:
					g_cheatStatus &= LPL_USED_CHEATS;
					g_cheatStatus |= g_frameCheatStatus;
					g_libplBuffer[0] = (uint32_t)g_cheatStatus << 16;
					break;
				case 1:
					g_cheatStatus &= ~(uint8_t)(g_libplBuffer[1] >> 24);
					g_cheatStatus |= g_frameCheatStatus;
					g_libplBuffer[0] = (uint32_t)g_cheatStatus << 16;
					break;
				default:
					g_libplBuffer[0] = 0x02000000u;
					break;
			}
			break;
		}
		default:
			g_libplBuffer[0] = 0x01000000u;
			break;
	}
}

static inline void handle_pl_cmd( uint16_t payloadSize ) {
	if( !g_outPipe || !g_inPipe ) {
		g_libplBuffer[0] = 0x03000000u;
		return;
	}
	
	write_bytes_from_u32_array( g_libplBuffer, g_outPipe, (size_t)payloadSize + 4 );
	fflush( g_outPipe );
	
	if( ferror( g_outPipe ) ) {
		g_libplBuffer[0] = 0x03000000u;
		free_libpl();
		return;
	}
	
	uint8_t header[4];
	if( fread( header, 1, 4, g_inPipe ) < 4 ) {
		g_libplBuffer[0] = 0x03000000u;
		free_libpl();
		return;
	}
	
	const uint16_t responseSize = (((uint16_t)header[2]) << 8) | (uint16_t)header[3];
	if( responseSize > 0xFFFCu ) {
		g_libplBuffer[0] = 0x03000000u;
		free_libpl();
		return;
	}
	
	read_bytes_into_u32_array( &g_libplBuffer[1], g_inPipe, (size_t)responseSize );
	if( ferror( g_inPipe ) ) {
		g_libplBuffer[0] = 0x03000000u;
		free_libpl();
	} else if( header[0] > 6 ) {
		g_libplBuffer[0] = 0x03000000u;
	} else {
		g_libplBuffer[0] = (uint32_t)responseSize | ((uint32_t)header[1] << 16) | ((uint32_t)header[0] << 24);
	}
}

void init_libpl(void) {
	free_libpl();
	
	memset( g_libplBuffer, 0, 0x10000 );
	libpl_change_savestate_token();
	
	const char *const inputPipeName = getenv( "PL_LIBPL_PIPE_IN" );
	const char *const outputPipeName = getenv( "PL_LIBPL_PIPE_OUT" );
	
	if( !inputPipeName || !outputPipeName || !inputPipeName[0] || !outputPipeName[0] ) {
		return;
	}
	
	g_outPipe = fopen( outputPipeName, "wb" );
	if( !g_outPipe ) {
		free_libpl();
		return;
	}
	
	setvbuf( g_outPipe, g_outPipeBuffer, _IOFBF, LIBPL_PIPE_BUFFER_SIZE );
	
	fputc( 0x06, g_outPipe );
	fflush( g_outPipe );
	
	if( ferror( g_outPipe ) ) {
		free_libpl();
		return;
	}
	
	g_inPipe = fopen( inputPipeName, "rb" );
	if( !g_inPipe ) {
		free_libpl();
		return;
	}
	
	setvbuf( g_inPipe, g_inPipeBuffer, _IOFBF, LIBPL_PIPE_BUFFER_SIZE );
	
	if( fgetc( g_inPipe ) != 6 ) {
		free_libpl();
	}
}

void free_libpl(void) {
	if( g_outPipe ) {
		fclose( g_outPipe );
		g_outPipe = NULL;
	}
	
	if( g_inPipe ) {
		fclose( g_inPipe );
		g_inPipe = NULL;
	}
}

int read_libpl(void* opaque, uint32_t address, uint32_t* value) {
	const uint32_t i = (address & 0xFFFFu) >> 2;
	*value = g_libplBuffer[i];
	return 0;
}

int write_libpl(void* opaque, uint32_t address, uint32_t value, uint32_t mask) {
	const uint32_t i = (address & 0xFFFFu) >> 2;
	g_libplBuffer[i] = (value & mask) | (g_libplBuffer[i] & ~mask);
	
	if( i == 0 && mask >> 16 ) {
		const uint16_t commandId = (uint16_t)(g_libplBuffer[0] >> 16);
		const uint16_t payloadSize = (uint16_t)(g_libplBuffer[0] & 0xFFFFu);
		
		if( payloadSize > 0xFFFCu ) {
			g_libplBuffer[0] = 0x02000000u;
			return 0;
		}
		
		if( commandId >> 8 ) {
			handle_pl_cmd( payloadSize );
		} else {
			handle_emu_cmd( commandId, payloadSize );
		}
	}
	
	return 0;
}

void libpl_change_savestate_token(void) {
	g_savestateToken = random_u32();
}

void libpl_set_cheats_used(void) {
	g_usedCheats = 0x00010000u;
	g_cheatStatus |= LPL_USED_CHEATS;
	g_frameCheatStatus |= LPL_USED_CHEATS;
}
