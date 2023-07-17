#include "random.h"

#include <stdlib.h>

#ifdef _WIN32

#include <ntsecapi.h>

static inline int random_u32_impl( uint32_t *value ) {
	return (int)RtlGenRandom( value, 4 );
}

#else

#include <stdio.h>

static inline int random_u32_impl( uint32_t *value ) {
	FILE *rand = fopen( "/dev/urandom", "r" );
	if( !rand ) return 0;
	
	const int status = (int)fread( value, 4, 1, rand );
	fclose( rand );
	
	return status;
}

#endif

uint32_t random_u32() {
	uint32_t value;
	if( !random_u32_impl( &value ) ) {
		value = ((uint32_t)rand() << 16) | ((uint32_t)rand() & 0xFFFFu);
	}
	
	return value;
}
