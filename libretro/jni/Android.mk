LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := retro_mupen64plus
M64P_ROOT_DIR := ../..
LIBRETRODIR = ../
VIDEODIR_GLIDE = $(M64P_ROOT_DIR)/glide2gl/src
RSPDIR = $(M64P_ROOT_DIR)/mupen64plus-rsp-hle
COREDIR = $(M64P_ROOT_DIR)/mupen64plus-core
VIDEODIR_GLN64 = $(M64P_ROOT_DIR)/gles2n64/src
CXD4DIR = ../../mupen64plus-rsp-cxd4

ifeq ($(TARGET_ARCH),arm)
COMMON_FLAGS += -DANDROID_ARM -DDYNAREC -DNEW_DYNAREC=3 -DARM_ASM
LOCAL_ARM_MODE := arm
LOCAL_CFLAGS += -marm
LOCAL_SRC_FILES += $(COREDIR)/src/r4300/new_dynarec/new_dynarec.c $(COREDIR)/src/r4300/empty_dynarec.c
LOCAL_SRC_FILES += $(COREDIR)/src/r4300/new_dynarec/linkage_arm.S
LOCAL_SRC_FILES += $(LIBRETRODIR)/libco/armeabi_asm.S
LOCAL_SRC_FILES += $(LIBRETRODIR)/sinc.c $(LIBRETRODIR)/utils.c
endif

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
COMMON_FLAGS += -DHAVE_NEON -D__NEON_OPT
LOCAL_ARM_NEON := true
LOCAL_SRC_FILES += $(LIBRETRODIR)/sinc_neon.S $(LIBRETRODIR)/utils_neon.S
LOCAL_SRC_FILES += $(VIDEODIR_GLN64)/3DMathNeon.c
LOCAL_SRC_FILES += $(VIDEODIR_GLN64)/gSPNeon.c
endif

ifeq ($(TARGET_ARCH_ABI),x86)
COMMON_FLAGS += -DANDROID_X86 -DDYNAREC
LOCAL_SRC_FILES += $(COREDIR)/src/r4300/x86/assemble.c \
          $(COREDIR)/src/r4300/x86/gbc.c \
          $(COREDIR)/src/r4300/x86/gcop0.c \
          $(COREDIR)/src/r4300/x86/gcop1.c \
          $(COREDIR)/src/r4300/x86/gcop1_d.c \
          $(COREDIR)/src/r4300/x86/gcop1_l.c \
          $(COREDIR)/src/r4300/x86/gcop1_s.c \
          $(COREDIR)/src/r4300/x86/gcop1_w.c \
          $(COREDIR)/src/r4300/x86/gr4300.c \
          $(COREDIR)/src/r4300/x86/gregimm.c \
          $(COREDIR)/src/r4300/x86/gspecial.c \
          $(COREDIR)/src/r4300/x86/gtlb.c \
          $(COREDIR)/src/r4300/x86/regcache.c \
          $(COREDIR)/src/r4300/x86/rjump.c
LOCAL_SRC_FILES   += $(VIDEODIR_GLIDE)/Glide64/3dmathsse.c
endif

ifeq ($(TARGET_ARCH),mips)
COMMON_FLAGS += -DANDROID_MIPS
endif

# libretro
LOCAL_SRC_FILES += $(LIBRETRODIR)/libretro.c $(LIBRETRODIR)/glsym.c $(LIBRETRODIR)/libco/libco.c $(LIBRETRODIR)/opengl_state_machine.c \
          $(LIBRETRODIR)/audio_plugin.c $(LIBRETRODIR)/input_plugin.c $(LIBRETRODIR)/resampler.c

# RSP Plugin
LOCAL_SRC_FILES += \
    $(RSPDIR)/src/alist.c \
    $(RSPDIR)/src/cicx105.c \
    $(RSPDIR)/src/jpeg.c \
    $(RSPDIR)/src/main.c

LOCAL_SRC_FILES += \
    $(RSPDIR)/src/ucode1.c \
    $(RSPDIR)/src/ucode2.c \
    $(RSPDIR)/src/ucode3.c \
    $(RSPDIR)/src/ucode3mp3.c

