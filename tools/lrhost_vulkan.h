/* Vulkan HW render support for lrhost, built when LRHOST_VULKAN is
 * defined (lrhost_vk). Headless: it negotiates a device with the core
 * (RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN v1), serves
 * retro_hw_render_interface_vulkan, and on every presented frame copies
 * the image the core handed to set_image() back to the host, so a frame
 * can be signed and dumped exactly as for a software core. Any Vulkan
 * driver works; CI uses Mesa's lavapipe (VK_ICD_FILENAMES=.../lvp_icd.json).
 *
 * The queue is shared with the core, which calls lock_queue/unlock_queue
 * from its own threads around every submission: the libretro Vulkan
 * interface defines that as mutual exclusion, so this test host uses a
 * plain mutex for it. */
#include <pthread.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <libretro_vulkan.h>

static struct retro_hw_render_callback vk_hw;
static const struct retro_hw_render_context_negotiation_interface_vulkan *vk_neg;
static struct retro_hw_render_interface_vulkan vk_iface;
static struct retro_vulkan_context vk_ctx;
static VkInstance vk_inst;
static void *vk_lib;
static PFN_vkGetInstanceProcAddr vk_gipa;
static pthread_mutex_t vk_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static int vk_ready;
static VkPhysicalDeviceFeatures vk_features;

/* the image of the last set_image() */
static struct retro_vulkan_image vk_img;
static VkSemaphore vk_img_sems[16];
static unsigned vk_img_nsems;
/* command buffers the core asked the frontend to submit */
static VkCommandBuffer vk_core_cmds[16];
static unsigned vk_core_ncmds;
static VkSemaphore vk_signal_sem;

/* readback */
static VkCommandPool vk_pool;
static VkCommandBuffer vk_cmd;
static VkFence vk_fence;
static VkBuffer vk_rb;
static VkDeviceMemory vk_rb_mem;
static VkDeviceSize vk_rb_size;
static void *vk_rb_map;
static uint32_t *vk_frame; static size_t vk_frame_cap;

#define VKF(name) static PFN_##name p_##name
VKF(vkCreateInstance); VKF(vkEnumeratePhysicalDevices); VKF(vkGetDeviceProcAddr);
VKF(vkGetPhysicalDeviceMemoryProperties); VKF(vkCreateCommandPool); VKF(vkAllocateCommandBuffers);
VKF(vkBeginCommandBuffer); VKF(vkEndCommandBuffer); VKF(vkResetCommandBuffer); VKF(vkCmdPipelineBarrier);
VKF(vkCmdCopyImageToBuffer); VKF(vkQueueSubmit); VKF(vkCreateFence); VKF(vkWaitForFences);
VKF(vkResetFences); VKF(vkCreateBuffer); VKF(vkGetBufferMemoryRequirements); VKF(vkAllocateMemory);
VKF(vkBindBufferMemory); VKF(vkMapMemory); VKF(vkQueueWaitIdle); VKF(vkDeviceWaitIdle);
VKF(vkDestroyBuffer); VKF(vkFreeMemory); VKF(vkDestroyFence); VKF(vkDestroyCommandPool);
VKF(vkDestroyDevice); VKF(vkDestroyInstance);

static void vk_set_image(void *h, const struct retro_vulkan_image *image, uint32_t n, const VkSemaphore *sems, uint32_t qf)
{
    (void)h; (void)qf;
    vk_img = *image;
    vk_img_nsems = n > 16 ? 16 : n;
    if (vk_img_nsems) memcpy(vk_img_sems, sems, vk_img_nsems * sizeof *sems);
}
static uint32_t vk_get_sync_index(void *h) { (void)h; return 0; }
static uint32_t vk_get_sync_index_mask(void *h) { (void)h; return 1; }
static void vk_set_command_buffers(void *h, uint32_t n, const VkCommandBuffer *cmds)
{
    (void)h; vk_core_ncmds = n > 16 ? 16 : n;
    if (vk_core_ncmds) memcpy(vk_core_cmds, cmds, vk_core_ncmds * sizeof *cmds);
}
static void vk_wait_sync_index(void *h)
{
    (void)h;
    pthread_mutex_lock(&vk_queue_lock);
    p_vkQueueWaitIdle(vk_ctx.queue);
    pthread_mutex_unlock(&vk_queue_lock);
}
static void vk_lock_queue(void *h) { (void)h; pthread_mutex_lock(&vk_queue_lock); }
static void vk_unlock_queue(void *h) { (void)h; pthread_mutex_unlock(&vk_queue_lock); }
static void vk_set_signal_semaphore(void *h, VkSemaphore s) { (void)h; vk_signal_sem = s; }

static PFN_vkVoidFunction vk_dev_proc(const char *n) { return p_vkGetDeviceProcAddr(vk_ctx.device, n); }

