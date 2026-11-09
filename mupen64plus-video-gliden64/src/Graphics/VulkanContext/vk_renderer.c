#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <libretro_vulkan.h>

#include <stdlib.h>
#include <file/file_path.h>
#include <string.h>

#include <boolean.h>
#include <retro_atomic.h>
#include <queues/mpsc_stack.h>
#include <streams/file_stream.h>
#include <compat/strl.h>
#include <rdp_worker.h>

#include "vk_device.h"
#include "vk_renderer.h"

/* ---- capacity: every pool is fixed at creation, exhaustion is defined ---- */
#define MAX_IMAGES      8192
#define MAX_FBOS        4096
#define MAX_RPS         16
#define MAX_SAMPLERS    64
#define MAX_PIPELINES   (1 << 14)
#define MAX_JOBS        512
#define MAX_PROGRAMS    4096
#define MAX_FRAMES      16
#define MAX_SUBMITS     64
#define MAX_GARBAGE     4096
#define MAX_DESC_SETS   4096
#define DESC_CACHE      (1 << 13)
#define RING_BYTES      (32u * 1024 * 1024)
#define BLOCK_BYTES     (128u * 1024 * 1024)
#define MAX_BLOCKS      16
#define MAX_SPANS       2048

#define VKR_DEVICE_FUNCS(X) \
   X(CreateImage) X(DestroyImage) X(CreateImageView) X(DestroyImageView) \
   X(GetImageMemoryRequirements) X(BindImageMemory) X(AllocateMemory) X(FreeMemory) \
   X(MapMemory) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
   X(CreateSampler) X(DestroySampler) X(CreateRenderPass) X(DestroyRenderPass) \
   X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateShaderModule) X(DestroyShaderModule) \
   X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) \
   X(CreatePipelineCache) X(DestroyPipelineCache) X(GetPipelineCacheData) \
   X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) \
   X(DestroyDescriptorPool) X(ResetDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
   X(CreateCommandPool) X(DestroyCommandPool) X(ResetCommandPool) X(AllocateCommandBuffers) \
   X(BeginCommandBuffer) X(EndCommandBuffer) X(QueueSubmit) \
   X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(GetFenceStatus) X(DeviceWaitIdle) \
   X(InvalidateMappedMemoryRanges) \
   X(CmdPipelineBarrier) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) \
   X(CmdBindDescriptorSets) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) X(CmdPushConstants) \
   X(CmdSetViewport) X(CmdSetScissor) X(CmdSetDepthBias) X(CmdSetBlendConstants) X(CmdSetLineWidth) \
   X(CmdDraw) X(CmdDrawIndexed) X(CmdClearAttachments) X(CmdClearColorImage) X(CmdClearDepthStencilImage) \
   X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) X(CmdBlitImage) X(CmdResolveImage)

#define VKR_EDS_FUNCS(X) \
   X(CmdSetCullModeEXT) X(CmdSetFrontFaceEXT) X(CmdSetDepthTestEnableEXT) X(CmdSetDepthWriteEnableEXT) \
   X(CmdSetDepthCompareOpEXT) X(CmdSetPrimitiveTopologyEXT) \
   X(CmdSetColorBlendEnableEXT) X(CmdSetColorBlendEquationEXT) X(CmdSetDepthClampEnableEXT)

typedef struct vkr_fn
{
#define X(N) PFN_vk##N N;
   VKR_DEVICE_FUNCS(X)
   VKR_EDS_FUNCS(X)
#undef X
} vkr_fn;

/* ---- device memory: first fit over a few big blocks ---- */
typedef struct vkr_span { VkDeviceSize offset, size; } vkr_span;

typedef struct vkr_block
{
   VkDeviceMemory mem;
   VkDeviceSize   size;
   uint32_t       type;
   vkr_span      *free_spans;
   unsigned       n_free;
} vkr_block;

typedef struct vkr_alloc { int block; VkDeviceSize offset, size; } vkr_alloc;

typedef struct vkr_image
{
   VkImage     image;
   VkImageView view;
   VkFormat    format;
   VkImageLayout layout;
   vkr_alloc   alloc;
   uint32_t    used_serial;
   uint16_t    gen;
   uint16_t    w, h;
   uint8_t     samples, levels, is_depth, live;
   int         fmt;
   VkSampler   sampler;
   int         mag, min, mip, wrap_s, wrap_t;
   float       max_lod, aniso;
} vkr_image;

typedef struct vkr_fbo
{
   unsigned    color, depth;
   VkFramebuffer fb;
   VkRenderPass  rp;
   uint32_t      rp_index;
   uint16_t      w, h;
   uint8_t       live;
} vkr_fbo;

typedef struct vkr_rp { VkFormat color, depth; uint32_t samples; VkRenderPass rp; } vkr_rp;

typedef struct vkr_sampler_entry { uint32_t key; float max_lod; float aniso; VkSampler s; } vkr_sampler_entry;

enum { G_IMAGE = 0, G_VIEW, G_FB, G_BUFFER, G_MEMORY };
typedef struct vkr_garbage { int kind; uint64_t handle; vkr_alloc alloc; } vkr_garbage;

typedef struct desc_entry { uint32_t key[VKR_BIND_COUNT]; VkDescriptorSet set; uint32_t stamp; } desc_entry;

typedef struct vkr_frame
{
   VkCommandPool   pool;
   VkCommandBuffer cmd[MAX_SUBMITS];
   VkCommandBuffer xfer[MAX_SUBMITS];
   unsigned        n_cmd;
   VkBuffer        ring;
   VkDeviceMemory  ring_mem;
   uint8_t        *ring_map;
   VkDeviceSize    ring_at;
   VkDescriptorPool dpool;
   unsigned        n_sets;
   uint32_t        last_serial;
   vkr_garbage     garbage[MAX_GARBAGE];
   unsigned        n_garbage;
   VkImage         win_image;
   VkImageView     win_view;
   VkDeviceMemory  win_mem;
   VkFramebuffer   win_fb;
   unsigned        win_w, win_h;
   VkImageLayout   win_layout;
} vkr_frame;

struct vkr_program
{
   vkr_renderer *r;
   vkr_combiner_desc desc;
   int  rect;
   int  vs_kind, fs_kind;
   int  is_special;
   uint32_t id;
   float special[4];
   /* only the worker reads and writes this once it is queued */
   VkShaderModule fs;
   /* the values last written to the ring this frame */
   const CombinerValues *slot;
   uint32_t   slot_stamp;
   uint32_t   slot_offset;
   int        has_slot;
};

enum { PS_EMPTY = 0, PS_PENDING, PS_READY, PS_FAILED };

typedef struct pipe_entry
{
   uint64_t key;
   uint32_t prog;
   VkPipeline pipeline;
   retro_atomic_int_t status;
   uint8_t used;
} pipe_entry;

typedef struct pipe_job
{
   mpsc_stack_node_t node;
   pipe_entry *entry;
   vkr_program *program;       /* NULL: vs and fs below are all it needs */
   VkShaderModule vs, fs;
   uint64_t key;
} pipe_job;

struct vkr_readback
{
   VkBuffer buffer;
   VkDeviceMemory mem;
   void *map;
   size_t bytes;
   uint32_t serial;
};

/* the fixed part of a pipeline: everything else is dynamic state */
typedef struct pipe_state
{
   unsigned blend_en : 1, src : 4, dst : 4, src_a : 4, dst_a : 4;
   unsigned rp : 5;
   unsigned rect : 1, kind : 3;
   unsigned depth_clamp : 1, lines : 1;
   unsigned cull : 2, depth_test : 1, depth_write : 1, depth_cmp : 2, strip : 1;
} pipe_state;

struct vkr_renderer
{
   const struct retro_hw_render_interface_vulkan *vk;
   VkDevice device;
   VkPhysicalDeviceProperties props;
   VkPhysicalDeviceMemoryProperties mem;
   vkr_fn fn;
   vkr_shader_config cfg;
   vkr_caps caps;
   int eds;
   int eds3; /* blending and depth clamp are dynamic state */
   VkFormat depth_format;

   VkPipelineCache cache;
   VkDescriptorSetLayout set_layout;
   VkPipelineLayout layout;
   VkShaderModule vs[VKR_VS_COUNT];
   VkShaderModule generic_fs;
   VkShaderModule special_fs[VKR_FS_COUNT];
   VkBuffer unused;

   vkr_image *images;
   uint32_t *image_free;
   unsigned n_image_free;
   vkr_fbo *fbos;
   uint32_t *fbo_free;
   unsigned n_fbo_free;
   vkr_rp rps[MAX_RPS];
   unsigned n_rps;
   vkr_sampler_entry samplers[MAX_SAMPLERS];
   unsigned n_samplers;
   vkr_block blocks[MAX_BLOCKS];
   unsigned n_blocks;
   unsigned dummy_color, dummy_ms, dummy_uint, dummy_depth, dummy_storage;

   vkr_frame frames[MAX_FRAMES];
   unsigned n_frames;
   vkr_frame *f;
   unsigned sync_index;
   int frame_open;
   VkCommandBuffer cmd, xfer;
   int xfer_open;
   int pass_open;
   uint32_t serial;           /* of the commands being recorded */
   VkFence fences[MAX_SUBMITS];
   uint32_t fence_serial[MAX_SUBMITS];
   unsigned window_w, window_h;

   desc_entry *desc_cache;
   uint32_t desc_stamp;

   /* pipelines */
   pipe_entry *pipes;
   rdp_worker_t *worker;
   pipe_job *jobs;
   mpsc_stack_t job_free_returned;
   mpsc_stack_node_t *job_free;
   retro_atomic_int_t job_count;
   spv_module worker_spv;
   uint32_t next_program;
   vkr_program *programs;
   unsigned n_programs;

   /* state */
   pipe_state st;
   int enable[5];
   int depth_clamp, clip;
   int depth_write_on, cull_mode, depth_cmp;
   VkViewport viewport;
   VkRect2D scissor;
   float blend_color[4];
   float bias_factor, bias_units;
   float line_width;
   unsigned bound[VKR_BIND_COUNT];
   unsigned read_fbo, draw_fbo;
   vkr_program *current;
   uint32_t values_offset;
   uint32_t dirty;
   VkPipeline bound_pipe;
   VkDescriptorSet bound_set;
   uint32_t bound_set_offset;
   int push_clip_sent;
   int cur_rect;
   int force_generic;
   int strict; /* tools/ci: a pipeline prewarm did not build is a failure */
   int prewarmed;
   char cache_path[1024];
   VkDeviceSize vb_offset, ib_offset;
   int vb_valid, ib_valid;
};

enum { D_VIEWPORT = 1, D_SCISSOR = 2, D_BIAS = 4, D_BLEND = 8, D_LINE = 16, D_EDS = 32, D_EDS3 = 64, D_ALL = 127 };

#define F(N) (r->fn.N)

/* ---- formats ---- */
static VkFormat to_vk_format(vkr_renderer *r, int fmt)
{
   switch (fmt)
   {
      case VKR_FMT_RGBA8:  return VK_FORMAT_R8G8B8A8_UNORM;
      case VKR_FMT_RGB8:   return VK_FORMAT_R8G8B8A8_UNORM;
      case VKR_FMT_RGBA4:  return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
      case VKR_FMT_RGB5A1: return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
      case VKR_FMT_R8:     return VK_FORMAT_R8_UNORM;
      case VKR_FMT_R16F:   return VK_FORMAT_R16_SFLOAT;
      case VKR_FMT_R32F:   return VK_FORMAT_R32_SFLOAT;
      case VKR_FMT_R32UI:  return VK_FORMAT_R32_UINT;
      case VKR_FMT_DEPTH:  return r->depth_format;
      default: break;
   }
   return VK_FORMAT_R8G8B8A8_UNORM;
}

/* ---- layouts ---- */
static void layout_info(VkImageLayout l, VkPipelineStageFlags *stage, VkAccessFlags *access)
{
   switch (l)
   {
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
         *stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
         *access = VK_ACCESS_SHADER_READ_BIT;
         break;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
         *stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
         *access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
         break;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
         *stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
         *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
         break;
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
         *stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
         *access = VK_ACCESS_TRANSFER_WRITE_BIT;
         break;
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
         *stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
         *access = VK_ACCESS_TRANSFER_READ_BIT;
         break;
      case VK_IMAGE_LAYOUT_GENERAL:
         *stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
         *access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
         break;
      default:
         *stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
         *access = 0;
         break;
   }
}

static void image_barrier(vkr_renderer *r, VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
      VkImageLayout from, VkImageLayout to)
{
   VkImageMemoryBarrier b;
   VkPipelineStageFlags src, dst;
   VkAccessFlags sa, da;

   layout_info(from, &src, &sa);
   layout_info(to, &dst, &da);
   memset(&b, 0, sizeof(b));
   b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   b.srcAccessMask = sa;
   b.dstAccessMask = da;
   b.oldLayout = from;
   b.newLayout = to;
   b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   b.image = image;
   b.subresourceRange.aspectMask = aspect;
   b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
   b.subresourceRange.layerCount = 1;
   F(CmdPipelineBarrier)(cmd, src, dst, 0, 0, NULL, 0, NULL, 1, &b);
}

