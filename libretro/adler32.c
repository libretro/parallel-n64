#include <stdint.h>

uint32_t adler32(uint32_t adler, void *buf, int len)
{
   return CRC32(adler, buf, len);
}
