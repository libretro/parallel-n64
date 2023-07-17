#include "array_io.h"

void write_bytes_from_u32_array( const uint32_t* array, FILE *file, size_t numBytes ) {
	const size_t numWords = numBytes >> 2;
	for( size_t i = 0; i < numWords; i++ ) {
		const uint32_t word = array[i];
		fputc( (int)(word >> 24), file );
		fputc( (int)((word >> 16) & 0xFFu), file );
		fputc( (int)((word >> 8) & 0xFFu), file );
		fputc( (int)(word & 0xFFu), file );
	}
	
	size_t r = numBytes % 4;
	if( !r ) return;
	
	const uint32_t lastWord = array[numWords];
	fputc( (int)(lastWord >> 24), file );
	if( !--r ) return;
	fputc( (int)((lastWord >> 16) & 0xFFu), file );
	if( !--r ) return;
	fputc( (int)((lastWord >> 8) & 0xFFu), file );
	if( !--r ) return;
	fputc( (int)(lastWord & 0xFFu), file );
}

static inline uint32_t u32_from_be( const uint8_t *word ) {
	return (
		((uint32_t)word[0] << 24) |
		((uint32_t)word[1] << 16) |
		((uint32_t)word[2] << 8) |
		(uint32_t)word[3]
	);
}

size_t read_bytes_into_u32_array( int32_t *array, FILE *file, size_t numBytes ) {
	uint8_t word[4];
	
	const size_t numWords = numBytes >> 2;
	for( size_t i = 0; i < numWords; i++ ) {
		if( !fread( word, 4, 1, file ) ) return i << 2;
		array[i] = u32_from_be( word );
	}
	
	const size_t r = numBytes % 4;
	if( !r ) return numBytes;
	if( !fread( word, r, 1, file ) ) return numWords << 2;
	
	const uint32_t mask = 0xFFFFFFFFu >> (r << 3);
	array[numWords] = (array[numBytes] & mask) | (u32_from_be( word ) & ~mask);
	return numBytes;
}
