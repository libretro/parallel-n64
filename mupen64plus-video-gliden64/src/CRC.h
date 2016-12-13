#include <stdint.h>

void CRC_BuildTable();

// CRC32
uint32_t CRC_Calculate( uint32_t crc, const void *buffer, uint32_t count );
uint32_t CRC_CalculatePalette( uint32_t crc, const void *buffer, uint32_t count );
// Fast checksum calculation from Glide64
uint32_t textureCRC(uint8_t * addr, uint32_t height, uint32_t stride);
