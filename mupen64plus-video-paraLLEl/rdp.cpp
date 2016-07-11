#include "Gfx #1.3.h"
#include "z64.h"
#include "rdp.hpp"
#include "parallel.h"
#include <assert.h>

using namespace Vulkan;
using namespace std;

extern "C" {
#include "rdp_dump.h"
}

static bool rdp_dump_active;

namespace RDP
{

const struct retro_hw_render_interface_vulkan *vulkan;

static int cmd_cur;
static int cmd_ptr;
static uint32_t cmd_data[0x00040000 >> 2];

unique_ptr<Frontend> frontend;
unique_ptr<Renderer> renderer;
unique_ptr<Device> device;
unique_ptr<VulkanContext> context;

static const unsigned cmd_len_lut[64] = {
   1, 1, 1, 1,
   1, 1, 1, 1,
   4, 6, 12, 14,
   12, 14, 20, 22,

   1, 1, 1, 1,
   1, 1, 1, 1,
   1, 1, 1, 1,
   1, 1, 1, 1,

   1, 1, 1, 1,
   2, 2, 1, 1,
   1, 1, 1, 1,
   1, 1, 1, 1,

   1, 1, 1, 1,
   1, 1, 1, 1,
   1, 1, 1, 1,
   1, 1, 1, 1,
};

void process_commands()
{
   const uint32_t DP_CURRENT = *GET_GFX_INFO(DPC_CURRENT_REG) & 0x00FFFFF8;
   const uint32_t DP_END     = *GET_GFX_INFO(DPC_END_REG)     & 0x00FFFFF8;
   *GET_GFX_INFO(DPC_STATUS_REG) &= ~DP_STATUS_FREEZE;

   int length = DP_END - DP_CURRENT;
   if (length <= 0)
      return;

   length = unsigned(length) >> 3;
   if ((cmd_ptr + length) & ~(0x0003FFFF >> 3))
      return;

   // Flush out changes in DRAM if anything has changed.
   if (!rdp_dump_active)
   {
      rdp_dump_flush_dram(DRAM, 8 * 1024 * 1024);
      rdp_dump_begin_command_list();
      rdp_dump_active = true;
   }

   uint32_t offset = DP_CURRENT;
   if (*GET_GFX_INFO(DPC_STATUS_REG) & DP_STATUS_XBUS_DMA)
   {
      do
      {
         offset &= 0xFF8;
         cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset);
         cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset + 4);
         offset += sizeof(uint64_t);
         cmd_ptr++;
      } while (--length > 0);
   }
   else
   {
      if (DP_END > 0x7ffffff || DP_CURRENT > 0x7ffffff)
      {
         return;
      }
      else
      {
         do
         {
            offset &= 0xFFFFF8;
            cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(DRAM + offset);
            cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(DRAM + offset + 4);
            offset += sizeof(uint64_t);
            cmd_ptr++;
         } while (--length > 0);
      }
   }

   while (cmd_cur - cmd_ptr < 0)
   {
      uint32_t w1      = cmd_data[2 * cmd_cur];
      uint32_t command = (w1 >> 24) & 63;
      int cmd_length   = cmd_len_lut[command];

      if (cmd_ptr - cmd_cur - cmd_length < 0)
      {
         *GET_GFX_INFO(DPC_START_REG)
            = *GET_GFX_INFO(DPC_CURRENT_REG)
            = *GET_GFX_INFO(DPC_END_REG);
         return;
      }

      rdp_dump_emit_command(command, cmd_data + 2 * cmd_cur, cmd_length * 2);
      frontend->command(command, &cmd_data[2 * cmd_cur]);

      // sync_full
      if (command == 0x29)
      {
         fprintf(stderr, "Signalling RDP IRQ\n");
         *gfx_info.MI_INTR_REG |= DP_INTERRUPT;
         gfx_info.CheckInterrupts();

         if (rdp_dump_active)
            rdp_dump_end_command_list();
         rdp_dump_active = false;
      }

      cmd_cur += cmd_length;
   }

   cmd_ptr = 0;
   cmd_cur = 0;
   *GET_GFX_INFO(DPC_START_REG)
      = *GET_GFX_INFO(DPC_CURRENT_REG)
      = *GET_GFX_INFO(DPC_END_REG);
}

void begin_frame()
{
   unsigned index = vulkan->get_sync_index(vulkan->handle);
   vulkan->wait_sync_index(vulkan->handle);

   // If number of sync indices changes, stall, and reinit per-frame
   // data structures.
   unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
   unsigned frames = 0;
   for (unsigned i = 0; i < 32; i++)
      if ((1u << i) & mask)
         frames = i + 1;

   if (frames != device->get_num_frames())
   {
      device->set_num_frames(frames);
      device->begin_index(index);
      renderer->begin_index(index);
      ::VI::set_num_frames(frames);
   }

   device->begin_index(index);
   renderer->begin_index(index);
}

bool init()
{
   assert(vulkan);

   if (!context)
   {
      Vulkan::VulkanContext::init_loader(vulkan->get_instance_proc_addr);
      context = unique_ptr<VulkanContext>(new VulkanContext(
               vulkan->instance, vulkan->gpu, vulkan->device,
               vulkan->queue, vulkan->queue_index));
   }

   unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
   unsigned frames = 0;
   for (unsigned i = 0; i < 32; i++)
      if ((1u << i) & mask)
         frames = i + 1;

   device = unique_ptr<Device>(new Device(*context, frames));
   renderer = unique_ptr<Renderer>(new Renderer(*device));
   frontend = unique_ptr<Frontend>(new Frontend);
   frontend->set_renderer(renderer.get(), device.get());
   renderer->set_rdram(DRAM, 8 * 1024 * 1024);
   ::VI::set_num_frames(frames);
}

void deinit()
{
   ::VI::deinit();
   frontend.reset();
   renderer.reset();
   device.reset();
   context.reset();
}

}

bool parallel_create_device(struct retro_vulkan_context *frontend_context,
      VkInstance instance,
      VkPhysicalDevice gpu,
      VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      const char **required_device_extensions,
      unsigned num_required_device_extensions,
      const char **required_device_layers,
      unsigned num_required_device_layers,
      const VkPhysicalDeviceFeatures *required_features)
{
   Vulkan::VulkanContext::init_loader(get_instance_proc_addr);

   try
   {
      ::RDP::context = unique_ptr<VulkanContext>(new VulkanContext(
               instance, gpu, surface,
               required_device_extensions,
               num_required_device_extensions,
               required_device_layers,
               num_required_device_layers,
               required_features));

      frontend_context->gpu = ::RDP::context->get_gpu();
      frontend_context->device = ::RDP::context->get_device();
      frontend_context->queue = ::RDP::context->get_alt_queue();
      frontend_context->queue_family_index = ::RDP::context->get_alt_queue_family();
      frontend_context->presentation_queue = ::RDP::context->get_alt_queue();
      frontend_context->presentation_queue_family_index = ::RDP::context->get_alt_queue_family();

      // Frontend owns the device.
      ::RDP::context->release_device();
      return true;
   }
   catch (const std::exception &e)
   {
      return false;
   }
}

const VkApplicationInfo *parallel_get_application_info(void)
{
   return &Vulkan::VulkanContext::get_application_info();
}

