/* A pipeline as it is made, without the driver: what a device built,
 * kept so the next device can build it again ahead of its first use.
 * Plain fixed-size fields only, so the records can be written to a file
 * as they are and read back by C code. C89. */
#ifndef VULKAN_PIPELINE_RECORD_H
#define VULKAN_PIPELINE_RECORD_H

#include <stdint.h>

#define PIPELINE_RECORD_ATTACHMENTS    8  /* VULKAN_NUM_ATTACHMENTS */
#define PIPELINE_RECORD_SPEC_CONSTANTS 12 /* VULKAN_NUM_TOTAL_SPEC_CONSTANTS */
#define PIPELINE_RECORD_VERTEX_ATTRIBS 16 /* VULKAN_NUM_VERTEX_ATTRIBS */
#define PIPELINE_RECORD_VERTEX_BUFFERS 4  /* VULKAN_NUM_VERTEX_BUFFERS */
#define PIPELINE_RECORD_SUBPASSES      4

#define PIPELINE_RECORD_COMPUTE  0u
#define PIPELINE_RECORD_GRAPHICS 1u

#ifdef __cplusplus
extern "C" {
#endif

/* What a compatible render pass is made from, without its images: the
 * attachment formats and sample counts, which of them are transient, and
 * the subpasses. Compatibility goes by these alone, so 1x1 stand-ins for
 * the attachments make the same pass again. */
typedef struct pipeline_record_subpass
{
   uint32_t color[PIPELINE_RECORD_ATTACHMENTS];
   uint32_t input[PIPELINE_RECORD_ATTACHMENTS];
   uint32_t resolve[PIPELINE_RECORD_ATTACHMENTS];
   uint32_t num_color;
   uint32_t num_input;
   uint32_t num_resolve;
   uint32_t depth_mode;
} pipeline_record_subpass_t;

typedef struct pipeline_record_pass
{
   uint32_t formats[PIPELINE_RECORD_ATTACHMENTS];
   uint32_t samples[PIPELINE_RECORD_ATTACHMENTS + 1]; /* the depth one last */
   uint32_t num_color;
   uint32_t depth_format;  /* VK_FORMAT_UNDEFINED (0): none */
   uint32_t lazy;          /* transient attachments, depth after the colour ones */
   uint32_t op_flags;
   uint32_t num_subpasses; /* 0: the default one */
   /* 0 when it cannot be made again from this: a swapchain image, more
    * than one layer, more subpasses than are kept */
   uint32_t recordable;
   pipeline_record_subpass_t subpasses[PIPELINE_RECORD_SUBPASSES];
} pipeline_record_pass_t;

typedef struct pipeline_record
{
   uint64_t pipeline_hash;
   uint64_t program_hash;
   uint64_t shader_hashes[2];  /* compute; or vertex, fragment */
   uint64_t render_pass_hash;  /* graphics only */
   uint32_t kind;              /* PIPELINE_RECORD_* */
   uint32_t static_words[4];
   uint32_t spec_constants[PIPELINE_RECORD_SPEC_CONSTANTS];
   uint32_t spec_masks;        /* user mask | internal mask << 8 */
   uint32_t subgroup_size_tag;
   uint32_t subpass_index;
   uint32_t blend_constants[4]; /* the floats' bits */
   uint32_t attribs[PIPELINE_RECORD_VERTEX_ATTRIBS][3]; /* binding, format, offset */
   uint32_t strides[PIPELINE_RECORD_VERTEX_BUFFERS];
   uint32_t input_rates[PIPELINE_RECORD_VERTEX_BUFFERS];
   uint32_t pad;
   pipeline_record_pass_t pass;
} pipeline_record_t;

/* the layout is the file's: no hidden padding, a whole number of words */
typedef char pipeline_record_size_check[(sizeof(pipeline_record_t) % 8) == 0 ? 1 : -1];

#ifdef __cplusplus
}
#endif

#endif
