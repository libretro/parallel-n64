LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := retro

ifeq ($(TARGET_ARCH),arm)
LOCAL_CFLAGS += -DANDROID_ARM -DDYNAREC -DNEW_DYNAREC=3
LOCAL_ARM_MODE := arm
CFILES += $(COREDIR)/src/r4300/new_dynarec/new_dynarec.c
ASMFILES += $(COREDIR)/src/r4300/new_dynarec/linkage_arm.S
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS += -DANDROID_X86 -DDYNAREC
CFILES += $(COREDIR)/src/r4300/x86/assemble.c \
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
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DANDROID_MIPS
endif

LIBRETRODIR = ../
# libretro
CFILES += $(LIBRETRODIR)/libretro.c $(LIBRETRODIR)/glsym.c $(LIBRETRODIR)/libco/libco.c $(LIBRETRODIR)/opengl_state_machine.c \
          $(LIBRETRODIR)/audio_plugin.c $(LIBRETRODIR)/input_plugin.c $(LIBRETRODIR)/resampler.c $(LIBRETRODIR)/sinc.c $(LIBRETRODIR)/utils.c
ASMFILES += $(LIBRETRODIR)/libco/armeabi_asm.S

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

# Video Plugins

VIDEODIR_RICE = ../../gles2rice/src
CXXFILES += $(VIDEODIR_RICE)/Blender.cpp \
            $(VIDEODIR_RICE)/Combiner.cpp \
            $(VIDEODIR_RICE)/CombinerTable.cpp \
            $(VIDEODIR_RICE)/Config.cpp \
            $(VIDEODIR_RICE)/ConvertImage16.cpp \
            $(VIDEODIR_RICE)/ConvertImage.cpp \
            $(VIDEODIR_RICE)/Debugger.cpp \
            $(VIDEODIR_RICE)/DecodedMux.cpp \
            $(VIDEODIR_RICE)/DeviceBuilder.cpp \
            $(VIDEODIR_RICE)/DirectXDecodedMux.cpp \
            $(VIDEODIR_RICE)/FrameBuffer.cpp \
            $(VIDEODIR_RICE)/GeneralCombiner.cpp \
            $(VIDEODIR_RICE)/GraphicsContext.cpp \
            $(VIDEODIR_RICE)/OGLCombiner.cpp \
            $(VIDEODIR_RICE)/OGLDecodedMux.cpp \
            $(VIDEODIR_RICE)/OGLES2FragmentShaders.cpp \
            $(VIDEODIR_RICE)/OGLExtCombiner.cpp \
            $(VIDEODIR_RICE)/OGLExtRender.cpp \
            $(VIDEODIR_RICE)/OGLGraphicsContext.cpp \
            $(VIDEODIR_RICE)/OGLRender.cpp \
            $(VIDEODIR_RICE)/OGLRenderExt.cpp \
            $(VIDEODIR_RICE)/OGLTexture.cpp \
            $(VIDEODIR_RICE)/osal_dynamiclib_unix.c \
            $(VIDEODIR_RICE)/osal_files_unix.c \
            $(VIDEODIR_RICE)/RenderBase.cpp \
            $(VIDEODIR_RICE)/Render.cpp \
            $(VIDEODIR_RICE)/RenderExt.cpp \
            $(VIDEODIR_RICE)/RenderTexture.cpp \
            $(VIDEODIR_RICE)/RSP_Parser.cpp \
            $(VIDEODIR_RICE)/RSP_S2DEX.cpp \
            $(VIDEODIR_RICE)/Texture.cpp \
            $(VIDEODIR_RICE)/TextureFilters_2xsai.cpp \
            $(VIDEODIR_RICE)/TextureFilters.cpp \
            $(VIDEODIR_RICE)/TextureFilters_hq2x.cpp \
            $(VIDEODIR_RICE)/TextureFilters_hq4x.cpp \
            $(VIDEODIR_RICE)/TextureManager.cpp \
            $(VIDEODIR_RICE)/VectorMath.cpp \
            $(VIDEODIR_RICE)/Video.cpp
CFILES +=   $(VIDEODIR_RICE)/liblinux/BMGImage.c \
            $(VIDEODIR_RICE)/liblinux/BMGUtils.c \
            $(VIDEODIR_RICE)/liblinux/bmp.c

VIDEODIR_GLIDE = ../../gles2glide64/src
INCFLAGS += $(VIDEODIR_GLIDE)/Glitch64/inc
CXXFILES += $(VIDEODIR_GLIDE)/Glide64/3dmath.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Config.cpp \
            $(VIDEODIR_GLIDE)/Glide64/FBtoScreen.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Main.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Util.cpp \
            $(VIDEODIR_GLIDE)/Glide64/CRC.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Debugger.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Ini.cpp \
            $(VIDEODIR_GLIDE)/Glide64/TexBuffer.cpp \
            $(VIDEODIR_GLIDE)/Glide64/rdp.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Combine.cpp \
            $(VIDEODIR_GLIDE)/Glide64/DepthBufferRender.cpp \
            $(VIDEODIR_GLIDE)/Glide64/Keys.cpp \
            $(VIDEODIR_GLIDE)/Glide64/TexCache.cpp \
            $(VIDEODIR_GLIDE)/Glitch64/combiner.cpp \
            $(VIDEODIR_GLIDE)/Glitch64/geometry.cpp \
            $(VIDEODIR_GLIDE)/Glitch64/glState.cpp \
            $(VIDEODIR_GLIDE)/Glitch64/main.cpp \
            $(VIDEODIR_GLIDE)/Glitch64/textures.cpp

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
    $(COREDIR)/src/main/savestates.c \
    $(COREDIR)/src/main/util.c \
    $(COREDIR)/src/memory/dma.c \
    $(COREDIR)/src/memory/flashram.c \
    $(COREDIR)/src/memory/memory.c \
    $(COREDIR)/src/memory/n64_cic_nus_6105.c \
    $(COREDIR)/src/memory/pif.c \
    $(COREDIR)/src/memory/tlb.c \
    $(COREDIR)/src/plugin/plugin.c \
    $(COREDIR)/src/r4300/empty_dynarec.c \
    $(COREDIR)/src/r4300/profile.c \
    $(COREDIR)/src/r4300/recomp.c \
    $(COREDIR)/src/r4300/exception.c \
    $(COREDIR)/src/r4300/pure_interp.c \
    $(COREDIR)/src/r4300/reset.c \
    $(COREDIR)/src/r4300/interupt.c \
    $(COREDIR)/src/r4300/r4300.c

LOCAL_SRC_FILES += $(CXXFILES) $(CFILES) $(ASMFILES)

LOCAL_CFLAGS += -O2 -Wall -ffast-math -fexceptions -DGLES -DANDROID -DNOSSE -DNO_ASM -D__LIBRETRO__ -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -std=gnu99 -DSDL_VIDEO_OPENGL_ES2=1
LOCAL_CXXFLAGS += -O2 -Wall -ffast-math -fexceptions -DGLES -DANDROID -DNOSSE -DNO_ASM -D__LIBRETRO__ -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DSDL_VIDEO_OPENGL_ES2=1
LOCAL_LDLIBS += -lz -llog -lGLESv2
LOCAL_C_INCLUDES = $(INCFLAGS) $(COREDIR)/src $(COREDIR)/src/api ../libco ../
LOCAL_CXX_INCLUDES = $(INCFLAGS) $(COREDIR)/src $(COREDIR)/src/api ../libco ../

include $(BUILD_SHARED_LIBRARY)

