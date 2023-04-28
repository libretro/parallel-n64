#include "is_viewer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static uint32_t g_isvBuffer[0x4000];
static FILE* g_outFile = NULL;
static int g_fileStatus = 0;

extern uint32_t IsvEmulationMode;

void poweron_is_viewer(void) {
	memset( g_isvBuffer, 0, 0x10000 );
	if( g_outFile ) return;
	
	if( IsvEmulationMode == 2 ) {
		g_outFile = stdout;
	} else if( IsvEmulationMode == 3 ) {
		const char *const isvFile = getenv( "PL_ISV_FILE_PATH" );
		if( isvFile && isvFile[0] != '\0' ) {
			g_outFile = fopen( isvFile, "wb" );
			if( g_outFile ) {
				fputc( 0x06, g_outFile );
			} else {
				g_fileStatus = errno;
			}
		}
	}
}

void poweroff_is_viewer(void) {
	if( !g_outFile ) return;
	fclose( g_outFile );
	g_outFile = NULL;
	g_fileStatus = 32;
}

int read_is_viewer(void* opaque, uint32_t address, uint32_t* value) {
	address &= 0xFFFFu;
	*value = g_isvBuffer[address >> 2];
	return 0;
}

static inline void write_partial_word( uint32_t word, uint32_t bytes ) {
	if( !bytes ) return;
	fputc( (int)(word >> 24), g_outFile );
	if( !--bytes ) return;
	fputc( (int)((word >> 16) & 0xFFu), g_outFile );
	if( !--bytes ) return;
	fputc( (int)((word >> 8) & 0xFFu), g_outFile );
	if( !--bytes ) return;
	fputc( (int)(word & 0xFFu), g_outFile );
}

int write_is_viewer(void* opaque, uint32_t address, uint32_t value, uint32_t mask) {
	if( IsvEmulationMode == 0 ) return 0;
	
	const uint32_t i = (address & 0xFFFFu) >> 2;
	if( i == 5 && (mask & 0xFFFFu) ) {
		mask &= 0xFFFF0000u;
		if( mask ) {
			g_isvBuffer[5] = (value & mask) | (g_isvBuffer[5] & ~mask);
		}
		
		if( !g_outFile ) return g_fileStatus;
		
		uint32_t numBytes = value & 0xFFFFu;
		if( numBytes > 0xFFE0u ) {
			memset( g_isvBuffer, 0, 0x10000 );
			
			if( IsvEmulationMode != 3 ) {
				fputs( "[IS Viewer] WARNING: Buffer overflow. IS Viewer has been reset.\n", g_outFile );
			}
			
			return 90;
		}
		
		if( IsvEmulationMode == 3 ) {
			fputc( (int)(numBytes >> 8), g_outFile );
			fputc( (int)(numBytes & 0xff), g_outFile );
		}
		
		const uint32_t numWords = numBytes >> 2;
		for( uint32_t j = 0; j < numWords; j++ ) {
			const uint32_t word = g_isvBuffer[8 + j];
			fputc( (int)(word >> 24), g_outFile );
			fputc( (int)((word >> 16) & 0xFFu), g_outFile );
			fputc( (int)((word >> 8) & 0xFFu), g_outFile );
			fputc( (int)(word & 0xFFu), g_outFile );
		}
		
		write_partial_word( g_isvBuffer[numWords + 8], numBytes % 4 );
		fflush( g_outFile );

		int status = ferror( g_outFile );
		if( status != 0 ) clearerr( g_outFile );
		
		return status;
	}
	
	g_isvBuffer[i] = (value & mask) | (g_isvBuffer[i] & ~mask);
	return 0;
}
