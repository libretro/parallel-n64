#ifndef _CXD4_CONFIG_H
#define _CXD4_CONFIG_H

#include <stdint.h>

extern unsigned char conf[32];

/*
 * The config file used to be a 32-byte EEPROM with binary settings storage.
 * It was found necessary for user and contributor convenience to replace.
 *
 * The current configuration system now uses Garteal's CFG text definitions.
 */

#define CFG_HLE_GFX     (conf[0x00])
#define CFG_HLE_AUD     (conf[0x01])
#define CFG_HLE_VID     (conf[0x02]) /* reserved/unused */
#define CFG_HLE_JPG     (conf[0x03]) /* unused */

#endif
