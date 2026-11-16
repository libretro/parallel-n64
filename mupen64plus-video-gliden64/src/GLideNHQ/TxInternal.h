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

typedef graphics::InternalColorFormatParam ColorFormat;

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
