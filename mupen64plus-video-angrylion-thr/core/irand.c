#include "irand.h"
#include "common.h"

static TLS int32_t iseed = 1;

int32_t irand(void)
{
    iseed *= 0x343fd;
    iseed += 0x269ec3;
    return ((iseed >> 16) & 0x7fff);
}