# Video Plugins
LOCAL_SRC_FILES += $(VIDEODIR_GLN64)/2xSAI.c \
            $(VIDEODIR_GLN64)/3DMath.c \
            $(VIDEODIR_GLN64)/Config.c \
            $(VIDEODIR_GLN64)/CRC.c \
            $(VIDEODIR_GLN64)/DepthBuffer.c \
            $(VIDEODIR_GLN64)/F3DCBFD.c \
            $(VIDEODIR_GLN64)/F3D.c \
            $(VIDEODIR_GLN64)/F3DDKR.c \
            $(VIDEODIR_GLN64)/F3DEX2.c \
            $(VIDEODIR_GLN64)/F3DEX.c \
            $(VIDEODIR_GLN64)/F3DPD.c \
            $(VIDEODIR_GLN64)/F3DWRUS.c \
            $(VIDEODIR_GLN64)/GBI.c \
            $(VIDEODIR_GLN64)/gDP.c \
            $(VIDEODIR_GLN64)/gles2N64.c \
            $(VIDEODIR_GLN64)/../cpufeatures.c \
            $(VIDEODIR_GLN64)/gSP.c \
            $(VIDEODIR_GLN64)/L3D.c \
            $(VIDEODIR_GLN64)/L3DEX2.c \
            $(VIDEODIR_GLN64)/L3DEX.c \
            $(VIDEODIR_GLN64)/N64.c \
            $(VIDEODIR_GLN64)/OpenGL.c \
            $(VIDEODIR_GLN64)/RDP.c \
            $(VIDEODIR_GLN64)/RSP.c \
            $(VIDEODIR_GLN64)/S2DEX2.c \
            $(VIDEODIR_GLN64)/S2DEX.c \
            $(VIDEODIR_GLN64)/ShaderCombiner.c \
            $(VIDEODIR_GLN64)/Textures.c \
            $(VIDEODIR_GLN64)/VI.c

INCFLAGS += $(VIDEODIR_GLIDE)/Glitch64/inc
LOCAL_SRC_FILES += $(VIDEODIR_GLIDE)/Glide64/3dmath.c \
            $(VIDEODIR_GLIDE)/Glide64/FBtoScreen.c \
            $(VIDEODIR_GLIDE)/Glide64/glidemain.c \
            $(VIDEODIR_GLIDE)/Glide64/Util.c \
            $(VIDEODIR_GLIDE)/Glide64/TexBuffer.c \
            $(VIDEODIR_GLIDE)/Glide64/rdp.c \
            $(VIDEODIR_GLIDE)/Glide64/Combine.c \
            $(VIDEODIR_GLIDE)/Glide64/DepthBufferRender.c \
            $(VIDEODIR_GLIDE)/Glide64/TexCache.c
LOCAL_SRC_FILES   += $(VIDEODIR_GLIDE)/Glitch64/combiner.c \
            $(VIDEODIR_GLIDE)/Glitch64/geometry.c \
            $(VIDEODIR_GLIDE)/Glitch64/glitchmain.c \
            $(VIDEODIR_GLIDE)/Glitch64/textures.c \
            $(VIDEODIR_GLIDE)/Glide64/glide64_crc.c

PLATFORM_EXT := unix
LOCAL_SRC_FILES += \
    $(COREDIR)/src/api/callbacks.c \
    $(COREDIR)/src/api/common.c \
    $(COREDIR)/src/api/config.c \
    $(COREDIR)/src/api/frontend.c \
    $(COREDIR)/src/main/eventloop.c \
    $(COREDIR)/src/main/cheat.c \
    $(COREDIR)/src/main/main.c \
    $(COREDIR)/src/main/md5.c \
    $(COREDIR)/src/main/rom.c \
    $(COREDIR)/src/main/savestates.c \
    $(COREDIR)/src/main/util.c \
    $(COREDIR)/src/memory/dma.c \
    $(COREDIR)/src/memory/flashram.c \
    $(COREDIR)/src/memory/memory.c \
    $(COREDIR)/src/memory/n64_cic_nus_6105.c \
    $(COREDIR)/src/memory/pif.c \
    $(COREDIR)/src/memory/tlb.c \
    $(COREDIR)/src/plugin/plugin.c \
    $(COREDIR)/src/r4300/profile.c \
    $(COREDIR)/src/r4300/recomp.c \
    $(COREDIR)/src/r4300/exception.c \
    $(COREDIR)/src/r4300/pure_interp.c \
    $(COREDIR)/src/r4300/reset.c \
    $(COREDIR)/src/r4300/interupt.c \
    $(COREDIR)/src/r4300/r4300.c

LOCAL_SRC_FILES += $(CXD4DIR)/rsp.c

COMMON_FLAGS += -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DM64P_PLUGIN_API -D__LIBRETRO__ -DNO_ASM -DNOSSE -DSDL_VIDEO_OPENGL_ES2=1 -DGLES -DANDROID -DSINC_LOWER_QUALITY -DGLES
COMMON_OPTFLAGS = -O3 -Wall -ffast-math -fexceptions

LOCAL_CFLAGS += $(COMMON_OPTFLAGS) $(COMMON_FLAGS)
LOCAL_CXXFLAGS += $(COMMON_OPTFLAGS) $(COMMON_FLAGS)
LOCAL_LDLIBS += -lz -llog -lGLESv2
LOCAL_C_INCLUDES = $(INCFLAGS) $(COREDIR)/src $(COREDIR)/src/api ../libco ../

include $(BUILD_SHARED_LIBRARY)