/* Creates the instance, lets the core create the device, and builds the
 * readback objects. Called after retro_load_game, before context_reset. */
static int vk_init(void)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkPhysicalDevice gpus[8]; uint32_t ngpu = 8;
    VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

    vk_lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!vk_lib) { fprintf(stderr, "lrhost_vk: no libvulkan.so.1\n"); return 0; }
    *(void**)&vk_gipa = dlsym(vk_lib, "vkGetInstanceProcAddr");
    *(void**)&p_vkCreateInstance = (void*)vk_gipa(NULL, "vkCreateInstance");

    app.pApplicationName = "lrhost"; app.apiVersion = VK_API_VERSION_1_1;
    if (vk_neg && vk_neg->get_application_info && vk_neg->get_application_info())
        app = *vk_neg->get_application_info();
    ici.pApplicationInfo = &app;
    if (p_vkCreateInstance(&ici, NULL, &vk_inst) != VK_SUCCESS) { fprintf(stderr, "lrhost_vk: vkCreateInstance failed\n"); return 0; }

#define IFN(name) *(void**)&p_##name = (void*)vk_gipa(vk_inst, #name)
    IFN(vkEnumeratePhysicalDevices); IFN(vkGetDeviceProcAddr); IFN(vkGetPhysicalDeviceMemoryProperties);
    IFN(vkDestroyInstance);
    if (p_vkEnumeratePhysicalDevices(vk_inst, &ngpu, gpus) < 0 || !ngpu) { fprintf(stderr, "lrhost_vk: no GPU\n"); return 0; }

    /* no extensions or features of our own; cores may dereference the
     * features pointer, as RetroArch always passes one */
    if (!vk_neg || !vk_neg->create_device ||
        !vk_neg->create_device(&vk_ctx, vk_inst, gpus[0], VK_NULL_HANDLE, vk_gipa, NULL, 0, NULL, 0, &vk_features))
    { fprintf(stderr, "lrhost_vk: the core did not create a device\n"); return 0; }

#define DFN(name) *(void**)&p_##name = (void*)vk_dev_proc(#name)
    DFN(vkCreateCommandPool); DFN(vkAllocateCommandBuffers); DFN(vkBeginCommandBuffer); DFN(vkEndCommandBuffer);
    DFN(vkResetCommandBuffer); DFN(vkCmdPipelineBarrier); DFN(vkCmdCopyImageToBuffer); DFN(vkQueueSubmit);
    DFN(vkCreateFence); DFN(vkWaitForFences); DFN(vkResetFences); DFN(vkCreateBuffer);
    DFN(vkGetBufferMemoryRequirements); DFN(vkAllocateMemory); DFN(vkBindBufferMemory); DFN(vkMapMemory);
    DFN(vkQueueWaitIdle); DFN(vkDeviceWaitIdle); DFN(vkDestroyBuffer); DFN(vkFreeMemory); DFN(vkDestroyFence);
    DFN(vkDestroyCommandPool); DFN(vkDestroyDevice);

    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = vk_ctx.queue_family_index;
    p_vkCreateCommandPool(vk_ctx.device, &cpi, NULL, &vk_pool);
    cai.commandPool = vk_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    p_vkAllocateCommandBuffers(vk_ctx.device, &cai, &vk_cmd);
    p_vkCreateFence(vk_ctx.device, &fci, NULL, &vk_fence);

    vk_iface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    vk_iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    vk_iface.handle = &vk_iface;
    vk_iface.instance = vk_inst;
    vk_iface.gpu = vk_ctx.gpu;
    vk_iface.device = vk_ctx.device;
    vk_iface.get_device_proc_addr = p_vkGetDeviceProcAddr;
    vk_iface.get_instance_proc_addr = vk_gipa;
    vk_iface.queue = vk_ctx.queue;
    vk_iface.queue_index = vk_ctx.queue_family_index;
    vk_iface.set_image = vk_set_image;
    vk_iface.get_sync_index = vk_get_sync_index;
    vk_iface.get_sync_index_mask = vk_get_sync_index_mask;
    vk_iface.set_command_buffers = vk_set_command_buffers;
    vk_iface.wait_sync_index = vk_wait_sync_index;
    vk_iface.lock_queue = vk_lock_queue;
    vk_iface.unlock_queue = vk_unlock_queue;
    vk_iface.set_signal_semaphore = vk_set_signal_semaphore;
    vk_ready = 1;
    return 1;
}

