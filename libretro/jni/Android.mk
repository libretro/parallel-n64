LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := retro

ifeq ($(TARGET_ARCH),arm)
LOCAL_CFLAGS += -DANDROID_ARM
LOCAL_ARM_MODE := arm
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS += -DANDROID_X86
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DANDROID_MIPS
endif

LIBRETRODIR = ../
# libretro
CFILES += $(LIBRETRODIR)/libretro.c $(LIBRETRODIR)/glsym.c $(LIBRETRODIR)/libco/libco.c $(LIBRETRODIR)/opengl_state_machine.c \
          $(LIBRETRODIR)/audio_plugin.c $(LIBRETRODIR)/input_plugin.c $(LIBRETRODIR)/resampler.c $(LIBRETRODIR)/sinc.c $(LIBRETRODIR)/utils.c

# RSP Plugin
RSPDIR = ../../mupen64plus-rsp-hle
CFILES += \
    $(RSPDIR)/src/alist.c \
    $(RSPDIR)/src/cicx105.c \
    $(RSPDIR)/src/jpeg.c \
    $(RSPDIR)/src/main.c

CXXFILES += \
    $(RSPDIR)/src/ucode1.cpp \
    $(RSPDIR)/src/ucode2.cpp \
    $(RSPDIR)/src/ucode3.cpp \
    $(RSPDIR)/src/ucode3mp3.cpp

VIDEODIR = ../../gles2glide64/src
INCFLAGS += $(VIDEODIR)/Glitch64/inc
CXXFILES += $(VIDEODIR)/Glide64/3dmath.cpp \
            $(VIDEODIR)/Glide64/Config.cpp \
            $(VIDEODIR)/Glide64/FBtoScreen.cpp \
            $(VIDEODIR)/Glide64/Main.cpp \
            $(VIDEODIR)/Glide64/Util.cpp \
            $(VIDEODIR)/Glide64/CRC.cpp \
            $(VIDEODIR)/Glide64/Debugger.cpp \
            $(VIDEODIR)/Glide64/Ini.cpp \
            $(VIDEODIR)/Glide64/TexBuffer.cpp \
            $(VIDEODIR)/Glide64/rdp.cpp \
            $(VIDEODIR)/Glide64/Combine.cpp \
            $(VIDEODIR)/Glide64/DepthBufferRender.cpp \
            $(VIDEODIR)/Glide64/Keys.cpp \
            $(VIDEODIR)/Glide64/TexCache.cpp \
            $(VIDEODIR)/Glitch64/combiner.cpp \
            $(VIDEODIR)/Glitch64/geometry.cpp \
            $(VIDEODIR)/Glitch64/glState.cpp \
            $(VIDEODIR)/Glitch64/main.cpp \
            $(VIDEODIR)/Glitch64/textures.cpp

COREDIR = ../../mupen64plus-core
PLATFORM_EXT := unix
CFILES += \
    $(COREDIR)/src/api/callbacks.c \
    $(COREDIR)/src/api/common.c \
    $(COREDIR)/src/api/config.c \
    $(COREDIR)/src/api/frontend.c \
    $(COREDIR)/src/main/main.c \
    $(COREDIR)/src/main/md5.c \
    $(COREDIR)/src/main/rom.c \
    $(COREDIR)/src/main/util.c \
    $(COREDIR)/src/memory/dma.c \
    $(COREDIR)/src/memory/flashram.c \
    $(COREDIR)/src/memory/memory.c \
    $(COREDIR)/src/memory/n64_cic_nus_6105.c \
    $(COREDIR)/src/memory/pif.c \
    $(COREDIR)/src/memory/tlb.c \
    $(COREDIR)/src/osal/files_$(PLATFORM_EXT).c \
    $(COREDIR)/src/plugin/plugin.c \
    $(COREDIR)/src/r4300/empty_dynarec.c \
    $(COREDIR)/src/r4300/profile.c \
    $(COREDIR)/src/r4300/recomp.c \
    $(COREDIR)/src/r4300/exception.c \
    $(COREDIR)/src/r4300/pure_interp.c \
    $(COREDIR)/src/r4300/reset.c \
    $(COREDIR)/src/r4300/interupt.c \
    $(COREDIR)/src/r4300/r4300.c

LOCAL_SRC_FILES += $(CXXFILES) $(CFILES)

LOCAL_CFLAGS += -O2 -Wall -ffast-math -fexceptions -DGLES -DANDROID -DNOSSE -DNO_ASM -D__LIBRETRO__ -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -std=gnu99
LOCAL_CXXFLAGS += -O2 -Wall -ffast-math -fexceptions -DGLES -DANDROID -DNOSSE -DNO_ASM -D__LIBRETRO__ -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE
LOCAL_LDLIBS += -lz -llog -lGLESv2
LOCAL_C_INCLUDES = $(INCFLAGS) $(COREDIR)/src $(COREDIR)/src/api ../libco ../

include $(BUILD_SHARED_LIBRARY)

