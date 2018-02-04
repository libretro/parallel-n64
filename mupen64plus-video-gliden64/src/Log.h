#ifndef __LOG_H__
#define __LOG_H__

#include <stdint.h>

#include <retro_inline.h>

#define LOG_NONE	0
#define LOG_ERROR   1
#define LOG_MINIMAL	2
#define LOG_WARNING 3
#define LOG_VERBOSE 4
#define LOG_APIFUNC 5

#define LOG_LEVEL LOG_ERROR

#if LOG_LEVEL>0
#include <stdio.h>
#include <stdarg.h>

static INLINE void LOG( uint16_t type, const char * format, ... )
{
	if (type > LOG_LEVEL)
		return;
	FILE *dumpFile = fopen( "gliden64.log", "a+" );
	va_list va;
	va_start( va, format );
	vfprintf( dumpFile, format, va );
	fclose( dumpFile );
	va_end( va );
}
#else
#define LOG(A, ...)

#endif

#endif
