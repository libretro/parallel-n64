#include <stdint.h>

#define CRC32_POLYNOMIAL     0x04C11DB7

unsigned int CRCTable[ 256 ];

uint32_t Reflect( uint32_t ref, char ch )
{
	 uint32_t value = 0;

	 // Swap bit 0 for bit 7
	 // bit 1 for bit 6, etc.
	 for (int i = 1; i < (ch + 1); ++i) {
		  if(ref & 1)
			value |= 1 << (ch - i);
		  ref >>= 1;
	 }
	 return value;
}

void CRC_BuildTable()
{
	uint32_t crc;

	for (int i = 0; i < 256; ++i) {
		crc = Reflect( i, 8 ) << 24;
		for (int j = 0; j < 8; ++j)
			crc = (crc << 1) ^ (crc & (1 << 31) ? CRC32_POLYNOMIAL : 0);

		CRCTable[i] = Reflect( crc, 32 );
	}
}

uint32_t CRC_Calculate( uint32_t crc, const void * buffer, uint32_t count )
{
	uint8_t *p;
	uint32_t orig = crc;

	p = (uint8_t*) buffer;
	while (count--)
		crc = (crc >> 8) ^ CRCTable[(crc & 0xFF) ^ *p++];

	return crc ^ orig;
}

uint32_t CRC_CalculatePalette(uint32_t crc, const void * buffer, uint32_t count )
{
	uint8_t *p;
	uint32_t orig = crc;

	p = (uint8_t*) buffer;
	while (count--) {
		crc = (crc >> 8) ^ CRCTable[(crc & 0xFF) ^ *p++];
		crc = (crc >> 8) ^ CRCTable[(crc & 0xFF) ^ *p++];

		p += 6;
	}

	return crc ^ orig;
}

uint32_t textureCRC(uint8_t * addr, uint32_t height, uint32_t stride)
{
	const uint32_t width = stride / 8;
	const uint32_t line = stride % 8;
	uint64_t crc = 0;
	uint64_t twopixel_crc;

	uint32_t *  pixelpos = (uint32_t*)addr;
	for (; height; height--) {
		int col = 0;
		for (uint32_t i = width; i; --i) {
			twopixel_crc = i * ((uint64_t)(pixelpos[1] & 0xFFFEFFFE) + (uint64_t)(pixelpos[0] & 0xFFFEFFFE) + crc);
			crc = (twopixel_crc >> 32) + twopixel_crc;
			pixelpos += 2;
		}
		crc = (height * crc >> 32) + height * crc;
		pixelpos = (uint32_t*)((uint8_t*)pixelpos + line);
	}

	return crc&0xFFFFFFFF;
}