static int vk_ensure_readback(VkDeviceSize size)
{
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements req; VkPhysicalDeviceMemoryProperties mp;
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    uint32_t i;
    if (size <= vk_rb_size) return 1;
    if (vk_rb) { p_vkDestroyBuffer(vk_ctx.device, vk_rb, NULL); p_vkFreeMemory(vk_ctx.device, vk_rb_mem, NULL); vk_rb = VK_NULL_HANDLE; }
    bci.size = size; bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (p_vkCreateBuffer(vk_ctx.device, &bci, NULL, &vk_rb) != VK_SUCCESS) return 0;
    p_vkGetBufferMemoryRequirements(vk_ctx.device, vk_rb, &req);
    p_vkGetPhysicalDeviceMemoryProperties(vk_ctx.gpu, &mp);
    for (i = 0; i < mp.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            break;
    if (i == mp.memoryTypeCount) return 0;
    mai.allocationSize = req.size; mai.memoryTypeIndex = i;
    if (p_vkAllocateMemory(vk_ctx.device, &mai, NULL, &vk_rb_mem) != VK_SUCCESS) return 0;
    p_vkBindBufferMemory(vk_ctx.device, vk_rb, vk_rb_mem, 0);
    p_vkMapMemory(vk_ctx.device, vk_rb_mem, 0, VK_WHOLE_SIZE, 0, &vk_rb_map);
    vk_rb_size = size;
    return 1;
}

/* Submits what the core asked for, waits on its semaphores, copies the
 * presented image to the host and returns it as tightly packed 32-bit
 * pixels (the image's own byte order). */
static const uint32_t *vk_read_frame(unsigned w, unsigned h)
{
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    VkBufferImageCopy region; VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPipelineStageFlags waits[16]; VkCommandBuffer cmds[17]; unsigned i, n = 0;
    VkImage image = vk_img.create_info.image;
    VkDeviceSize size = (VkDeviceSize)w * h * 4;

    if (!image || !vk_ensure_readback(size)) return NULL;

    p_vkResetCommandBuffer(vk_cmd, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    p_vkBeginCommandBuffer(vk_cmd, &bi);
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = vk_img.image_layout; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image; b.subresourceRange = vk_img.create_info.subresourceRange;
    b.subresourceRange.levelCount = 1; b.subresourceRange.layerCount = 1;
    p_vkCmdPipelineBarrier(vk_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    memset(&region, 0, sizeof region);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = vk_img.create_info.subresourceRange.baseMipLevel;
    region.imageSubresource.baseArrayLayer = vk_img.create_info.subresourceRange.baseArrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = w; region.imageExtent.height = h; region.imageExtent.depth = 1;
    p_vkCmdCopyImageToBuffer(vk_cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_rb, 1, &region);
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b.newLayout = vk_img.image_layout;
    p_vkCmdPipelineBarrier(vk_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    p_vkEndCommandBuffer(vk_cmd);

    for (i = 0; i < vk_core_ncmds; i++) cmds[n++] = vk_core_cmds[i];
    cmds[n++] = vk_cmd;
    for (i = 0; i < vk_img_nsems; i++) waits[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    si.waitSemaphoreCount = vk_img_nsems; si.pWaitSemaphores = vk_img_sems; si.pWaitDstStageMask = waits;
    si.commandBufferCount = n; si.pCommandBuffers = cmds;
    if (vk_signal_sem) { si.signalSemaphoreCount = 1; si.pSignalSemaphores = &vk_signal_sem; }

    pthread_mutex_lock(&vk_queue_lock);
    p_vkResetFences(vk_ctx.device, 1, &vk_fence);
    p_vkQueueSubmit(vk_ctx.queue, 1, &si, vk_fence);
    pthread_mutex_unlock(&vk_queue_lock);
    p_vkWaitForFences(vk_ctx.device, 1, &vk_fence, VK_TRUE, UINT64_MAX);
    vk_core_ncmds = 0; vk_img_nsems = 0; vk_signal_sem = VK_NULL_HANDLE;

    if ((size_t)w * h > vk_frame_cap) { free(vk_frame); vk_frame = (uint32_t*)malloc((size_t)w * h * 4); vk_frame_cap = vk_frame ? (size_t)w * h : 0; }
    if (!vk_frame) return NULL;
    memcpy(vk_frame, vk_rb_map, (size_t)size);
    return vk_frame;
}

static void vk_shutdown(void)
{
    if (!vk_ready) return;
    p_vkDeviceWaitIdle(vk_ctx.device);
    if (vk_hw.context_destroy) vk_hw.context_destroy();
    if (vk_rb) { p_vkDestroyBuffer(vk_ctx.device, vk_rb, NULL); p_vkFreeMemory(vk_ctx.device, vk_rb_mem, NULL); }
    p_vkDestroyFence(vk_ctx.device, vk_fence, NULL);
    p_vkDestroyCommandPool(vk_ctx.device, vk_pool, NULL);
    vk_ready = 0;
}
