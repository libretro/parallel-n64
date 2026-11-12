/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GRANITE_RENDER_PASS_H
#define GRANITE_RENDER_PASS_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "pipeline_record.h"

RETRO_BEGIN_DECLS

#define GRANITE_NUM_ATTACHMENTS 8

enum granite_render_pass_op
{
	GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = 1 << 0,
	GRANITE_RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = 1 << 1,
	GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = 1 << 2,
	GRANITE_RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT = 1 << 3,
	/* A transient attachment is neither loaded nor stored unless these
	 * say so: doing either costs what being transient saves */
	GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT = 1 << 4,
	GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT = 1 << 5
};

/* What a render pass needs to know of an image it renders to */
typedef struct granite_attachment_desc
{
	VkFormat format;
	/* VkSampleCountFlagBits */
	uint32_t samples;
	/* its memory is lazily allocated and its contents last one pass */
	bool transient;
	/* it is kept in VK_IMAGE_LAYOUT_GENERAL, not the optimal layout of
	 * each use */
	bool general;
} granite_attachment_desc_t;

/* A pass of one subpass that draws to all its attachments */
typedef struct granite_render_pass_desc
{
	granite_attachment_desc_t color[GRANITE_NUM_ATTACHMENTS];
	/* format VK_FORMAT_UNDEFINED: none */
	granite_attachment_desc_t depth_stencil;
	unsigned num_color;
	/* GRANITE_RENDER_PASS_OP_* */
	uint32_t op_flags;
	/* a bit a colour attachment: cleared, loaded (neither: undefined) and
	 * stored (or discarded) */
	uint32_t clear_attachments;
	uint32_t load_attachments;
	uint32_t store_attachments;
} granite_render_pass_desc_t;

typedef struct granite_render_pass
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkRenderPass render_pass;
	VkFormat color_formats[GRANITE_NUM_ATTACHMENTS];
	VkFormat depth_stencil_format;
	unsigned num_color;
	/* of its attachments: they all have the same */
	uint32_t samples;
	/* what it is made from, for the pipeline records */
	pipeline_record_pass_t signature;
} granite_render_pass_t;

/* Two descriptions with one hash make the same pass. @compatible: the
 * hash of the passes that are render pass compatible with it, whatever
 * they load, store and clear and whatever layout their images are in. */
uint64_t granite_render_pass_hash(const granite_render_pass_desc_t *desc, bool compatible);
/* The description of the stand-in pass a pipeline record's signature
 * makes: compatible with the pass the signature was taken from, and with
 * the same compatible hash. False for a signature no pass is made from. */
bool granite_render_pass_desc_from_signature(granite_render_pass_desc_t *desc, const pipeline_record_pass_t *signature);
/* False for a description without attachments, or when the driver
 * refuses the pass: render_pass is VK_NULL_HANDLE then. */
bool granite_render_pass_init(granite_render_pass_t *pass, VkDevice device, const struct VolkDeviceTable *table,
		const granite_render_pass_desc_t *desc);
void granite_render_pass_free(granite_render_pass_t *pass);
bool granite_render_pass_has_depth(const granite_render_pass_t *pass);
bool granite_render_pass_has_stencil(const granite_render_pass_t *pass);

RETRO_END_DECLS

#endif