static VkImageAspectFlags aspect_of(const vkr_image *i)
{
   return i->is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

/* ---- memory ---- */
static int find_memory_type(vkr_renderer *r, uint32_t bits, VkMemoryPropertyFlags want)
{
   uint32_t i;
   for (i = 0; i < r->mem.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (r->mem.memoryTypes[i].propertyFlags & want) == want)
         return (int)i;
   return -1;
}

static int block_add(vkr_renderer *r, uint32_t type, VkDeviceSize size)
{
   VkMemoryAllocateInfo info;
   vkr_block *b;

   if (r->n_blocks >= MAX_BLOCKS)
      return -1;
   b = &r->blocks[r->n_blocks];
   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   info.allocationSize = size;
   info.memoryTypeIndex = type;
   if (F(AllocateMemory)(r->device, &info, NULL, &b->mem) != VK_SUCCESS)
      return -1;
   b->size = size;
   b->type = type;
   b->free_spans = (vkr_span*)malloc(MAX_SPANS * sizeof(vkr_span));
   if (!b->free_spans)
      return -1;
   b->free_spans[0].offset = 0;
   b->free_spans[0].size = size;
   b->n_free = 1;
   return (int)r->n_blocks++;
}

static int block_take(vkr_block *b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *out)
{
   unsigned i;
   for (i = 0; i < b->n_free; i++)
   {
      vkr_span *s = &b->free_spans[i];
      VkDeviceSize at = (s->offset + align - 1) & ~(align - 1);
      VkDeviceSize pad = at - s->offset;
      if (s->size < pad + size)
         continue;
      {
         VkDeviceSize after = s->size - pad - size;
         VkDeviceSize orig = s->offset;
         if (pad == 0 && after == 0)
         {
            memmove(s, s + 1, (b->n_free - i - 1) * sizeof(*s));
            b->n_free--;
         }
         else if (pad == 0)
         {
            s->offset = at + size;
            s->size = after;
         }
         else
         {
            s->size = pad;
            (void)orig;
            if (after)
            {
               if (b->n_free >= MAX_SPANS)
                  return 0;
               memmove(s + 2, s + 1, (b->n_free - i - 1) * sizeof(*s));
               b->n_free++;
               s[1].offset = at + size;
               s[1].size = after;
            }
         }
      }
      *out = at;
      return 1;
   }
   return 0;
}

static void block_give(vkr_block *b, VkDeviceSize offset, VkDeviceSize size)
{
   unsigned i = 0;
   while (i < b->n_free && b->free_spans[i].offset < offset)
      i++;
   if (i > 0 && b->free_spans[i - 1].offset + b->free_spans[i - 1].size == offset)
   {
      b->free_spans[i - 1].size += size;
      if (i < b->n_free && b->free_spans[i - 1].offset + b->free_spans[i - 1].size == b->free_spans[i].offset)
      {
         b->free_spans[i - 1].size += b->free_spans[i].size;
         memmove(&b->free_spans[i], &b->free_spans[i + 1], (b->n_free - i - 1) * sizeof(vkr_span));
         b->n_free--;
      }
      return;
   }
   if (i < b->n_free && offset + size == b->free_spans[i].offset)
   {
      b->free_spans[i].offset = offset;
      b->free_spans[i].size += size;
      return;
   }
   if (b->n_free >= MAX_SPANS)
      return; /* lost until the context goes: the list cannot hold it */
   memmove(&b->free_spans[i + 1], &b->free_spans[i], (b->n_free - i) * sizeof(vkr_span));
   b->free_spans[i].offset = offset;
   b->free_spans[i].size = size;
   b->n_free++;
}

static int mem_alloc(vkr_renderer *r, const VkMemoryRequirements *req, VkMemoryPropertyFlags want, vkr_alloc *a)
{
   int type = find_memory_type(r, req->memoryTypeBits, want);
   unsigned i;
   VkDeviceSize block_size = BLOCK_BYTES;

   if (type < 0)
      return 0;
   for (i = 0; i < r->n_blocks; i++)
   {
      if (r->blocks[i].type != (uint32_t)type)
         continue;
      if (block_take(&r->blocks[i], req->size, req->alignment, &a->offset))
      {
         a->block = (int)i;
         a->size = req->size;
         return 1;
      }
   }
   if (req->size > block_size)
      block_size = req->size;
   i = (unsigned)block_add(r, (uint32_t)type, block_size);
   if ((int)i < 0 || !block_take(&r->blocks[i], req->size, req->alignment, &a->offset))
      return 0;
   a->block = (int)i;
   a->size = req->size;
   return 1;
}

/* ---- the frame ---- */
static void wait_serial(vkr_renderer *r, uint32_t serial)
{
   unsigned i;
   if (!serial)
      return;
   i = serial % MAX_SUBMITS;
   if (r->fence_serial[i] != serial)
      return; /* that fence has been reused, so it has signalled */
   F(WaitForFences)(r->device, 1, &r->fences[i], VK_TRUE, ~(uint64_t)0);
}

static void retire(vkr_renderer *r, vkr_frame *f, unsigned i)
{
   vkr_garbage *g = &f->garbage[i];
   switch (g->kind)
   {
      case G_IMAGE:  F(DestroyImage)(r->device, (VkImage)(uintptr_t)g->handle, NULL); break;
      case G_VIEW:   F(DestroyImageView)(r->device, (VkImageView)(uintptr_t)g->handle, NULL); break;
      case G_FB:     F(DestroyFramebuffer)(r->device, (VkFramebuffer)(uintptr_t)g->handle, NULL); break;
      case G_BUFFER: F(DestroyBuffer)(r->device, (VkBuffer)(uintptr_t)g->handle, NULL); break;
      case G_MEMORY: F(FreeMemory)(r->device, (VkDeviceMemory)(uintptr_t)g->handle, NULL); break;
      default: break;
   }
   if (g->alloc.block >= 0)
      block_give(&r->blocks[g->alloc.block], g->alloc.offset, g->alloc.size);
}

static void trash(vkr_renderer *r, int kind, uint64_t handle, const vkr_alloc *alloc)
{
   vkr_frame *f = r->f;
   vkr_garbage *g;

   if (!f)
      f = &r->frames[0];
   if (f->n_garbage >= MAX_GARBAGE)
   {
      /* the list is full: what it holds retires now, once the GPU is done */
      unsigned i;
      F(DeviceWaitIdle)(r->device);
      for (i = 0; i < f->n_garbage; i++)
         retire(r, f, i);
      f->n_garbage = 0;
   }
   g = &f->garbage[f->n_garbage++];
   g->kind = kind;
   g->handle = handle;
   if (alloc)
      g->alloc = *alloc;
   else
      g->alloc.block = -1;
}

static void begin_cmds(vkr_renderer *r)
{
   VkCommandBufferBeginInfo info;
   vkr_frame *f = r->f;

   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   r->cmd  = f->cmd[f->n_cmd];
   r->xfer = f->xfer[f->n_cmd];
   F(BeginCommandBuffer)(r->cmd, &info);
   F(BeginCommandBuffer)(r->xfer, &info);
   r->xfer_open = 0;
   r->dirty = D_ALL;
   r->bound_pipe = VK_NULL_HANDLE;
   r->bound_set = VK_NULL_HANDLE;
   r->push_clip_sent = -1;
   r->vb_valid = r->ib_valid = 0;
   f->n_cmd++;
}

static void frame_begin(vkr_renderer *r)
{
   vkr_frame *f;
   unsigned i;

   if (r->frame_open)
      return;
   r->vk->wait_sync_index(r->vk->handle);
   r->sync_index = r->vk->get_sync_index(r->vk->handle);
   if (r->sync_index >= r->n_frames)
      r->sync_index = 0;
   f = &r->frames[r->sync_index];
   wait_serial(r, f->last_serial);
   for (i = 0; i < f->n_garbage; i++)
      retire(r, f, i);
   f->n_garbage = 0;
   F(ResetCommandPool)(r->device, f->pool, 0);
   F(ResetDescriptorPool)(r->device, f->dpool, 0);
   f->n_cmd = 0;
   f->n_sets = 0;
   f->ring_at = 0;
   r->desc_stamp++;
   r->f = f;
   r->frame_open = 1;
   begin_cmds(r);
   r->serial++;
}

static void end_pass(vkr_renderer *r)
{
   if (!r->pass_open)
      return;
   F(CmdEndRenderPass)(r->cmd);
   r->pass_open = 0;
}

/* Sends what is recorded. Returns its fence's serial. */
static uint32_t submit(vkr_renderer *r)
{
   vkr_frame *f = r->f;
   VkSubmitInfo si;
   VkCommandBuffer cmds[2];
   unsigned idx = r->serial % MAX_SUBMITS;
   uint32_t done = r->serial;

   end_pass(r);
   F(EndCommandBuffer)(r->cmd);
   F(EndCommandBuffer)(r->xfer);
   cmds[0] = r->xfer;
   cmds[1] = r->cmd;

   if (r->fence_serial[idx])
      F(WaitForFences)(r->device, 1, &r->fences[idx], VK_TRUE, ~(uint64_t)0);
   F(ResetFences)(r->device, 1, &r->fences[idx]);
   r->fence_serial[idx] = r->serial;

   memset(&si, 0, sizeof(si));
   si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.commandBufferCount = r->xfer_open ? 2 : 1;
   si.pCommandBuffers = r->xfer_open ? cmds : &cmds[1];
   r->vk->lock_queue(r->vk->handle);
   F(QueueSubmit)(r->vk->queue, 1, &si, r->fences[idx]);
   r->vk->unlock_queue(r->vk->handle);
   f->last_serial = r->serial;
   r->serial++;
   return done;
}

static void flush(vkr_renderer *r)
{
   submit(r);
   if (r->f->n_cmd >= MAX_SUBMITS)
   {
      /* out of command buffers for this frame: the GPU catches up */
      uint32_t last = r->f->last_serial;
      wait_serial(r, last);
      F(ResetCommandPool)(r->device, r->f->pool, 0);
      r->f->n_cmd = 0;
   }
   begin_cmds(r);
}

static VkCommandBuffer xfer_cmd(vkr_renderer *r)
{
   r->xfer_open = 1;
   return r->xfer;
}

/* ---- ring ---- */
static void *ring_reserve(vkr_renderer *r, VkDeviceSize bytes, VkDeviceSize align, VkDeviceSize *offset)
{
   vkr_frame *f;
   VkDeviceSize at;

   frame_begin(r);
   f = r->f;
   at = (f->ring_at + align - 1) & ~(align - 1);
   if (at + bytes > RING_BYTES)
   {
      /* more than a frame's worth: everything recorded runs first */
      uint32_t last;
      flush(r);
      last = f->last_serial;
      wait_serial(r, last);
      at = 0;
   }
   f->ring_at = at + bytes;
   *offset = at;
   return f->ring_map + at;
}

/* ---- images ---- */
static void ensure_layout(vkr_renderer *r, vkr_image *i, VkImageLayout l)
{
   if (i->layout == l)
      return;
   end_pass(r);
   image_barrier(r, r->cmd, i->image, aspect_of(i), i->layout, l);
   i->layout = l;
}

static void destroy_image_now(vkr_renderer *r, vkr_image *i)
{
   if (i->view != VK_NULL_HANDLE)
      trash(r, G_VIEW, (uint64_t)(uintptr_t)i->view, NULL);
   if (i->image != VK_NULL_HANDLE)
      trash(r, G_IMAGE, (uint64_t)(uintptr_t)i->image, &i->alloc);
   i->view = VK_NULL_HANDLE;
   i->image = VK_NULL_HANDLE;
}

static int image_create(vkr_renderer *r, vkr_image *i, int fmt, unsigned samples,
      unsigned w, unsigned h, unsigned levels)
{
   VkImageCreateInfo info;
   VkImageViewCreateInfo view;
   VkMemoryRequirements req;
   VkFormat format = to_vk_format(r, fmt);
   int depth = fmt == VKR_FMT_DEPTH;

   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   info.imageType = VK_IMAGE_TYPE_2D;
   info.format = format;
   info.extent.width = w;
   info.extent.height = h;
   info.extent.depth = 1;
   info.mipLevels = levels;
   info.arrayLayers = 1;
   info.samples = (VkSampleCountFlagBits)(samples ? samples : 1);
   info.tiling = VK_IMAGE_TILING_OPTIMAL;
   info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   if (depth)
      info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
   else if (fmt == VKR_FMT_RGBA8 || fmt == VKR_FMT_RGB8 || fmt == VKR_FMT_R8)
      info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
   if (fmt == VKR_FMT_R32F && r->caps.storage_images)
      info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
   info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   if (F(CreateImage)(r->device, &info, NULL, &i->image) != VK_SUCCESS)
   {
      i->image = VK_NULL_HANDLE;
      return 0;
   }
   F(GetImageMemoryRequirements)(r->device, i->image, &req);
   if (!mem_alloc(r, &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &i->alloc)
         || F(BindImageMemory)(r->device, i->image, r->blocks[i->alloc.block].mem, i->alloc.offset) != VK_SUCCESS)
   {
      F(DestroyImage)(r->device, i->image, NULL);
      i->image = VK_NULL_HANDLE;
      return 0;
   }

   memset(&view, 0, sizeof(view));
   view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   view.image = i->image;
   view.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view.format = format;
   view.components.r = VK_COMPONENT_SWIZZLE_R;
   view.components.g = VK_COMPONENT_SWIZZLE_G;
   view.components.b = VK_COMPONENT_SWIZZLE_B;
   view.components.a = VK_COMPONENT_SWIZZLE_A;
   if (fmt == VKR_FMT_RGB8)
      view.components.a = VK_COMPONENT_SWIZZLE_ONE;
   if (fmt == VKR_FMT_RGBA4)
   {
      /* R4G4B4A4 is not mandatory; this one is, and swizzling it reads
       * the same bits */
      view.components.r = VK_COMPONENT_SWIZZLE_B;
      view.components.b = VK_COMPONENT_SWIZZLE_R;
   }
   view.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
   view.subresourceRange.levelCount = levels;
   view.subresourceRange.layerCount = 1;
   if (F(CreateImageView)(r->device, &view, NULL, &i->view) != VK_SUCCESS)
   {
      i->view = VK_NULL_HANDLE;
      return 0;
   }

   i->format = format;
   i->w = (uint16_t)w;
   i->h = (uint16_t)h;
   i->samples = (uint8_t)(samples ? samples : 1);
   i->levels = (uint8_t)levels;
   i->is_depth = (uint8_t)depth;
   i->fmt = fmt;
   i->layout = VK_IMAGE_LAYOUT_UNDEFINED;
   i->used_serial = 0;

   /* a texture is never garbage until written: zero it, on the side */
   frame_begin(r);
   image_barrier(r, xfer_cmd(r), i->image, aspect_of(i), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
   {
      VkImageSubresourceRange range;
      memset(&range, 0, sizeof(range));
      range.aspectMask = aspect_of(i);
      range.levelCount = VK_REMAINING_MIP_LEVELS;
      range.layerCount = 1;
      if (depth)
      {
         VkClearDepthStencilValue v;
         v.depth = 0.f;
         v.stencil = 0;
         F(CmdClearDepthStencilImage)(r->xfer, i->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
      }
      else if (samples <= 1)
      {
         VkClearColorValue c;
         memset(&c, 0, sizeof(c));
         F(CmdClearColorImage)(r->xfer, i->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &range);
      }
   }
   image_barrier(r, r->xfer, i->image, aspect_of(i), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
   i->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   return 1;
}

unsigned vkr_texture_new(vkr_renderer *r)
{
   unsigned h;
   vkr_image *i;
   if (!r->n_image_free)
      return 0;
   h = r->image_free[--r->n_image_free];
   i = &r->images[h];
   i->live = 1;
   i->gen++;
   i->sampler = VK_NULL_HANDLE;
   i->mag = i->min = i->mip = VKR_FILTER_NEAREST;
   i->wrap_s = i->wrap_t = VKR_WRAP_CLAMP;
   i->max_lod = 1000.f;
   i->aniso = 0.f;
   return h;
}

void vkr_texture_delete(vkr_renderer *r, unsigned h)
{
   vkr_image *i;
   unsigned u;
   if (!h || h >= MAX_IMAGES || !r->images[h].live)
      return;
   i = &r->images[h];
   destroy_image_now(r, i);
   i->live = 0;
   r->image_free[r->n_image_free++] = h;
   for (u = 0; u < VKR_BIND_COUNT; u++)
      if (r->bound[u] == h)
         r->bound[u] = 0;
}

static void upload(vkr_renderer *r, vkr_image *i, unsigned level, unsigned x, unsigned y,
      unsigned w, unsigned h, const void *data, size_t bytes)
{
   VkDeviceSize offset;
   VkBufferImageCopy c;
   VkCommandBuffer cmd;
   int in_this_cmd = i->used_serial == r->serial;
   const uint8_t *from = (const uint8_t*)data;

   /* decoded in place: it is in the ring already */
   if (from >= r->f->ring_map && from < r->f->ring_map + RING_BYTES)
      offset = (VkDeviceSize)(from - r->f->ring_map);
   else
      memcpy(ring_reserve(r, bytes, 16, &offset), data, bytes);

   memset(&c, 0, sizeof(c));
   c.bufferOffset = offset;
   c.imageSubresource.aspectMask = aspect_of(i);
   c.imageSubresource.mipLevel = level;
   c.imageSubresource.layerCount = 1;
   c.imageOffset.x = (int)x;
   c.imageOffset.y = (int)y;
   c.imageExtent.width = w;
   c.imageExtent.height = h;
   c.imageExtent.depth = 1;

   /* Not read yet by what is being recorded: it goes ahead of it, and the
    * render pass stays open. */
   if (in_this_cmd)
   {
      end_pass(r);
      cmd = r->cmd;
   }
   else
      cmd = xfer_cmd(r);
   image_barrier(r, cmd, i->image, aspect_of(i), i->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
   F(CmdCopyBufferToImage)(cmd, r->f->ring, i->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
   image_barrier(r, cmd, i->image, aspect_of(i), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
   i->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void vkr_texture_init(vkr_renderer *r, unsigned h, int fmt, unsigned samples,
      unsigned w, unsigned hgt, unsigned level, unsigned levels, const void *data, size_t data_bytes)
{
   vkr_image *i;

   if (!h || h >= MAX_IMAGES)
      return;
   i = &r->images[h];
   frame_begin(r);
   if (samples > 1)
      samples = r->cfg.msaa > 1 ? (unsigned)r->cfg.msaa : 1;
   if (level == 0 && (i->image == VK_NULL_HANDLE || i->w != w || i->h != hgt
            || i->fmt != fmt || i->samples != (samples ? samples : 1) || i->levels != levels))
   {
      destroy_image_now(r, i);
      if (!image_create(r, i, fmt, samples, w, hgt, levels ? levels : 1))
         return;
   }
   if (data && data_bytes)
      upload(r, i, level, 0, 0, w, hgt, data, data_bytes);
}

void *vkr_texture_lend(vkr_renderer *r, size_t bytes)
{
   VkDeviceSize offset;
   /* one texture must not be able to end the frame's ring early */
   if (bytes > RING_BYTES / 4)
      return NULL;
   frame_begin(r);
   return ring_reserve(r, bytes, 16, &offset);
}

void vkr_texture_update(vkr_renderer *r, unsigned h, unsigned level,
      unsigned x, unsigned y, unsigned w, unsigned hgt, const void *data, size_t data_bytes)
{
   vkr_image *i;
   if (!h || h >= MAX_IMAGES)
      return;
   i = &r->images[h];
   if (i->image == VK_NULL_HANDLE || !data)
      return;
   frame_begin(r);
   upload(r, i, level, x, y, w, hgt, data, data_bytes);
}

static VkSampler get_sampler(vkr_renderer *r, int mag, int min, int mip_linear, int wrap_s, int wrap_t,
      float max_lod, float aniso)
{
   uint32_t key = (uint32_t)(mag | min << 1 | mip_linear << 2 | wrap_s << 3 | wrap_t << 5);
   VkSamplerCreateInfo info;
   vkr_sampler_entry *e;
   unsigned i;
   static const VkSamplerAddressMode modes[3] = {
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
   };

   for (i = 0; i < r->n_samplers; i++)
      if (r->samplers[i].key == key && r->samplers[i].max_lod == max_lod && r->samplers[i].aniso == aniso)
         return r->samplers[i].s;
   if (r->n_samplers >= MAX_SAMPLERS)
      return r->samplers[0].s;
   e = &r->samplers[r->n_samplers];
   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   info.magFilter = mag ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
   info.minFilter = min ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
   info.mipmapMode = mip_linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
   info.addressModeU = modes[wrap_s];
   info.addressModeV = modes[wrap_t];
   info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   info.anisotropyEnable = aniso > 1.f ? VK_TRUE : VK_FALSE;
   info.maxAnisotropy = aniso > 1.f ? aniso : 1.f;
   info.maxLod = max_lod;
   if (F(CreateSampler)(r->device, &info, NULL, &e->s) != VK_SUCCESS)
      return r->samplers[0].s;
   e->key = key;
   e->max_lod = max_lod;
   e->aniso = aniso;
   r->n_samplers++;
   return e->s;
}

/* What is passed as VKR_KEEP, or a negative limit, stays as it was. */
void vkr_texture_params(vkr_renderer *r, unsigned h, int mag, int min, int mip_linear,
      int wrap_s, int wrap_t, float max_lod, float anisotropy)
{
   vkr_image *i;
   if (!h || h >= MAX_IMAGES)
      return;
   i = &r->images[h];
   if (mag != VKR_KEEP)
      i->mag = mag;
   if (min != VKR_KEEP)
      i->min = min;
   if (mip_linear != VKR_KEEP)
      i->mip = mip_linear;
   if (wrap_s != VKR_KEEP)
      i->wrap_s = wrap_s;
   if (wrap_t != VKR_KEEP)
      i->wrap_t = wrap_t;
   if (max_lod >= 0.f)
      i->max_lod = max_lod;
   if (anisotropy >= 0.f)
      i->aniso = anisotropy;
   i->sampler = get_sampler(r, i->mag, i->min, i->mip, i->wrap_s, i->wrap_t, i->max_lod, i->aniso);
   i->gen++;
}

void vkr_bind(vkr_renderer *r, unsigned unit, unsigned h)
{
   if (unit < VKR_BIND_COUNT)
      r->bound[unit] = h;
}

void vkr_bind_image(vkr_renderer *r, unsigned unit, unsigned h)
{
   vkr_bind(r, unit, h);
}

/* ---- framebuffers ---- */
unsigned vkr_fbo_new(vkr_renderer *r)
{
   unsigned h;
   if (!r->n_fbo_free)
      return 0;
   h = r->fbo_free[--r->n_fbo_free];
   memset(&r->fbos[h], 0, sizeof(vkr_fbo));
   r->fbos[h].live = 1;
   return h;
}

static void fbo_reset(vkr_renderer *r, vkr_fbo *f)
{
   if (f->fb != VK_NULL_HANDLE)
      trash(r, G_FB, (uint64_t)(uintptr_t)f->fb, NULL);
   f->fb = VK_NULL_HANDLE;
}

void vkr_fbo_delete(vkr_renderer *r, unsigned h)
{
   if (!h || h >= MAX_FBOS || !r->fbos[h].live)
      return;
   if (r->draw_fbo == h)
   {
      end_pass(r);
      r->draw_fbo = 0;
   }
   fbo_reset(r, &r->fbos[h]);
   r->fbos[h].live = 0;
   r->fbo_free[r->n_fbo_free++] = h;
}

void vkr_fbo_bind(vkr_renderer *r, int read, int draw, unsigned h)
{
   if (read)
      r->read_fbo = h;
   if (draw && r->draw_fbo != h)
   {
      end_pass(r);
      r->draw_fbo = h;
      r->dirty |= D_VIEWPORT | D_SCISSOR;
   }
}

void vkr_fbo_color(vkr_renderer *r, unsigned fbo, unsigned tex)
{
   if (!fbo || fbo >= MAX_FBOS || r->fbos[fbo].color == tex)
      return;
   if (fbo == r->draw_fbo)
      end_pass(r);
   r->fbos[fbo].color = tex;
   fbo_reset(r, &r->fbos[fbo]);
}

void vkr_fbo_depth(vkr_renderer *r, unsigned fbo, unsigned tex)
{
   if (!fbo || fbo >= MAX_FBOS || r->fbos[fbo].depth == tex)
      return;
   if (fbo == r->draw_fbo)
      end_pass(r);
   r->fbos[fbo].depth = tex;
   fbo_reset(r, &r->fbos[fbo]);
}

static int get_render_pass(vkr_renderer *r, VkFormat color, VkFormat depth, uint32_t samples)
{
   VkAttachmentDescription att[2];
   VkAttachmentReference cref, dref;
   VkSubpassDescription sub;
   VkRenderPassCreateInfo info;
   unsigned i;
   unsigned n = 0;

   for (i = 0; i < r->n_rps; i++)
      if (r->rps[i].color == color && r->rps[i].depth == depth && r->rps[i].samples == samples)
         return (int)i;
   if (r->strict && r->prewarmed)
      abort(); /* tools/ci: a format prewarm does not cover */
   if (r->n_rps >= MAX_RPS)
      return -1;

   memset(att, 0, sizeof(att));
   memset(&sub, 0, sizeof(sub));
   memset(&info, 0, sizeof(info));
   cref.attachment = VK_ATTACHMENT_UNUSED;
   cref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   dref.attachment = VK_ATTACHMENT_UNUSED;
   dref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   if (color != VK_FORMAT_UNDEFINED)
   {
      att[n].format = color;
      att[n].samples = (VkSampleCountFlagBits)samples;
      att[n].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      att[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      att[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      att[n].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      att[n].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      cref.attachment = n++;
      sub.colorAttachmentCount = 1;
      sub.pColorAttachments = &cref;
   }
   if (depth != VK_FORMAT_UNDEFINED)
   {
      att[n].format = depth;
      att[n].samples = (VkSampleCountFlagBits)samples;
      att[n].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      att[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      att[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      att[n].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      att[n].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      dref.attachment = n++;
      sub.pDepthStencilAttachment = &dref;
   }
   sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   info.attachmentCount = n;
   info.pAttachments = att;
   info.subpassCount = 1;
   info.pSubpasses = &sub;
   if (F(CreateRenderPass)(r->device, &info, NULL, &r->rps[r->n_rps].rp) != VK_SUCCESS)
      return -1;
   r->rps[r->n_rps].color = color;
   r->rps[r->n_rps].depth = depth;
   r->rps[r->n_rps].samples = samples;
   return (int)r->n_rps++;
}

static void window_image(vkr_renderer *r, vkr_frame *f)
{
   VkImageCreateInfo info;
   VkImageViewCreateInfo view;
   VkMemoryRequirements req;
   VkMemoryAllocateInfo ma;
   int rp;

   if (f->win_image != VK_NULL_HANDLE && f->win_w == r->window_w && f->win_h == r->window_h)
      return;
   if (f->win_fb != VK_NULL_HANDLE)
      trash(r, G_FB, (uint64_t)(uintptr_t)f->win_fb, NULL);
   if (f->win_view != VK_NULL_HANDLE)
      trash(r, G_VIEW, (uint64_t)(uintptr_t)f->win_view, NULL);
   if (f->win_image != VK_NULL_HANDLE)
      trash(r, G_IMAGE, (uint64_t)(uintptr_t)f->win_image, NULL);
   if (f->win_mem != VK_NULL_HANDLE)
      trash(r, G_MEMORY, (uint64_t)(uintptr_t)f->win_mem, NULL);

   if (!r->window_w || !r->window_h)
   {
      r->window_w = 320;
      r->window_h = 240;
   }
   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   info.imageType = VK_IMAGE_TYPE_2D;
   info.format = VK_FORMAT_R8G8B8A8_UNORM;
   info.extent.width = r->window_w ? r->window_w : 1;
   info.extent.height = r->window_h ? r->window_h : 1;
   info.extent.depth = 1;
   info.mipLevels = 1;
   info.arrayLayers = 1;
   info.samples = VK_SAMPLE_COUNT_1_BIT;
   info.tiling = VK_IMAGE_TILING_OPTIMAL;
   info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   F(CreateImage)(r->device, &info, NULL, &f->win_image);
   F(GetImageMemoryRequirements)(r->device, f->win_image, &req);
   memset(&ma, 0, sizeof(ma));
   ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   ma.allocationSize = req.size;
   ma.memoryTypeIndex = (uint32_t)find_memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   F(AllocateMemory)(r->device, &ma, NULL, &f->win_mem);
   F(BindImageMemory)(r->device, f->win_image, f->win_mem, 0);
   memset(&view, 0, sizeof(view));
   view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   view.image = f->win_image;
   view.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view.format = info.format;
   view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   view.subresourceRange.levelCount = 1;
   view.subresourceRange.layerCount = 1;
   F(CreateImageView)(r->device, &view, NULL, &f->win_view);
   rp = get_render_pass(r, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 1);
   {
      VkFramebufferCreateInfo fbi;
      memset(&fbi, 0, sizeof(fbi));
      fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbi.renderPass = r->rps[rp].rp;
      fbi.attachmentCount = 1;
      fbi.pAttachments = &f->win_view;
      fbi.width = info.extent.width;
      fbi.height = info.extent.height;
      fbi.layers = 1;
      F(CreateFramebuffer)(r->device, &fbi, NULL, &f->win_fb);
   }
   f->win_w = r->window_w;
   f->win_h = r->window_h;
   f->win_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

/* Everything the draw reads, in the layouts it reads them in. */
static void prepare_textures(vkr_renderer *r)
{
   unsigned u;
   for (u = VKR_BIND_TEX0; u < VKR_BIND_COUNT; u++)
   {
      vkr_image *i;
      unsigned h = r->bound[u];
      if (!h || h >= MAX_IMAGES)
         continue;
      i = &r->images[h];
      if (i->image == VK_NULL_HANDLE)
         continue;
      if (u == VKR_BIND_DEPTH_Z || u == VKR_BIND_DEPTH_DZ)
         ensure_layout(r, i, VK_IMAGE_LAYOUT_GENERAL);
      else
         ensure_layout(r, i, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      i->used_serial = r->serial;
   }
}

static int begin_pass(vkr_renderer *r)
{
   VkRenderPassBeginInfo info;
   VkRenderPass rp;
   VkFramebuffer fb;
   unsigned w, h;
   vkr_frame *f = r->f;

   if (r->pass_open)
      return 1;
   memset(&info, 0, sizeof(info));
   if (r->draw_fbo == 0)
   {
      int idx;
      window_image(r, f);
      if (f->win_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
      {
         image_barrier(r, r->cmd, f->win_image, VK_IMAGE_ASPECT_COLOR_BIT, f->win_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
         f->win_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      }
      idx = get_render_pass(r, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 1);
      r->st.rp = (unsigned)idx;
      rp = r->rps[idx].rp;
      fb = f->win_fb;
      w = f->win_w;
      h = f->win_h;
   }
   else
   {
      vkr_fbo *o = &r->fbos[r->draw_fbo];
      vkr_image *c = o->color ? &r->images[o->color] : NULL;
      vkr_image *d = o->depth ? &r->images[o->depth] : NULL;
      if ((!c || c->image == VK_NULL_HANDLE) && (!d || d->image == VK_NULL_HANDLE))
         return 0;
      if (c)
      {
         ensure_layout(r, c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
         c->used_serial = r->serial;
      }
      if (d)
      {
         ensure_layout(r, d, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
         d->used_serial = r->serial;
      }
      if (o->fb == VK_NULL_HANDLE)
      {
         VkFramebufferCreateInfo fbi;
         VkImageView views[2];
         uint32_t n = 0;
         int idx = get_render_pass(r, c ? c->format : VK_FORMAT_UNDEFINED, d ? d->format : VK_FORMAT_UNDEFINED, c ? c->samples : d->samples);
         if (idx < 0)
            return 0;
         if (c)
            views[n++] = c->view;
         if (d)
            views[n++] = d->view;
         memset(&fbi, 0, sizeof(fbi));
         fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
         fbi.renderPass = r->rps[idx].rp;
         fbi.attachmentCount = n;
         fbi.pAttachments = views;
         fbi.width = c ? c->w : d->w;
         fbi.height = c ? c->h : d->h;
         fbi.layers = 1;
         if (F(CreateFramebuffer)(r->device, &fbi, NULL, &o->fb) != VK_SUCCESS)
            return 0;
         o->rp = r->rps[idx].rp;
         o->rp_index = (uint32_t)idx;
         o->w = (uint16_t)fbi.width;
         o->h = (uint16_t)fbi.height;
      }
      r->st.rp = o->rp_index;
      rp = o->rp;
      fb = o->fb;
      w = o->w;
      h = o->h;
   }
   info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   info.renderPass = rp;
   info.framebuffer = fb;
   info.renderArea.extent.width = w;
   info.renderArea.extent.height = h;
   F(CmdBeginRenderPass)(r->cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
   r->pass_open = 1;
   r->dirty |= D_VIEWPORT | D_SCISSOR;
   return 1;
}

static void framebuffer_height(const vkr_renderer *r, int *h)
{
   *h = (int)r->f->win_h;
}

static const VkBlendFactor blend_factors[11] = {
   VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
   VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
   VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, VK_BLEND_FACTOR_SRC1_ALPHA,
   VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
};

static void flush_dynamic(vkr_renderer *r)
{
   if (r->dirty & D_VIEWPORT)
   {
      VkViewport v = r->viewport;
      if (r->draw_fbo == 0)
      {
         /* the window is presented top-down: the N64's image is drawn with
          * GL's bottom-left origin everywhere else */
         int h;
         framebuffer_height(r, &h);
         v.y = (float)h - v.y;
         v.height = -v.height;
      }
      /* zero-sized in GL draws nothing: the scissor keeps that */
      if (v.width <= 0.f)
         v.width = 1.f;
      if (v.height == 0.f)
         v.height = 1.f;
      F(CmdSetViewport)(r->cmd, 0, 1, &v);
   }
   if (r->dirty & D_SCISSOR)
   {
      VkRect2D s = r->scissor;
      if (r->viewport.width <= 0.f || r->viewport.height <= 0.f)
      {
         s.offset.x = 0;
         s.offset.y = 0;
         s.extent.width = 0;
         s.extent.height = 0;
         F(CmdSetScissor)(r->cmd, 0, 1, &s);
         goto scissor_done;
      }
      if (!r->enable[VKR_CAP_SCISSOR])
      {
         s.offset.x = 0;
         s.offset.y = 0;
         s.extent.width = 32767;
         s.extent.height = 32767;
      }
      else if (r->draw_fbo == 0)
      {
         int h;
         framebuffer_height(r, &h);
         s.offset.y = h - (int)s.offset.y - (int)s.extent.height;
      }
      if (s.offset.x < 0)
      {
         if ((int)s.extent.width + s.offset.x < 0)
            s.extent.width = 0;
         else
            s.extent.width = (uint32_t)((int)s.extent.width + s.offset.x);
         s.offset.x = 0;
      }
      if (s.offset.y < 0)
      {
         if ((int)s.extent.height + s.offset.y < 0)
            s.extent.height = 0;
         else
            s.extent.height = (uint32_t)((int)s.extent.height + s.offset.y);
         s.offset.y = 0;
      }
      F(CmdSetScissor)(r->cmd, 0, 1, &s);
   }
scissor_done:
   if (r->dirty & D_BIAS)
   {
      int on = r->enable[VKR_CAP_POLY_OFFSET];
      F(CmdSetDepthBias)(r->cmd, on ? r->bias_units : 0.f, 0.f, on ? r->bias_factor : 0.f);
   }
   if (r->dirty & D_BLEND)
      F(CmdSetBlendConstants)(r->cmd, r->blend_color);
   if (r->dirty & D_LINE)
      F(CmdSetLineWidth)(r->cmd, r->line_width);
   if ((r->dirty & D_EDS) && r->eds)
   {
      static const VkCullModeFlags cull[4] = { VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_AND_BACK };
      static const VkCompareOp cmp[3] = { VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_LESS, VK_COMPARE_OP_ALWAYS };
      F(CmdSetFrontFaceEXT)(r->cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
      F(CmdSetCullModeEXT)(r->cmd, cull[r->enable[VKR_CAP_CULL] ? r->cull_mode : 0]);
      F(CmdSetDepthTestEnableEXT)(r->cmd, r->enable[VKR_CAP_DEPTH_TEST] ? VK_TRUE : VK_FALSE);
      F(CmdSetDepthWriteEnableEXT)(r->cmd, r->depth_write_on ? VK_TRUE : VK_FALSE);
      F(CmdSetDepthCompareOpEXT)(r->cmd, cmp[r->depth_cmp]);
   }
   if ((r->dirty & D_EDS3) && r->eds3)
   {
      VkBool32 on = r->enable[VKR_CAP_BLEND] ? VK_TRUE : VK_FALSE;
      VkColorBlendEquationEXT eq;
      eq.srcColorBlendFactor = blend_factors[r->st.src];
      eq.dstColorBlendFactor = blend_factors[r->st.dst];
      eq.colorBlendOp = VK_BLEND_OP_ADD;
      eq.srcAlphaBlendFactor = blend_factors[r->st.src_a];
      eq.dstAlphaBlendFactor = blend_factors[r->st.dst_a];
      eq.alphaBlendOp = VK_BLEND_OP_ADD;
      F(CmdSetColorBlendEnableEXT)(r->cmd, 0, 1, &on);
      F(CmdSetColorBlendEquationEXT)(r->cmd, 0, 1, &eq);
      F(CmdSetDepthClampEnableEXT)(r->cmd, r->depth_clamp ? VK_TRUE : VK_FALSE);
   }
   r->dirty = 0;
}

/* ---- pipelines ---- */
static uint64_t state_key(const vkr_renderer *r, int rect, int lines, int strip)
{
   pipe_state s = r->st;
   uint64_t k = 0;

   s.blend_en    = r->enable[VKR_CAP_BLEND] ? 1 : 0;
   if (!s.blend_en)
      s.src = s.dst = s.src_a = s.dst_a = 0;
   s.depth_clamp = r->depth_clamp ? 1 : 0;
   if (r->eds3)
   {
      s.blend_en = s.src = s.dst = s.src_a = s.dst_a = 0;
      s.depth_clamp = 0;
   }
   s.rect        = rect ? 1 : 0;
   s.lines       = lines ? 1 : 0;
   s.kind        = 0;
   if (r->eds)
   {
      s.cull = s.depth_test = s.depth_write = s.depth_cmp = 0;
      s.strip = 0;
   }
   else
   {
      s.cull        = r->enable[VKR_CAP_CULL] ? (unsigned)r->cull_mode : 0;
      s.depth_test  = r->enable[VKR_CAP_DEPTH_TEST] ? 1 : 0;
      s.depth_write = r->depth_write_on ? 1 : 0;
      s.depth_cmp   = (unsigned)r->depth_cmp;
      s.strip       = strip ? 1 : 0;
   }
   memcpy(&k, &s, sizeof(s) < sizeof(k) ? sizeof(s) : sizeof(k));
   return k;
}

static VkPipeline build_pipeline(vkr_renderer *r, VkShaderModule vs, VkShaderModule fs, uint64_t key)
{
   static const VkCullModeFlags cull[4] = { VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_AND_BACK };
   static const VkCompareOp cmp[3] = { VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_LESS, VK_COMPARE_OP_ALWAYS };
   static const VkDynamicState base_dyn[5] = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS,
      VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_LINE_WIDTH
   };
   static const VkDynamicState eds_dyn[6] = {
      VK_DYNAMIC_STATE_CULL_MODE_EXT, VK_DYNAMIC_STATE_FRONT_FACE_EXT, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,
      VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT
   };
   VkDynamicState dyn[14];
   pipe_state s;
   VkPipelineShaderStageCreateInfo stages[2];
   VkVertexInputBindingDescription vb;
   VkVertexInputAttributeDescription va[5];
   VkPipelineVertexInputStateCreateInfo vi;
   VkPipelineInputAssemblyStateCreateInfo ia;
   VkPipelineViewportStateCreateInfo vp;
   VkPipelineRasterizationStateCreateInfo rs;
   VkPipelineMultisampleStateCreateInfo ms;
   VkPipelineDepthStencilStateCreateInfo ds;
   VkPipelineColorBlendAttachmentState cba;
   VkPipelineColorBlendStateCreateInfo cb;
   VkPipelineDynamicStateCreateInfo dy;
   VkGraphicsPipelineCreateInfo info;
   VkPipeline pipe = VK_NULL_HANDLE;
   const vkr_rp *rp;
   unsigned n = 5;
   unsigned i;

   memcpy(&s, &key, sizeof(s) < sizeof(key) ? sizeof(s) : sizeof(key));
   rp = &r->rps[s.rp];
   memcpy(dyn, base_dyn, sizeof(base_dyn));
   if (r->eds)
   {
      for (i = 0; i < 6; i++)
         dyn[n++] = eds_dyn[i];
   }

   memset(stages, 0, sizeof(stages));
   for (i = 0; i < 2; i++)
   {
      stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[i].pName = "main";
   }
   stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   stages[0].module = vs;
   stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   stages[1].module = fs;

   memset(&vi, 0, sizeof(vi));
   memset(va, 0, sizeof(va));
   vb.binding = 0;
   vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vi.vertexBindingDescriptionCount = 1;
   vi.pVertexBindingDescriptions = &vb;
   vi.pVertexAttributeDescriptions = va;
   if (s.rect)
   {
      vb.stride = sizeof(vkr_rect_vertex);
      va[0].location = 0; va[0].format = VK_FORMAT_R32G32B32A32_SFLOAT; va[0].offset = offsetof(vkr_rect_vertex, x);
      va[1].location = 1; va[1].format = VK_FORMAT_R32G32_SFLOAT; va[1].offset = offsetof(vkr_rect_vertex, s0);
      va[2].location = 2; va[2].format = VK_FORMAT_R32G32_SFLOAT; va[2].offset = offsetof(vkr_rect_vertex, s1);
      vi.vertexAttributeDescriptionCount = 3;
   }
   else
   {
      vb.stride = sizeof(vkr_tri_vertex);
      va[0].location = 0; va[0].format = VK_FORMAT_R32G32B32A32_SFLOAT; va[0].offset = offsetof(vkr_tri_vertex, x);
      va[1].location = 1; va[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; va[1].offset = offsetof(vkr_tri_vertex, r);
      va[2].location = 2; va[2].format = VK_FORMAT_R32G32_SFLOAT; va[2].offset = offsetof(vkr_tri_vertex, s);
      va[3].location = 3; va[3].format = VK_FORMAT_R8G8B8A8_SNORM; va[3].offset = offsetof(vkr_tri_vertex, modify);
      va[4].location = 4; va[4].format = VK_FORMAT_R32_SFLOAT; va[4].offset = offsetof(vkr_tri_vertex, num_lights);
      vi.vertexAttributeDescriptionCount = 5;
   }

   memset(&ia, 0, sizeof(ia));
   ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   ia.topology = s.lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
      : (s.strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

   memset(&vp, 0, sizeof(vp));
   vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   vp.viewportCount = 1;
   vp.scissorCount = 1;

   memset(&rs, 0, sizeof(rs));
   rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   rs.depthClampEnable = s.depth_clamp ? VK_TRUE : VK_FALSE;
   rs.polygonMode = VK_POLYGON_MODE_FILL;
   rs.cullMode = cull[s.cull];
   rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   rs.depthBiasEnable = VK_TRUE; /* off is zero: one pipeline less each way */
   rs.lineWidth = 1.f;

   memset(&ms, 0, sizeof(ms));
   ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms.rasterizationSamples = (VkSampleCountFlagBits)rp->samples;

   memset(&ds, 0, sizeof(ds));
   ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
   ds.depthTestEnable = s.depth_test ? VK_TRUE : VK_FALSE;
   ds.depthWriteEnable = s.depth_write ? VK_TRUE : VK_FALSE;
   ds.depthCompareOp = cmp[s.depth_cmp];

   memset(&cba, 0, sizeof(cba));
   cba.blendEnable = s.blend_en ? VK_TRUE : VK_FALSE;
   cba.srcColorBlendFactor = blend_factors[s.src];
   cba.dstColorBlendFactor = blend_factors[s.dst];
   cba.colorBlendOp = VK_BLEND_OP_ADD;
   cba.srcAlphaBlendFactor = blend_factors[s.src_a];
   cba.dstAlphaBlendFactor = blend_factors[s.dst_a];
   cba.alphaBlendOp = VK_BLEND_OP_ADD;
   cba.colorWriteMask = 0xf;

   memset(&cb, 0, sizeof(cb));
   cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   cb.attachmentCount = rp->color != VK_FORMAT_UNDEFINED ? 1 : 0;
   cb.pAttachments = &cba;

   memset(&dy, 0, sizeof(dy));
   if (r->eds3)
   {
      dyn[n++] = VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
      dyn[n++] = VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT;
      dyn[n++] = VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT;
   }
   dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   dy.dynamicStateCount = n;
   dy.pDynamicStates = dyn;

   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   info.stageCount = 2;
   info.pStages = stages;
   info.pVertexInputState = &vi;
   info.pInputAssemblyState = &ia;
   info.pViewportState = &vp;
   info.pRasterizationState = &rs;
   info.pMultisampleState = &ms;
   info.pDepthStencilState = rp->depth != VK_FORMAT_UNDEFINED ? &ds : NULL;
   info.pColorBlendState = &cb;
   info.pDynamicState = &dy;
   info.layout = r->layout;
   info.renderPass = rp->rp;

   if (F(CreateGraphicsPipelines)(r->device, r->cache, 1, &info, NULL, &pipe) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return pipe;
}

static VkShaderModule make_module(vkr_renderer *r, const uint32_t *words, unsigned count)
{
   VkShaderModuleCreateInfo info;
   VkShaderModule mod = VK_NULL_HANDLE;
   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   info.codeSize = count * sizeof(uint32_t);
   info.pCode = words;
   if (F(CreateShaderModule)(r->device, &info, NULL, &mod) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   return mod;
}

/* The worker: a combiner's module once, then its pipelines. */
static void pipeline_work(void *user, mpsc_stack_node_t *node)
{
   vkr_renderer *r = (vkr_renderer*)user;
   pipe_job *job = (pipe_job*)node;
   vkr_program *p = job->program;
   VkPipeline pipe = VK_NULL_HANDLE;

   if (!p)
      pipe = build_pipeline(r, job->vs, job->fs, job->key);
   else
   {
      if (p->fs == VK_NULL_HANDLE)
      {
         unsigned count = 0;
         const uint32_t *words = vkr_write_combiner(&r->worker_spv, &r->cfg, &p->desc, 0, &count);
         if (words)
            p->fs = make_module(r, words, count);
      }
      if (p->fs != VK_NULL_HANDLE)
         pipe = build_pipeline(r, r->vs[p->rect ? VKR_VS_RECT : VKR_VS_TRIANGLE], p->fs, job->key);
   }

   /* published in its own entry, the moment it exists */
   job->entry->pipeline = pipe;
   retro_atomic_store_release_int(&job->entry->status, pipe != VK_NULL_HANDLE ? PS_READY : PS_FAILED);
   retro_atomic_fetch_sub_int(&r->job_count, 1);
   mpsc_stack_push(&r->job_free_returned, &job->node);
}

static pipe_job *job_take(vkr_renderer *r)
{
   mpsc_stack_node_t *n = r->job_free;
   if (!n)
      n = mpsc_stack_drain(&r->job_free_returned);
   if (!n)
      return NULL;
   r->job_free = n->next;
   return (pipe_job*)n;
}

static pipe_entry *pipe_lookup(vkr_renderer *r, uint32_t prog, uint64_t key)
{
   uint64_t h = (key ^ ((uint64_t)prog * (((uint64_t)0x9E3779B9u << 32) | 0x7F4A7C15u)));
   unsigned i;
   unsigned at;
   h ^= h >> 29;
   at = (unsigned)h & (MAX_PIPELINES - 1);
   for (i = 0; i < 64; i++)
   {
      pipe_entry *e = &r->pipes[(at + i) & (MAX_PIPELINES - 1)];
      if (!e->used)
      {
         e->used = 1;
         e->prog = prog;
         e->key = key;
         return e;
      }
      if (e->key == key && e->prog == prog)
         return e;
   }
   return NULL; /* a run this long: this state stays on the generic pipeline */
}

/* Hands a pipeline to the worker; a full queue leaves it for next time. */
static int pipe_queue(vkr_renderer *r, pipe_entry *e, vkr_program *p,
      VkShaderModule vs, VkShaderModule fs, uint64_t key)
{
   pipe_job *job = job_take(r);
   if (!job)
      return 0;
   job->entry = e;
   job->program = p;
   job->vs = vs;
   job->fs = fs;
   job->key = key;
   retro_atomic_store_release_int(&e->status, PS_PENDING);
   retro_atomic_fetch_add_int(&r->job_count, 1);
   rdp_worker_push(r->worker, &job->node);
   return 1;
}

static uint32_t special_id(int vs, int fs)
{
   return 0x80000000u | ((uint32_t)vs << 8) | (uint32_t)fs;
}

/* A generic or special pipeline. prewarm built every one the renderer can
 * ask for, so this finds it. Should a state ever be missed, the draw thread
 * still never compiles: the worker builds it for the next frame. */
static VkPipeline fixed_pipeline(vkr_renderer *r, uint32_t prog, VkShaderModule vs, VkShaderModule fs, uint64_t key)
{
   pipe_entry *e = pipe_lookup(r, prog, key);
   int status = e ? retro_atomic_load_acquire_int(&e->status) : PS_FAILED;

   if (status == PS_READY)
      return e->pipeline;
   if (r->strict)
      abort(); /* tools/ci: prewarm no longer covers what the renderer draws */
   if (status == PS_EMPTY)
      pipe_queue(r, e, NULL, vs, fs, key);
   return VK_NULL_HANDLE;
}

static int bind_for_draw(vkr_renderer *r, int rect, int lines, int strip)
{
   vkr_program *p = r->current;
   VkPipeline pipe = VK_NULL_HANDLE;
   vkr_push push;
   uint64_t key;
   int generic = 0;

   if (!p)
      return 0;
   key = state_key(r, rect, lines, strip);

   if (p->is_special)
      pipe = fixed_pipeline(r, special_id(p->vs_kind, p->fs_kind), r->vs[p->vs_kind], r->special_fs[p->fs_kind], key);
   else
   {
      pipe_entry *e = pipe_lookup(r, p->id, key);
      int status = e ? retro_atomic_load_acquire_int(&e->status) : PS_FAILED;
      if (status == PS_READY && !r->force_generic)
         pipe = e->pipeline;
      else
      {
         if (status == PS_EMPTY)
            pipe_queue(r, e, p, VK_NULL_HANDLE, VK_NULL_HANDLE, key);
         pipe = fixed_pipeline(r, 0, r->vs[rect ? VKR_VS_RECT : VKR_VS_TRIANGLE], r->generic_fs, key);
         generic = 1;
      }
   }
   if (pipe == VK_NULL_HANDLE)
      return 0;
   if (pipe != r->bound_pipe)
   {
      F(CmdBindPipeline)(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      r->bound_pipe = pipe;
   }

   if (generic)
   {
      memcpy(push.ops, p->desc.ops, sizeof(push.ops));
      push.flags = p->desc.flags;
      push.clip_enable = r->clip;
      F(CmdPushConstants)(r->cmd, r->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, 72, &push);
      r->push_clip_sent = r->clip;
   }
   else if (p->is_special)
   {
      memcpy(push.special, p->special, sizeof(push.special));
      push.clip_enable = r->clip;
      F(CmdPushConstants)(r->cmd, r->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            68, 4, &push.clip_enable);
      F(CmdPushConstants)(r->cmd, r->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            80, 16, push.special);
      r->push_clip_sent = r->clip;
   }
   else if (r->push_clip_sent != r->clip)
   {
      push.clip_enable = r->clip;
      F(CmdPushConstants)(r->cmd, r->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            68, 4, &push.clip_enable);
      r->push_clip_sent = r->clip;
   }
   return 1;
}

static uint32_t image_key(const vkr_renderer *r, unsigned h)
{
   if (!h || h >= MAX_IMAGES)
      return 0;
   return (uint32_t)r->images[h].gen << 16 | h;
}

static VkDescriptorSet descriptors(vkr_renderer *r)
{
   uint32_t key[VKR_BIND_COUNT];
   uint32_t hash = 2166136261u;
   desc_entry *e = NULL;
   VkDescriptorSetAllocateInfo ai;
   VkDescriptorSet set;
   VkDescriptorImageInfo infos[VKR_BIND_COUNT];
   VkDescriptorBufferInfo ubo;
   VkWriteDescriptorSet writes[VKR_BIND_COUNT];
   unsigned u, i, n = 0;
   vkr_frame *f = r->f;

   for (u = 0; u < VKR_BIND_COUNT; u++)
   {
      key[u] = image_key(r, r->bound[u]);
      hash = (hash ^ key[u]) * 16777619u;
   }
   for (i = 0; i < 16; i++)
   {
      desc_entry *c = &r->desc_cache[(hash + i) & (DESC_CACHE - 1)];
      if (c->stamp != r->desc_stamp)
      {
         e = c;
         break;
      }
      if (memcmp(c->key, key, sizeof(key)) == 0)
         return c->set;
   }

   if (f->n_sets >= MAX_DESC_SETS)
   {
      /* the pool is spent: the GPU catches up and it starts over */
      uint32_t last;
      flush(r);
      last = f->last_serial;
      wait_serial(r, last);
      F(ResetDescriptorPool)(r->device, f->dpool, 0);
      f->n_sets = 0;
      r->desc_stamp++;
      e = NULL;
   }

   memset(&ai, 0, sizeof(ai));
   ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   ai.descriptorPool = f->dpool;
   ai.descriptorSetCount = 1;
   ai.pSetLayouts = &r->set_layout;
   if (F(AllocateDescriptorSets)(r->device, &ai, &set) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   f->n_sets++;

   ubo.buffer = f->ring;
   ubo.offset = 0;
   ubo.range = sizeof(CombinerValues);
   memset(writes, 0, sizeof(writes));
   writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   writes[n].dstSet = set;
   writes[n].dstBinding = VKR_BIND_VALUES;
   writes[n].descriptorCount = 1;
   writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
   writes[n].pBufferInfo = &ubo;
   n++;

   for (u = VKR_BIND_TEX0; u < VKR_BIND_COUNT; u++)
   {
      unsigned h = r->bound[u];
      const vkr_image *im;
      int storage = u == VKR_BIND_DEPTH_Z || u == VKR_BIND_DEPTH_DZ;
      if (storage && !r->cfg.n64_depth_compare)
         continue;
      if (u == VKR_BIND_MS0 || u == VKR_BIND_MS1)
      {
         if (r->cfg.msaa == 0)
            continue;
         if (!h || r->images[h].samples <= 1 || r->images[h].image == VK_NULL_HANDLE)
            h = r->dummy_ms;
      }
      else if (u == VKR_BIND_ZLUT || u == VKR_BIND_TLUT)
      {
         if (!h || r->images[h].image == VK_NULL_HANDLE)
            h = r->dummy_uint;
      }
      else if (storage)
      {
         if (!h || r->images[h].image == VK_NULL_HANDLE)
            h = r->dummy_storage;
      }
      else if (u == VKR_BIND_DEPTH)
      {
         if (!h || r->images[h].image == VK_NULL_HANDLE)
            h = r->dummy_depth;
      }
      else
      {
         if (!h || r->images[h].image == VK_NULL_HANDLE)
            h = r->dummy_color;
      }
      im = &r->images[h];
      infos[u].sampler = im->sampler != VK_NULL_HANDLE ? im->sampler : r->samplers[0].s;
      infos[u].imageView = im->view;
      infos[u].imageLayout = storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[n].dstSet = set;
      writes[n].dstBinding = u;
      writes[n].descriptorCount = 1;
      writes[n].descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[n].pImageInfo = &infos[u];
      n++;
   }
   F(UpdateDescriptorSets)(r->device, n, writes, 0, NULL);

   if (e)
   {
      memcpy(e->key, key, sizeof(key));
      e->set = set;
      e->stamp = r->desc_stamp;
   }
   return set;
}

static int draw_begin(vkr_renderer *r, int rect, int lines, int strip)
{
   VkDescriptorSet set;
   VkBuffer ring;

   frame_begin(r);
   if (!r->current)
      return 0;

   prepare_textures(r);
   if (!begin_pass(r))
      return 0;

   if (r->dirty)
      flush_dynamic(r);
   if (!bind_for_draw(r, rect, lines, strip))
      return 0;

   set = descriptors(r);
   if (set == VK_NULL_HANDLE)
      return 0;
   if (set != r->bound_set || r->values_offset != r->bound_set_offset)
   {
      uint32_t off = r->values_offset;
      F(CmdBindDescriptorSets)(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &set, 1, &off);
      r->bound_set = set;
      r->bound_set_offset = off;
   }
   /* a flush since the reserve lost the bindings */
   if (!r->vb_valid)
   {
      ring = r->f->ring;
      F(CmdBindVertexBuffers)(r->cmd, 0, 1, &ring, &r->vb_offset);
      r->vb_valid = 1;
   }
   if (!r->ib_valid)
   {
      F(CmdBindIndexBuffer)(r->cmd, r->f->ring, r->ib_offset, VK_INDEX_TYPE_UINT16);
      r->ib_valid = 1;
   }
   return 1;
}

vkr_tri_vertex *vkr_tri_reserve(vkr_renderer *r, unsigned count)
{
   VkDeviceSize offset;
   vkr_tri_vertex *v = (vkr_tri_vertex*)ring_reserve(r, count * sizeof(vkr_tri_vertex), sizeof(float), &offset);
   r->vb_offset = offset;
   r->vb_valid = 1;
   F(CmdBindVertexBuffers)(r->cmd, 0, 1, &r->f->ring, &r->vb_offset);
   return v;
}

vkr_rect_vertex *vkr_rect_reserve(vkr_renderer *r, unsigned count)
{
   VkDeviceSize offset;
   vkr_rect_vertex *v = (vkr_rect_vertex*)ring_reserve(r, count * sizeof(vkr_rect_vertex), sizeof(float), &offset);
   r->vb_offset = offset;
   r->vb_valid = 1;
   F(CmdBindVertexBuffers)(r->cmd, 0, 1, &r->f->ring, &r->vb_offset);
   return v;
}

uint16_t *vkr_index_reserve(vkr_renderer *r, unsigned count)
{
   VkDeviceSize offset;
   uint16_t *i = (uint16_t*)ring_reserve(r, count * sizeof(uint16_t), 2, &offset);
   r->ib_offset = offset;
   r->ib_valid = 1;
   F(CmdBindIndexBuffer)(r->cmd, r->f->ring, offset, VK_INDEX_TYPE_UINT16);
   return i;
}

static void set_topology(vkr_renderer *r, int topology)
{
   VkPrimitiveTopology t = topology == VKR_TOPO_STRIP ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
      : (topology == VKR_TOPO_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
   if (r->eds)
      F(CmdSetPrimitiveTopologyEXT)(r->cmd, t);
}

void vkr_draw_tris(vkr_renderer *r, int topology, unsigned vertices, unsigned indices)
{
   if (!draw_begin(r, 0, topology == VKR_TOPO_LINES, topology == VKR_TOPO_STRIP))
      return;
   set_topology(r, topology);
   if (indices)
      F(CmdDrawIndexed)(r->cmd, indices, 1, 0, 0, 0);
   else
      F(CmdDraw)(r->cmd, vertices, 1, 0, 0);
}

void vkr_draw_rects(vkr_renderer *r, int topology, unsigned vertices)
{
   if (!draw_begin(r, 1, 0, topology == VKR_TOPO_STRIP))
      return;
   set_topology(r, topology);
   F(CmdDraw)(r->cmd, vertices, 1, 0, 0);
}

void vkr_draw_line(vkr_renderer *r, unsigned vertices, float width)
{
   if (!draw_begin(r, 0, 1, 0))
      return;
   if (r->line_width != width)
   {
      r->line_width = width;
      F(CmdSetLineWidth)(r->cmd, width);
   }
   set_topology(r, VKR_TOPO_LINES);
   F(CmdDraw)(r->cmd, vertices, 1, 0, 0);
}

/* ---- state setters ---- */
void vkr_enable(vkr_renderer *r, int cap, int on)
{
   if (r->enable[cap] == on)
      return;
   r->enable[cap] = on;
   if (cap == VKR_CAP_SCISSOR)
      r->dirty |= D_SCISSOR;
   else if (cap == VKR_CAP_CULL || cap == VKR_CAP_DEPTH_TEST)
      r->dirty |= D_EDS;
   else if (cap == VKR_CAP_POLY_OFFSET)
      r->dirty |= D_BIAS;
   else if (cap == VKR_CAP_BLEND)
      r->dirty |= D_EDS3;
}

int vkr_is_enabled(const vkr_renderer *r, int cap) { return r->enable[cap]; }

void vkr_set_clamp(vkr_renderer *r, int depth_clamp, int clip)
{
   r->depth_clamp = depth_clamp;
   r->dirty |= D_EDS3;
   r->clip = clip;
}

void vkr_cull(vkr_renderer *r, int mode)
{
   if (r->cull_mode != mode)
   {
      r->cull_mode = mode;
      r->dirty |= D_EDS;
   }
}

void vkr_depth_write(vkr_renderer *r, int on)
{
   if (r->depth_write_on != on)
   {
      r->depth_write_on = on;
      r->dirty |= D_EDS;
   }
}

void vkr_depth_compare(vkr_renderer *r, int cmp)
{
   if (r->depth_cmp != cmp)
   {
      r->depth_cmp = cmp;
      r->dirty |= D_EDS;
   }
}

void vkr_viewport(vkr_renderer *r, int x, int y, int w, int h)
{
   r->viewport.x = (float)x;
   r->viewport.y = (float)y;
   r->viewport.width = (float)w;
   r->viewport.height = (float)h;
   r->viewport.minDepth = 0.f;
   r->viewport.maxDepth = 1.f;
   r->dirty |= D_VIEWPORT;
}

void vkr_scissor(vkr_renderer *r, int x, int y, int w, int h)
{
   r->scissor.offset.x = x;
   r->scissor.offset.y = y;
   r->scissor.extent.width = (uint32_t)(w < 0 ? 0 : w);
   r->scissor.extent.height = (uint32_t)(h < 0 ? 0 : h);
   r->dirty |= D_SCISSOR;
}

void vkr_blend(vkr_renderer *r, int src, int dst, int src_a, int dst_a)
{
   r->st.src = (unsigned)src;
   r->st.dst = (unsigned)dst;
   r->dirty |= D_EDS3;
   r->st.src_a = (unsigned)src_a;
   r->st.dst_a = (unsigned)dst_a;
}

void vkr_blend_color(vkr_renderer *r, float cr, float g, float b, float a)
{
   r->blend_color[0] = cr;
   r->blend_color[1] = g;
   r->blend_color[2] = b;
   r->blend_color[3] = a;
   r->dirty |= D_BLEND;
}

void vkr_polygon_offset(vkr_renderer *r, float factor, float units)
{
   r->bias_factor = factor;
   r->bias_units = units;
   r->dirty |= D_BIAS;
}

static void clear_attachment(vkr_renderer *r, VkImageAspectFlags aspect, const VkClearValue *v, unsigned w, unsigned h)
{
   VkClearAttachment a;
   VkClearRect rect;
   memset(&a, 0, sizeof(a));
   memset(&rect, 0, sizeof(rect));
   a.aspectMask = aspect;
   a.clearValue = *v;
   rect.rect.extent.width = w;
   rect.rect.extent.height = h;
   rect.layerCount = 1;
   F(CmdClearAttachments)(r->cmd, 1, &a, 1, &rect);
}

static void target_size(vkr_renderer *r, unsigned *w, unsigned *h)
{
   if (r->draw_fbo)
   {
      *w = r->fbos[r->draw_fbo].w;
      *h = r->fbos[r->draw_fbo].h;
   }
   else
   {
      *w = r->f->win_w;
      *h = r->f->win_h;
   }
}

void vkr_clear_color(vkr_renderer *r, float cr, float g, float b, float a)
{
   VkClearValue v;
   unsigned w, h;
   frame_begin(r);
   if (r->draw_fbo && !r->fbos[r->draw_fbo].color)
      return;
   if (!begin_pass(r))
      return;
   v.color.float32[0] = cr;
   v.color.float32[1] = g;
   v.color.float32[2] = b;
   v.color.float32[3] = a;
   target_size(r, &w, &h);
   clear_attachment(r, VK_IMAGE_ASPECT_COLOR_BIT, &v, w, h);
}

void vkr_clear_depth(vkr_renderer *r)
{
   VkClearValue v;
   unsigned w, h;
   frame_begin(r);
   if (!r->draw_fbo || !r->fbos[r->draw_fbo].depth)
      return;
   if (!begin_pass(r))
      return;
   v.depthStencil.depth = 1.f;
   v.depthStencil.stencil = 0;
   target_size(r, &w, &h);
   clear_attachment(r, VK_IMAGE_ASPECT_DEPTH_BIT, &v, w, h);
}

/* ---- blit ---- */

/* GL clips the source rectangle, scaling the destination with it. */
static void blit_clip_axis(int size, int *s0, int *s1, int *d0, int *d1)
{
   int sa = *s0, sb = *s1, da = *d0, db = *d1;
   int lo = sa < sb ? sa : sb, hi = sa < sb ? sb : sa;
   float scale;
   int len = hi - lo;

   if (lo >= 0 && hi <= size)
      return;
   scale = len ? (float)(db - da) / (float)(sb - sa) : 0.f;
   if (lo < 0)
      lo = 0;
   if (hi > size)
      hi = size;
   if (sa < sb)
   {
      *d0 = da + (int)((float)(lo - sa) * scale);
      *d1 = da + (int)((float)(hi - sa) * scale);
      *s0 = lo;
      *s1 = hi;
   }
   else
   {
      *d0 = da + (int)((float)(hi - sa) * scale);
      *d1 = da + (int)((float)(lo - sa) * scale);
      *s0 = hi;
      *s1 = lo;
   }
}

static void blit_clip(unsigned w, unsigned h, int *sx0, int *sy0, int *sx1, int *sy1,
      int *dx0, int *dy0, int *dx1, int *dy1)
{
   blit_clip_axis((int)w, sx0, sx1, dx0, dx1);
   blit_clip_axis((int)h, sy0, sy1, dy0, dy1);
}

int vkr_blit(vkr_renderer *r, unsigned read, unsigned draw,
      int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1,
      int color, int depth, int linear)
{
   vkr_image *src, *dst;
   vkr_image dst_window;
   int window = draw == 0;
   VkImageBlit b;
   VkImageResolve rv;

   frame_begin(r);
   end_pass(r);

   if (color)
   {
      if (!read || !r->fbos[read].color)
         return 0;
      src = &r->images[r->fbos[read].color];
      if (src->image == VK_NULL_HANDLE)
         return 0;
      blit_clip(src->w, src->h, &sx0, &sy0, &sx1, &sy1, &dx0, &dy0, &dx1, &dy1);
      if (window)
      {
         window_image(r, r->f);
         memset(&dst_window, 0, sizeof(dst_window));
         dst_window.image = r->f->win_image;
         dst_window.layout = r->f->win_layout;
         dst_window.w = (uint16_t)r->f->win_w;
         dst_window.h = (uint16_t)r->f->win_h;
         dst_window.samples = 1;
         dst = &dst_window;
         /* GL's window is bottom-up, this one is top-down */
         dy0 = dst_window.h - dy0;
         dy1 = dst_window.h - dy1;
      }
      else
      {
         if (!r->fbos[draw].color)
            return 0;
         dst = &r->images[r->fbos[draw].color];
      }
      if (src->image == VK_NULL_HANDLE || dst->image == VK_NULL_HANDLE)
         return 0;
      ensure_layout(r, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      src->used_serial = r->serial;
      if (dst->layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
      {
         image_barrier(r, r->cmd, dst->image, VK_IMAGE_ASPECT_COLOR_BIT, dst->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
         dst->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      }
      dst->used_serial = r->serial;
      if (window)
         r->f->win_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

      if (src->samples > 1)
      {
         memset(&rv, 0, sizeof(rv));
         rv.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         rv.srcSubresource.layerCount = 1;
         rv.dstSubresource = rv.srcSubresource;
         rv.srcOffset.x = sx0 < sx1 ? sx0 : sx1;
         rv.srcOffset.y = sy0 < sy1 ? sy0 : sy1;
         rv.dstOffset.x = dx0 < dx1 ? dx0 : dx1;
         rv.dstOffset.y = dy0 < dy1 ? dy0 : dy1;
         rv.extent.width = (uint32_t)abs(sx1 - sx0);
         rv.extent.height = (uint32_t)abs(sy1 - sy0);
         rv.extent.depth = 1;
         F(CmdResolveImage)(r->cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rv);
      }
      else
      {
         memset(&b, 0, sizeof(b));
         b.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         b.srcSubresource.layerCount = 1;
         b.dstSubresource = b.srcSubresource;
         b.srcOffsets[0].x = sx0; b.srcOffsets[0].y = sy0; b.srcOffsets[1].x = sx1; b.srcOffsets[1].y = sy1;
         b.srcOffsets[1].z = 1;
         b.dstOffsets[0].x = dx0; b.dstOffsets[0].y = dy0; b.dstOffsets[1].x = dx1; b.dstOffsets[1].y = dy1;
         b.dstOffsets[1].z = 1;
         F(CmdBlitImage)(r->cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
      }
   }

   if (depth)
   {
      if (!read || !draw || !r->fbos[read].depth || !r->fbos[draw].depth)
         return color;
      src = &r->images[r->fbos[read].depth];
      dst = &r->images[r->fbos[draw].depth];
      if (src->image == VK_NULL_HANDLE || dst->image == VK_NULL_HANDLE)
         return color;
      blit_clip(src->w, src->h, &sx0, &sy0, &sx1, &sy1, &dx0, &dy0, &dx1, &dy1);
      ensure_layout(r, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      ensure_layout(r, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      src->used_serial = r->serial;
      dst->used_serial = r->serial;
      memset(&b, 0, sizeof(b));
      b.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      b.srcSubresource.layerCount = 1;
      b.dstSubresource = b.srcSubresource;
      b.srcOffsets[0].x = sx0; b.srcOffsets[0].y = sy0; b.srcOffsets[1].x = sx1; b.srcOffsets[1].y = sy1;
      b.srcOffsets[1].z = 1;
      b.dstOffsets[0].x = dx0; b.dstOffsets[0].y = dy0; b.dstOffsets[1].x = dx1; b.dstOffsets[1].y = dy1;
      b.dstOffsets[1].z = 1;
      F(CmdBlitImage)(r->cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
   }
   return 1;
}

/* ---- readback ---- */
vkr_readback *vkr_readback_new(vkr_renderer *r, size_t bytes)
{
   vkr_readback *rb = (vkr_readback*)calloc(1, sizeof(*rb));
   VkBufferCreateInfo info;
   VkMemoryRequirements req;
   VkMemoryAllocateInfo ma;
   int type;

   if (!rb)
      return NULL;
   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   info.size = bytes;
   info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
   if (F(CreateBuffer)(r->device, &info, NULL, &rb->buffer) != VK_SUCCESS)
   {
      free(rb);
      return NULL;
   }
   F(GetBufferMemoryRequirements)(r->device, rb->buffer, &req);
   type = find_memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
   if (type < 0)
      type = find_memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   memset(&ma, 0, sizeof(ma));
   ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   ma.allocationSize = req.size;
   ma.memoryTypeIndex = (uint32_t)type;
   F(AllocateMemory)(r->device, &ma, NULL, &rb->mem);
   F(BindBufferMemory)(r->device, rb->buffer, rb->mem, 0);
   F(MapMemory)(r->device, rb->mem, 0, VK_WHOLE_SIZE, 0, &rb->map);
   rb->bytes = bytes;
   return rb;
}

void vkr_readback_delete(vkr_renderer *r, vkr_readback *rb)
{
   if (!rb)
      return;
   trash(r, G_BUFFER, (uint64_t)(uintptr_t)rb->buffer, NULL);
   trash(r, G_MEMORY, (uint64_t)(uintptr_t)rb->mem, NULL);
   free(rb);
}

static void readback_copy(vkr_renderer *r, vkr_readback *rb, vkr_image *i, int x, int y, unsigned w, unsigned h)
{
   VkBufferImageCopy c;
   frame_begin(r);
   end_pass(r);
   ensure_layout(r, i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
   i->used_serial = r->serial;
   memset(&c, 0, sizeof(c));
   c.imageSubresource.aspectMask = aspect_of(i);
   c.imageSubresource.layerCount = 1;
   c.imageOffset.x = x;
   c.imageOffset.y = y;
   c.imageExtent.width = w;
   c.imageExtent.height = h;
   c.imageExtent.depth = 1;
   F(CmdCopyImageToBuffer)(r->cmd, i->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb->buffer, 1, &c);
   rb->serial = r->serial;
}

void vkr_readback_tex(vkr_renderer *r, vkr_readback *rb, unsigned tex, int x, int y, unsigned w, unsigned hgt)
{
   vkr_image *i;
   if (!tex || tex >= MAX_IMAGES)
      return;
   i = &r->images[tex];
   if (i->image == VK_NULL_HANDLE)
      return;
   readback_copy(r, rb, i, x, y, w, hgt);
}

void vkr_readback_fbo(vkr_renderer *r, vkr_readback *rb, int x, int y, unsigned w, unsigned hgt, int depth)
{
   vkr_fbo *o = &r->fbos[r->read_fbo];
   unsigned t = depth ? o->depth : o->color;
   vkr_readback_tex(r, rb, t, x, y, w, hgt);
}

int vkr_readback_ready(vkr_renderer *r, vkr_readback *rb, int wait)
{
   unsigned i;
   if (!rb->serial)
      return 1;
   if (rb->serial == r->serial && r->frame_open)
   {
      /* still being recorded: a wait is a real CPU reader, so it goes now */
      if (!wait)
         return 0;
      flush(r);
   }
   i = rb->serial % MAX_SUBMITS;
   if (r->fence_serial[i] != rb->serial)
      return 1;
   if (wait)
   {
      F(WaitForFences)(r->device, 1, &r->fences[i], VK_TRUE, ~(uint64_t)0);
      return 1;
   }
   return F(GetFenceStatus)(r->device, r->fences[i]) == VK_SUCCESS;
}

const void *vkr_readback_data(vkr_readback *rb)
{
   return rb->map;
}

/* ---- programs ---- */
static bool jobs_done(void *ctx)
{
   vkr_renderer *r = (vkr_renderer*)ctx;
   return retro_atomic_load_acquire_int(&r->job_count) == 0;
}

static vkr_program *program_new(vkr_renderer *r)
{
   vkr_program *p;
   if (r->n_programs < MAX_PROGRAMS)
      p = &r->programs[r->n_programs++];
   else
      p = (vkr_program*)calloc(1, sizeof(*p));
   if (!p)
      return NULL;
   memset(p, 0, sizeof(*p));
   p->r = r;
   p->id = ++r->next_program;
   return p;
}

vkr_program *vkr_combiner_new(vkr_renderer *r, const vkr_combiner_desc *desc, int rect)
{
   vkr_program *p = program_new(r);
   if (!p)
      return NULL;
   p->desc = *desc;
   p->rect = rect;
   return p;
}

vkr_program *vkr_special_new(vkr_renderer *r, int vs_kind, int fs_kind)
{
   vkr_program *p = program_new(r);
   if (!p)
      return NULL;
   p->is_special = 1;
   p->vs_kind = vs_kind;
   p->fs_kind = fs_kind;
   return p;
}

void vkr_program_delete(vkr_renderer *r, vkr_program *p)
{
   if (!p)
      return;
   /* its pipelines were published under its id and stay valid; the jobs
    * referring to it must be done first */
   rdp_worker_wait(r->worker, jobs_done, r);
   if (r->current == p)
      r->current = NULL;
   if (p->fs != VK_NULL_HANDLE)
      F(DestroyShaderModule)(r->device, p->fs, NULL);
   if (p < r->programs || p >= r->programs + MAX_PROGRAMS)
      free(p);
}

void vkr_use(vkr_renderer *r, vkr_program *p)
{
   r->current = p;
   if (p && p->has_slot && p->slot_stamp == r->desc_stamp)
      r->values_offset = p->slot_offset;
}

void vkr_values(vkr_renderer *r, vkr_program *p, const CombinerValues *v)
{
   VkDeviceSize offset;
   void *dst;

   frame_begin(r);
   /* Nothing changed since this frame's slot: the GPU reads that one. */
   if (p->has_slot && p->slot_stamp == r->desc_stamp && memcmp(p->slot, v, sizeof(*v)) == 0)
   {
      r->values_offset = p->slot_offset;
      return;
   }
   dst = ring_reserve(r, sizeof(*v), r->props.limits.minUniformBufferOffsetAlignment, &offset);
   memcpy(dst, v, sizeof(*v));
   p->slot = (const CombinerValues*)dst;
   p->slot_offset = (uint32_t)offset;
   p->slot_stamp = r->desc_stamp;
   p->has_slot = 1;
   r->values_offset = (uint32_t)offset;
}

void vkr_special_params(vkr_renderer *r, vkr_program *p, float a, float b, float c, float d)
{
   (void)r;
   p->special[0] = a;
   p->special[1] = b;
   p->special[2] = c;
   p->special[3] = d;
}

/* ---- present ---- */
void vkr_set_window(vkr_renderer *r, unsigned w, unsigned h)
{
   r->window_w = w;
   r->window_h = h;
   if (r->viewport.width == 1.f && r->viewport.height == 1.f)
   {
      r->viewport.width = (float)w;
      r->viewport.height = (float)h;
   }
}

void vkr_present(vkr_renderer *r)
{
   vkr_frame *f;
   struct retro_vulkan_image image;

   frame_begin(r);
   f = r->f;
   window_image(r, f);
   end_pass(r);
   if (f->win_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
   {
      image_barrier(r, r->cmd, f->win_image, VK_IMAGE_ASPECT_COLOR_BIT, f->win_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      f->win_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   }
   submit(r);

   memset(&image, 0, sizeof(image));
   image.image_view = f->win_view;
   image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   image.create_info.image = f->win_image;
   image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   image.create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
   image.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   image.create_info.subresourceRange.levelCount = 1;
   image.create_info.subresourceRange.layerCount = 1;
   r->vk->set_image(r->vk->handle, &image, 0, NULL, VK_QUEUE_FAMILY_IGNORED);

   r->frame_open = 0;
   r->f = NULL;
}

const vkr_caps *vkr_get_caps(const vkr_renderer *r) { return &r->caps; }

/* ---- the driver's pipeline cache, across sessions ---- */

/* A cache of another GPU or driver, or a damaged one, is a miss. */
static int load_cache(vkr_renderer *r, const char *path, void **blob, size_t *size)
{
   RFILE *f;
   int64_t n;
   void *data;
   const uint32_t *h;

   if (!path || !*path)
      return 0;
   f = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!f)
      return 0;
   n = filestream_get_size(f);
   if (n < 32 || n > 256 * 1024 * 1024)
   {
      filestream_close(f);
      return 0;
   }
   data = malloc((size_t)n);
   if (!data || filestream_read(f, data, n) != n)
   {
      free(data);
      filestream_close(f);
      return 0;
   }
   filestream_close(f);
   h = (const uint32_t*)data;
   if (h[0] < 32 || h[1] != VK_PIPELINE_CACHE_HEADER_VERSION_ONE
         || h[2] != r->props.vendorID || h[3] != r->props.deviceID
         || memcmp(&h[4], r->props.pipelineCacheUUID, VK_UUID_SIZE) != 0)
   {
      free(data);
      return 0;
   }
   *blob = data;
   *size = (size_t)n;
   return 1;
}

static void save_cache(vkr_renderer *r)
{
   size_t size = 0;
   void *data;
   RFILE *f;

   if (!r->cache_path[0] || r->cache == VK_NULL_HANDLE
         || F(GetPipelineCacheData)(r->device, r->cache, &size, NULL) != VK_SUCCESS || !size)
      return;
   data = malloc(size);
   if (!data)
      return;
   if (F(GetPipelineCacheData)(r->device, r->cache, &size, data) == VK_SUCCESS)
   {
      char dir[1024];
      /* a first run has no directory yet, and the save would be lost */
      fill_pathname_basedir(dir, r->cache_path, sizeof(dir));
      path_mkdir(dir);
      f = filestream_open(r->cache_path, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (f)
      {
         filestream_write(f, data, (int64_t)size);
         filestream_close(f);
      }
   }
   free(data);
}

/* ---- creation ---- */
static int create_frame(vkr_renderer *r, vkr_frame *f)
{
   VkCommandPoolCreateInfo cpi;
   VkCommandBufferAllocateInfo cai;
   VkBufferCreateInfo bi;
   VkMemoryRequirements req;
   VkMemoryAllocateInfo ma;
   VkDescriptorPoolCreateInfo dpi;
   VkDescriptorPoolSize sizes[3];
   int type;

   memset(&cpi, 0, sizeof(cpi));
   cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cpi.queueFamilyIndex = r->vk->queue_index;
   if (F(CreateCommandPool)(r->device, &cpi, NULL, &f->pool) != VK_SUCCESS)
      return 0;
   memset(&cai, 0, sizeof(cai));
   cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cai.commandPool = f->pool;
   cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cai.commandBufferCount = MAX_SUBMITS;
   if (F(AllocateCommandBuffers)(r->device, &cai, f->cmd) != VK_SUCCESS
         || F(AllocateCommandBuffers)(r->device, &cai, f->xfer) != VK_SUCCESS)
      return 0;

   memset(&bi, 0, sizeof(bi));
   bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bi.size = RING_BYTES;
   bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
   if (F(CreateBuffer)(r->device, &bi, NULL, &f->ring) != VK_SUCCESS)
      return 0;
   F(GetBufferMemoryRequirements)(r->device, f->ring, &req);
   type = find_memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   if (type < 0)
      return 0;
   memset(&ma, 0, sizeof(ma));
   ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   ma.allocationSize = req.size;
   ma.memoryTypeIndex = (uint32_t)type;
   if (F(AllocateMemory)(r->device, &ma, NULL, &f->ring_mem) != VK_SUCCESS)
      return 0;
   F(BindBufferMemory)(r->device, f->ring, f->ring_mem, 0);
   if (F(MapMemory)(r->device, f->ring_mem, 0, VK_WHOLE_SIZE, 0, (void**)&f->ring_map) != VK_SUCCESS)
      return 0;

   memset(&dpi, 0, sizeof(dpi));
   sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
   sizes[0].descriptorCount = MAX_DESC_SETS;
   sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   sizes[1].descriptorCount = MAX_DESC_SETS * 8;
   sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
   sizes[2].descriptorCount = MAX_DESC_SETS * 2;
   dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   dpi.maxSets = MAX_DESC_SETS;
   dpi.poolSizeCount = r->cfg.n64_depth_compare ? 3 : 2;
   dpi.pPoolSizes = sizes;
   if (F(CreateDescriptorPool)(r->device, &dpi, NULL, &f->dpool) != VK_SUCCESS)
      return 0;
   return 1;
}

static int create_layouts(vkr_renderer *r)
{
   VkDescriptorSetLayoutBinding b[VKR_BIND_COUNT];
   VkDescriptorSetLayoutCreateInfo dsl;
   VkPipelineLayoutCreateInfo pl;
   VkPushConstantRange range;
   unsigned u, n = 0;

   memset(b, 0, sizeof(b));
   b[n].binding = VKR_BIND_VALUES;
   b[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
   b[n].descriptorCount = 1;
   b[n].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
   n++;
   for (u = VKR_BIND_TEX0; u < VKR_BIND_COUNT; u++)
   {
      int storage = u == VKR_BIND_DEPTH_Z || u == VKR_BIND_DEPTH_DZ;
      if (storage && !r->cfg.n64_depth_compare)
         continue;
      if ((u == VKR_BIND_MS0 || u == VKR_BIND_MS1) && r->cfg.msaa == 0)
         continue;
      b[n].binding = u;
      b[n].descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b[n].descriptorCount = 1;
      b[n].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      n++;
   }
   memset(&dsl, 0, sizeof(dsl));
   dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dsl.bindingCount = n;
   dsl.pBindings = b;
   if (F(CreateDescriptorSetLayout)(r->device, &dsl, NULL, &r->set_layout) != VK_SUCCESS)
      return 0;

   range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
   range.offset = 0;
   range.size = sizeof(vkr_push);
   memset(&pl, 0, sizeof(pl));
   pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pl.setLayoutCount = 1;
   pl.pSetLayouts = &r->set_layout;
   pl.pushConstantRangeCount = 1;
   pl.pPushConstantRanges = &range;
   return F(CreatePipelineLayout)(r->device, &pl, NULL, &r->layout) == VK_SUCCESS;
}

static int create_shaders(vkr_renderer *r)
{
   spv_module *m = &r->worker_spv;
   vkr_combiner_desc none;
   const uint32_t *w;
   unsigned count = 0;
   int k;

   for (k = 0; k < VKR_VS_COUNT; k++)
   {
      w = vkr_write_vertex(m, &r->cfg, k, &count);
      if (!w || (r->vs[k] = make_module(r, w, count)) == VK_NULL_HANDLE)
         return 0;
   }
   for (k = 0; k < VKR_FS_COUNT; k++)
   {
      w = vkr_write_special(m, &r->cfg, k, &count);
      if (!w || (r->special_fs[k] = make_module(r, w, count)) == VK_NULL_HANDLE)
         return 0;
   }
   memset(&none, 0, sizeof(none));
   w = vkr_write_combiner(m, &r->cfg, &none, 1, &count);
   return w && (r->generic_fs = make_module(r, w, count)) != VK_NULL_HANDLE;
}

static void load_functions(vkr_renderer *r)
{
   PFN_vkGetDeviceProcAddr get = r->vk->get_device_proc_addr;
#define X(N) r->fn.N = (PFN_vk##N)get(r->device, "vk" #N);
   VKR_DEVICE_FUNCS(X)
   VKR_EDS_FUNCS(X)
#undef X
}

/* Queues one pipeline; at load, and only here, waits for room. */
static void prewarm_one(vkr_renderer *r, uint32_t prog, VkShaderModule vs, VkShaderModule fs, uint64_t key)
{
   pipe_entry *e = pipe_lookup(r, prog, key);
   if (!e || retro_atomic_load_acquire_int(&e->status) != PS_EMPTY)
      return;
   while (!pipe_queue(r, e, NULL, vs, fs, key))
      rdp_worker_wait(r->worker, jobs_done, r);
}

/* Every generic and special pipeline the renderer can ask for, before the
 * first frame, so no draw ever compiles or waits. The pairs are all the ones
 * _setBlendMode and _legacySetBlendMode can set. */
static void prewarm(vkr_renderer *r)
{
   static const unsigned char pairs[20][2] = {
      { VKR_BLEND_ONE, VKR_BLEND_ZERO },
      { VKR_BLEND_ONE, VKR_BLEND_ONE },
      { VKR_BLEND_ONE, VKR_BLEND_SRC_ALPHA },
      { VKR_BLEND_ONE, VKR_BLEND_INV_SRC_ALPHA },
      { VKR_BLEND_ONE, VKR_BLEND_DST_ALPHA },
      { VKR_BLEND_ONE, VKR_BLEND_CONST_ALPHA },
      { VKR_BLEND_ONE, VKR_BLEND_INV_CONST_ALPHA },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_ZERO },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_ONE },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_SRC_ALPHA },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_INV_SRC_ALPHA },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_DST_ALPHA },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_CONST_ALPHA },
      { VKR_BLEND_DST_ALPHA, VKR_BLEND_INV_CONST_ALPHA },
      { VKR_BLEND_ZERO, VKR_BLEND_ONE },
      { VKR_BLEND_ZERO, VKR_BLEND_INV_SRC_ALPHA },
      { VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA },
      { VKR_BLEND_SRC_ALPHA, VKR_BLEND_ONE },
      { VKR_BLEND_SRC_ALPHA, VKR_BLEND_DST_ALPHA },
      { VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_SRC_ALPHA }
   };
   /* the fs of every special, and the vs each goes with */
   static const unsigned char specials[VKR_FS_COUNT][2] = {
      { VKR_VS_SPECIAL, VKR_FS_SHADOW_MAP },
      { VKR_VS_SPECIAL, VKR_FS_TEXRECT_DRAW },
      { VKR_VS_SPECIAL, VKR_FS_TEXRECT_CLEAR },
      { VKR_VS_SPECIAL, VKR_FS_TEXRECT_COPY },
      { VKR_VS_SPECIAL, VKR_FS_GAMMA },
      { VKR_VS_FXAA, VKR_FS_FXAA }
   };
   int rps[6];
   unsigned n_rps = 0, n_full, p, clampf, shape, k, i;
   int depth;

   rps[n_rps++] = get_render_pass(r, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 1);
   rps[n_rps++] = get_render_pass(r, VK_FORMAT_R8G8B8A8_UNORM, r->depth_format, 1);
   if (r->cfg.msaa)
   {
      rps[n_rps++] = get_render_pass(r, VK_FORMAT_R8G8B8A8_UNORM, r->depth_format, (uint32_t)r->cfg.msaa);
      rps[n_rps++] = get_render_pass(r, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, (uint32_t)r->cfg.msaa);
   }
   n_full = n_rps;
   /* the monochrome targets: every render pass exists before the first draw */
   rps[n_rps++] = get_render_pass(r, VK_FORMAT_R8_UNORM, VK_FORMAT_UNDEFINED, 1);
   rps[n_rps++] = get_render_pass(r, VK_FORMAT_R8_UNORM, r->depth_format, 1);

   for (k = 0; k < n_rps; k++)
   {
      if (rps[k] < 0)
         continue;
      /* each state of its own would be a lot of pipelines for targets this rare */
      if (k >= n_full && !r->eds3)
         continue;
      r->st.rp = (unsigned)rps[k];
      for (depth = 0; depth < (r->eds ? 1 : 2); depth++)
      for (p = 0; p <= 20; p++)
      for (clampf = 0; clampf < 2; clampf++)
      for (shape = 0; shape < 3; shape++) /* triangles, rects, lines */
      {
         r->enable[VKR_CAP_BLEND] = p != 0;
         if (p)
         {
            r->st.src = r->st.src_a = pairs[p - 1][0];
            r->st.dst = r->st.dst_a = pairs[p - 1][1];
         }
         r->depth_clamp = (int)clampf;
         r->enable[VKR_CAP_DEPTH_TEST] = depth;
         prewarm_one(r, 0, r->vs[shape == 1 ? VKR_VS_RECT : VKR_VS_TRIANGLE], r->generic_fs,
               state_key(r, shape == 1, shape == 2, 0));
         if (shape == 2)
            continue;
         for (i = 0; i < VKR_FS_COUNT; i++)
            /* only the shadow map is drawn blended: the rest turn it off */
            if (p == 0 || specials[i][1] == VKR_FS_SHADOW_MAP)
               prewarm_one(r, special_id(specials[i][0], specials[i][1]), r->vs[specials[i][0]],
                     r->special_fs[specials[i][1]], state_key(r, shape == 1, 0, 0));
      }
   }
   r->enable[VKR_CAP_BLEND] = 0;
   r->enable[VKR_CAP_DEPTH_TEST] = 0;
   r->depth_clamp = 0;
   r->st.src = r->st.dst = r->st.src_a = r->st.dst_a = 0;
   rdp_worker_wait(r->worker, jobs_done, r);
   r->prewarmed = 1;
}

static unsigned make_dummy(vkr_renderer *r, int fmt, unsigned samples)
{
   unsigned h = vkr_texture_new(r);
   vkr_texture_init(r, h, fmt, samples, 1, 1, 0, 1, NULL, 0);
   return h;
}

vkr_renderer *vkr_create(const struct retro_hw_render_interface_vulkan *vk, const vkr_shader_config *cfg,
      const char *cache_path)
{
   vkr_renderer *r = (vkr_renderer*)calloc(1, sizeof(*r));
   PFN_vkGetPhysicalDeviceProperties get_props;
   PFN_vkGetPhysicalDeviceMemoryProperties get_mem;
   PFN_vkGetPhysicalDeviceFormatProperties get_format;
   PFN_vkGetPhysicalDeviceFeatures get_features;
   VkPhysicalDeviceFeatures feats;
   VkFormatProperties fp;
   VkFenceCreateInfo fi;
   VkPipelineCacheCreateInfo pc;
   VkSamplerCreateInfo sci;
   void *cache_blob = NULL;
   size_t cache_size = 0;
   unsigned i;

   if (!r)
      return NULL;
   r->vk = vk;
   r->device = vk->device;
   r->cfg = *cfg;
   load_functions(r);
   get_props = (PFN_vkGetPhysicalDeviceProperties)vk->get_instance_proc_addr(vk->instance, "vkGetPhysicalDeviceProperties");
   get_mem = (PFN_vkGetPhysicalDeviceMemoryProperties)vk->get_instance_proc_addr(vk->instance, "vkGetPhysicalDeviceMemoryProperties");
   get_format = (PFN_vkGetPhysicalDeviceFormatProperties)vk->get_instance_proc_addr(vk->instance, "vkGetPhysicalDeviceFormatProperties");
   get_features = (PFN_vkGetPhysicalDeviceFeatures)vk->get_instance_proc_addr(vk->instance, "vkGetPhysicalDeviceFeatures");
   get_props(vk->gpu, &r->props);
   get_mem(vk->gpu, &r->mem);
   get_features(vk->gpu, &feats);

   r->eds = r->fn.CmdSetCullModeEXT != NULL && r->fn.CmdSetDepthCompareOpEXT != NULL;
   /* for tools/ci: every combiner through the generic shader */
   r->force_generic = getenv("GLIDEN64_VULKAN") != NULL && strcmp(getenv("GLIDEN64_VULKAN"), "generic") == 0;
   r->strict = getenv("GLIDEN64_VULKAN_STRICT") != NULL;
   /* tools/ci: GPUs without it take the other path, and it stays tested */
   r->eds3 = r->eds && r->fn.CmdSetColorBlendEnableEXT != NULL && gliden64_vulkan_has_dynamic_blend()
         && getenv("GLIDEN64_VULKAN_NO_EDS3") == NULL;
   get_format(vk->gpu, VK_FORMAT_D32_SFLOAT, &fp);
   r->depth_format = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
      ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_X8_D24_UNORM_PACK32;

   r->caps.max_texture_size = (int)r->props.limits.maxImageDimension2D;
   r->caps.max_anisotropy = feats.samplerAnisotropy ? r->props.limits.maxSamplerAnisotropy : 1.f;
   {
      /* the highest level below 16 every attachment kind supports */
      VkSampleCountFlags have = r->props.limits.framebufferColorSampleCounts
         & r->props.limits.framebufferDepthSampleCounts
         & r->props.limits.sampledImageColorSampleCounts;
      unsigned count;
      r->caps.max_samples = 1;
      for (count = 16; count > 1; count >>= 1)
         if (have & count)
         {
            r->caps.max_samples = (int)count;
            break;
         }
      /* a level the device lacks: the nearest one above, or the highest */
      if (r->cfg.msaa > 0 && !(have & (unsigned)r->cfg.msaa))
      {
         unsigned want = 2;
         while (want < 16 && (!(have & want) || want < (unsigned)r->cfg.msaa))
            want <<= 1;
         r->cfg.msaa = (have & want) ? (int)want : r->caps.max_samples;
         if (r->cfg.msaa <= 1)
            r->cfg.msaa = 0;
      }
   }
   r->caps.max_line_width = feats.wideLines ? r->props.limits.lineWidthRange[1] : 1.f;
   r->caps.storage_images = feats.fragmentStoresAndAtomics != 0;

   if (cfg->n64_depth_compare && !r->caps.storage_images)
      r->cfg.n64_depth_compare = 0;

   r->images = (vkr_image*)calloc(MAX_IMAGES, sizeof(vkr_image));
   r->image_free = (uint32_t*)malloc(MAX_IMAGES * sizeof(uint32_t));
   r->fbos = (vkr_fbo*)calloc(MAX_FBOS, sizeof(vkr_fbo));
   r->fbo_free = (uint32_t*)malloc(MAX_FBOS * sizeof(uint32_t));
   r->pipes = (pipe_entry*)calloc(MAX_PIPELINES, sizeof(pipe_entry));
   r->desc_cache = (desc_entry*)calloc(DESC_CACHE, sizeof(desc_entry));
   r->jobs = (pipe_job*)calloc(MAX_JOBS, sizeof(pipe_job));
   r->programs = (vkr_program*)calloc(MAX_PROGRAMS, sizeof(vkr_program));
   if (!r->images || !r->image_free || !r->fbos || !r->fbo_free || !r->pipes || !r->desc_cache || !r->jobs || !r->programs)
      goto fail;
   for (i = MAX_IMAGES - 1; i > 0; i--)
      r->image_free[r->n_image_free++] = i;
   for (i = MAX_FBOS - 1; i > 0; i--)
      r->fbo_free[r->n_fbo_free++] = i;
   for (i = 0; i < MAX_PIPELINES; i++)
      retro_atomic_int_init(&r->pipes[i].status, PS_EMPTY);

   mpsc_stack_init(&r->job_free_returned);
   retro_atomic_int_init(&r->job_count, 0);
   for (i = 0; i < MAX_JOBS; i++)
   {
      r->jobs[i].node.next = r->job_free;
      r->job_free = &r->jobs[i].node;
   }

   if (!spv_init(&r->worker_spv))
      goto fail;

   r->n_frames = vk->get_sync_index_mask(vk->handle) + 1;
   if (r->n_frames > MAX_FRAMES)
      r->n_frames = MAX_FRAMES;
   /* GL's defaults, which GLideN64 relies on until it sets its own */
   r->enable[VKR_CAP_SCISSOR] = 0;
   r->scissor.extent.width = 32767;
   r->scissor.extent.height = 32767;
   r->viewport.width = 1.f;
   r->viewport.height = 1.f;
   r->viewport.maxDepth = 1.f;
   r->depth_write_on = 1;
   r->line_width = 1.f;
   r->serial = 0;

   memset(&fi, 0, sizeof(fi));
   fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   for (i = 0; i < MAX_SUBMITS; i++)
      if (F(CreateFence)(r->device, &fi, NULL, &r->fences[i]) != VK_SUCCESS)
         goto fail;

   memset(&pc, 0, sizeof(pc));
   pc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
   if (load_cache(r, cache_path, &cache_blob, &cache_size))
   {
      pc.initialDataSize = cache_size;
      pc.pInitialData = cache_blob;
   }
   F(CreatePipelineCache)(r->device, &pc, NULL, &r->cache);
   free(cache_blob);
   if (cache_path)
      strlcpy(r->cache_path, cache_path, sizeof(r->cache_path));

   memset(&sci, 0, sizeof(sci));
   sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   sci.maxLod = 0.f;
   if (F(CreateSampler)(r->device, &sci, NULL, &r->samplers[0].s) != VK_SUCCESS)
      goto fail;
   r->samplers[0].key = 0xffffffffu;
   r->n_samplers = 1;

   if (!create_layouts(r) || !create_shaders(r))
      goto fail;
   for (i = 0; i < r->n_frames; i++)
      if (!create_frame(r, &r->frames[i]))
         goto fail;

   r->dummy_color = make_dummy(r, VKR_FMT_RGBA8, 1);
   r->dummy_uint = make_dummy(r, VKR_FMT_R32UI, 1);
   r->dummy_depth = make_dummy(r, VKR_FMT_R32F, 1);
   r->dummy_storage = make_dummy(r, VKR_FMT_R32F, 1);
   r->dummy_ms = r->cfg.msaa ? make_dummy(r, VKR_FMT_RGBA8, (unsigned)r->cfg.msaa) : 0;
   /* storage images live in GENERAL */
   if (r->cfg.n64_depth_compare)
   {
      vkr_image *d = &r->images[r->dummy_storage];
      image_barrier(r, r->xfer, d->image, VK_IMAGE_ASPECT_COLOR_BIT, d->layout, VK_IMAGE_LAYOUT_GENERAL);
      d->layout = VK_IMAGE_LAYOUT_GENERAL;
   }

   r->worker = rdp_worker_new(NULL, pipeline_work, r);
   if (!r->worker)
      goto fail;
   prewarm(r);
   return r;

fail:
   vkr_destroy(r);
   return NULL;
}

void vkr_destroy(vkr_renderer *r)
{
   unsigned i;

   if (!r)
      return;
   /* stop admitting, join, drain, then destroy */
   if (r->worker)
      rdp_worker_free(r->worker);
   if (r->device)
   {
      F(DeviceWaitIdle)(r->device);
      save_cache(r);
   }

   for (i = 0; i < r->n_frames; i++)
   {
      vkr_frame *f = &r->frames[i];
      unsigned g;
      for (g = 0; g < f->n_garbage; g++)
         retire(r, f, g);
      f->n_garbage = 0;
      if (f->win_fb != VK_NULL_HANDLE)
         F(DestroyFramebuffer)(r->device, f->win_fb, NULL);
      if (f->win_view != VK_NULL_HANDLE)
         F(DestroyImageView)(r->device, f->win_view, NULL);
      if (f->win_image != VK_NULL_HANDLE)
         F(DestroyImage)(r->device, f->win_image, NULL);
      if (f->win_mem != VK_NULL_HANDLE)
         F(FreeMemory)(r->device, f->win_mem, NULL);
      if (f->dpool != VK_NULL_HANDLE)
         F(DestroyDescriptorPool)(r->device, f->dpool, NULL);
      if (f->pool != VK_NULL_HANDLE)
         F(DestroyCommandPool)(r->device, f->pool, NULL);
      if (f->ring != VK_NULL_HANDLE)
         F(DestroyBuffer)(r->device, f->ring, NULL);
      if (f->ring_mem != VK_NULL_HANDLE)
         F(FreeMemory)(r->device, f->ring_mem, NULL);
   }

   if (r->images)
      for (i = 1; i < MAX_IMAGES; i++)
         if (r->images[i].live)
         {
            if (r->images[i].view != VK_NULL_HANDLE)
               F(DestroyImageView)(r->device, r->images[i].view, NULL);
            if (r->images[i].image != VK_NULL_HANDLE)
               F(DestroyImage)(r->device, r->images[i].image, NULL);
         }
   if (r->fbos)
      for (i = 1; i < MAX_FBOS; i++)
         if (r->fbos[i].live && r->fbos[i].fb != VK_NULL_HANDLE)
            F(DestroyFramebuffer)(r->device, r->fbos[i].fb, NULL);
   if (r->pipes)
      for (i = 0; i < MAX_PIPELINES; i++)
         if (r->pipes[i].pipeline != VK_NULL_HANDLE)
            F(DestroyPipeline)(r->device, r->pipes[i].pipeline, NULL);
   for (i = 0; i < r->n_rps; i++)
      F(DestroyRenderPass)(r->device, r->rps[i].rp, NULL);
   for (i = 0; i < r->n_samplers; i++)
      F(DestroySampler)(r->device, r->samplers[i].s, NULL);
   for (i = 0; i < VKR_VS_COUNT; i++)
      if (r->vs[i] != VK_NULL_HANDLE)
         F(DestroyShaderModule)(r->device, r->vs[i], NULL);
   for (i = 0; i < VKR_FS_COUNT; i++)
      if (r->special_fs[i] != VK_NULL_HANDLE)
         F(DestroyShaderModule)(r->device, r->special_fs[i], NULL);
   if (r->generic_fs != VK_NULL_HANDLE)
      F(DestroyShaderModule)(r->device, r->generic_fs, NULL);
   if (r->layout != VK_NULL_HANDLE)
      F(DestroyPipelineLayout)(r->device, r->layout, NULL);
   if (r->set_layout != VK_NULL_HANDLE)
      F(DestroyDescriptorSetLayout)(r->device, r->set_layout, NULL);
   if (r->cache != VK_NULL_HANDLE)
      F(DestroyPipelineCache)(r->device, r->cache, NULL);
   for (i = 0; i < MAX_SUBMITS; i++)
      if (r->fences[i] != VK_NULL_HANDLE)
         F(DestroyFence)(r->device, r->fences[i], NULL);
   for (i = 0; i < r->n_blocks; i++)
   {
      F(FreeMemory)(r->device, r->blocks[i].mem, NULL);
      free(r->blocks[i].free_spans);
   }
   spv_free(&r->worker_spv);
   free(r->images);
   free(r->image_free);
   free(r->fbos);
   free(r->fbo_free);
   free(r->pipes);
   free(r->desc_cache);
   free(r->jobs);
   free(r->programs);
   free(r);
}
