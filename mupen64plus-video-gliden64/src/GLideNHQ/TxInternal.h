/*
 * Texture Filtering
 * Version:  1.0
 *
 * Copyright (C) 2007  Hiroshi Morii   All Rights Reserved.
 * Email koolsmoky(at)users.sourceforge.net
 * Web   http://www.3dfxzone.it/koolsmoky
 *
 * this is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * this is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include "Ext_TxFilter.h"
#include <string>
#include <algorithm>

#include <Graphics/Parameters.h>
#include <encodings/crc32.h>

typedef graphics::InternalColorFormatParam ColorFormat;

/* the bytes a texture of @format takes, 0 for a format GLideNHQ has not */
static inline int txutil_sizeof_tx(int width, int height, ColorFormat format)
{
	if (format == graphics::internalcolorFormat::COLOR_INDEX8)
		return width * height;
	if (format == graphics::internalcolorFormat::RGBA4 ||
			format == graphics::internalcolorFormat::RGB5_A1 ||
			format == graphics::internalcolorFormat::RGB8)
		return (width * height) << 1;
	if (format == graphics::internalcolorFormat::RGBA8)
		return (width * height) << 2;
	return 0;
}

/* the texture's CRC-32/ISO-HDLC, 0 for a format GLideNHQ has not */
static inline uint32 txutil_checksum_tx(const uint8 *src, int width, int height, ColorFormat format)
{
	const int dataSize = txutil_sizeof_tx(width, height, format);
	return dataSize ? encoding_crc32(0, src, dataSize) : 0;
}

/* @info's format, texture format and pixel type for @internalFormat */
static inline void setTextureFormat(ColorFormat internalFormat, GHQTexInfo *info)
{
	info->format = u32(internalFormat);
	if (internalFormat == graphics::internalcolorFormat::RGB8) {
		info->texture_format = static_cast<unsigned short>(u32(graphics::colorFormat::RED_GREEN_BLUE));
		info->pixel_type = static_cast<unsigned short>(u32(graphics::datatype::UNSIGNED_SHORT_5_6_5));
	} else if (internalFormat == graphics::internalcolorFormat::RGBA4) {
		info->texture_format = static_cast<unsigned short>(u32(graphics::colorFormat::RGBA));
		info->pixel_type = static_cast<unsigned short>(u32(graphics::datatype::UNSIGNED_SHORT_4_4_4_4));
	} else if (internalFormat == graphics::internalcolorFormat::RGB5_A1) {
		info->texture_format = static_cast<unsigned short>(u32(graphics::colorFormat::RGBA));
		info->pixel_type = static_cast<unsigned short>(u32(graphics::datatype::UNSIGNED_SHORT_5_5_5_1));
	} else {
		/* RGBA8, and what GLideNHQ has not */
		info->texture_format = static_cast<unsigned short>(u32(graphics::colorFormat::RGBA));
		info->pixel_type = static_cast<unsigned short>(u32(graphics::datatype::UNSIGNED_BYTE));
	}
}

/* an image's format as TxImage's C reads it (TXI_*) */
static inline ColorFormat txi_format(int format)
{
	if (format == 1) return graphics::internalcolorFormat::RGBA8;        /* TXI_RGBA8 */
	if (format == 2) return graphics::internalcolorFormat::COLOR_INDEX8; /* TXI_COLOR_INDEX8 */
	return graphics::internalcolorFormat::NOCOLOR;                       /* TXI_NONE */
}

/* the format as TxQuantize's C has it (TXQ_*), -1 for one it has not */
static inline int txq_format(ColorFormat format)
{
	if (format == graphics::internalcolorFormat::RGBA8)   return 0; /* TXQ_RGBA8 */
	if (format == graphics::internalcolorFormat::RGB5_A1) return 1; /* TXQ_RGB5_A1 */
	if (format == graphics::internalcolorFormat::RGBA4)   return 2; /* TXQ_RGBA4 */
	if (format == graphics::internalcolorFormat::RGB8)    return 3; /* TXQ_RGB565 */
	return -1;
}

/* in-memory zlib texture compression */
#define GL_TEXFMT_GZ 0x80000000

#endif /* __INTERNAL_H__ */
