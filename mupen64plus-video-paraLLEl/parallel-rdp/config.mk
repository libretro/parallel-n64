# For use in standalone implementations.

PARALLEL_RDP_CFLAGS :=
PARALLEL_RDP_CXXFLAGS :=
# retro_atomic.h picks std::atomic in C++: the builtins instead
ifneq (,$(findstring msvc,$(platform)))
PARALLEL_RDP_CXXFLAGS += -DRETRO_ATOMIC_FORCE_MSVC
else
PARALLEL_RDP_CXXFLAGS += -DRETRO_ATOMIC_FORCE_GCC_NEW
endif

PARALLEL_RDP_SOURCES_CXX := \
        $(wildcard $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/*.cpp)

PARALLEL_RDP_SOURCES_C := \
        $(PARALLEL_RDP_IMPLEMENTATION)/volk/volk.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/command_ring.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/rdp_worker.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/rdp_dump_write.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/upscaler_pass.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/vi_registers.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp/temporal_upscaler.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/cookie.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/fence_wait.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/fence.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/semaphore.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/queue_submit.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/frame_sync.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/context.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/device_caps.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/program_cache.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/render_target.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/command_buffer.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/frame_context.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/submission.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/device.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/framebuffer.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/frame_garbage.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/pipeline_compile.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/pipeline_cache.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/pipeline_map.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/pipeline_record_list.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/release_queue.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/memory_release.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/render_pass.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/resource_bindings.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/buffer_create.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/buffer.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/buffer_pool.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/image_create.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/image_upload.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/image.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/sampler.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/command_ops.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/command_pool.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/command_state.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/descriptor_set.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/memory_allocator.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/shader.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/submit_batch.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/sync_pools.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/texture_format.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/aligned_alloc.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/legion_allocator.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/logging.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/pool.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/thread_id.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/thread_name.c \
        $(PARALLEL_RDP_IMPLEMENTATION)/util/timer.c

PARALLEL_RDP_INCLUDE_DIRS := \
        -I$(PARALLEL_RDP_IMPLEMENTATION)/parallel-rdp \
        -I$(PARALLEL_RDP_IMPLEMENTATION)/volk \
        -I$(PARALLEL_RDP_IMPLEMENTATION)/vulkan \
        -I$(PARALLEL_RDP_IMPLEMENTATION)/vulkan-headers/include \
        -I$(PARALLEL_RDP_IMPLEMENTATION)/util

PARALLEL_RDP_LDFLAGS := -pthread
ifeq (,$(findstring win,$(platform)))
    PARALLEL_RDP_LDFLAGS += -ldl
else
    PARALLEL_RDP_CFLAGS += -DVK_USE_PLATFORM_WIN32_KHR
    PARALLEL_RDP_LDFLAGS += -lwinmm
endif

