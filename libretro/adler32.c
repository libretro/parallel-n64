#include <stdint.h>

unsigned int CRC32( unsigned int crc, void *buffer, unsigned int count );

uint32_t adler32(uint32_t adler, void *buf, int len)
{
   return CRC32(adler, buf, len);
}
