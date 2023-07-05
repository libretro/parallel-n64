#ifndef M64P_UTIL_ARRAY_IO_H
#define M64P_UTIL_ARRAY_IO_H

#include <stdint.h>
#include <stdio.h>

void write_bytes_from_u32_array( const uint32_t* array, FILE *file, size_t numBytes );
size_t read_bytes_into_u32_array( int32_t *array, FILE *file, size_t numBytes );

#endif
