LOCAL_PATH := $(call my-dir)
PERFTEST = 0
HAVE_HWFBE = 0
HAVE_SHARED_CONTEXT=0
SINGLE_THREAD=0

include $(CLEAR_VARS)

LOCAL_MODULE := retro_mupen64plus
M64P_ROOT_DIR := ../..
LIBRETRODIR = ../
VIDEODIR_GLIDE = $(M64P_ROOT_DIR)/glide2gl/src
VIDEODIR_ANGRYLION = $(M64P_ROOT_DIR)/angrylionrdp
VIDEODIR_RICE = $(M64P_ROOT_DIR)/gles2rice/src
RSPDIR = $(M64P_ROOT_DIR)/mupen64plus-rsp-hle
COREDIR = $(M64P_ROOT_DIR)/mupen64plus-core
VIDEODIR_GLN64 = $(M64P_ROOT_DIR)/gles2n64/src
CXD4DIR = ../../mupen64plus-rsp-cxd4

ifeq ($(TARGET_ARCH),arm)
COMMON_FLAGS += -DANDROID_ARM -DDYNAREC -DNEW_DYNAREC=3 -DARM_ASM -DNO_ASM -DNOSSE
LOCAL_ARM_MODE := arm
LOCAL_CFLAGS += -marm
LOCAL_SRC_FILES += $(COREDIR)/src/r4300/new_dynarec/new_dynarec.c $(COREDIR)/src/r4300/empty_dynarec.c $(COREDIR)/src/r4300/instr_counters.c
LOCAL_SRC_FILES += $(COREDIR)/src/r4300/new_dynarec/linkage_arm.S
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
COMMON_FLAGS += -DHAVE_NEON -D__NEON_OPT
LOCAL_ARM_NEON := true
LOCAL_SRC_FILES += $(LIBRETRODIR)/sinc_neon.S $(LIBRETRODIR)/utils_neon.S
LOCAL_SRC_FILES += $(VIDEODIR_GLN64)/3DMathNeon.c
LOCAL_SRC_FILES += $(VIDEODIR_GLN64)/gSPNeon.c
endif
endif

ifeq ($(TARGET_ARCH),x86)
COMMON_FLAGS += -DANDROID_X86 -DDYNAREC -D__SSE2__ -D__SSE__
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
endif

ifeq ($(TARGET_ARCH),mips)
COMMON_FLAGS += -DANDROID_MIPS
endif

# libretro
LOCAL_SRC_FILES += $(LIBRETRODIR)/libretro.c $(LIBRETRODIR)/adler32.c $(LIBRETRODIR)/glsym/glsym_es2.c $(LIBRETRODIR)/glsym/rglgen.c $(LIBRETRODIR)/libco/libco.c $(LIBRETRODIR)/opengl_state_machine.c \
          $(LIBRETRODIR)/audio_plugin.c $(LIBRETRODIR)/input_plugin.c $(LIBRETRODIR)/resampler.c $(LIBRETRODIR)/sinc.c $(LIBRETRODIR)/cc_resampler.c $(LIBRETRODIR)/utils.c

# RSP Plugin
LOCAL_SRC_FILES += $(RSPDIR)/src/alist.c \
    $(RSPDIR)/src/alist_audio.c \
    $(RSPDIR)/src/alist_naudio.c \
    $(RSPDIR)/src/alist_nead.c \
    $(RSPDIR)/src/audio.c \
    $(RSPDIR)/src/cicx105.c \
    $(RSPDIR)/src/hle.c \
    $(RSPDIR)/src/jpeg.c \
    $(RSPDIR)/src/hle_memory.c \
    $(RSPDIR)/src/mp3.c \
    $(RSPDIR)/src/musyx.c \
    $(RSPDIR)/src/hle_plugin.c

# Video Plugins
LOCAL_SRC_FILES += $(VIDEODIR_RICE)/Blender.cpp \
            $(VIDEODIR_RICE)/Combiner.cpp \
            $(VIDEODIR_RICE)/CombinerTable.cpp \
            $(VIDEODIR_RICE)/RiceConfig.cpp \
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
            $(VIDEODIR_RICE)/osal_files_unix.c \
            $(VIDEODIR_RICE)/RenderBase.cpp \
            $(VIDEODIR_RICE)/Render.cpp \
            $(VIDEODIR_RICE)/RenderExt.cpp \
            $(VIDEODIR_RICE)/RenderTexture.cpp \
            $(VIDEODIR_RICE)/RSP_Parser.cpp \
            $(VIDEODIR_RICE)/RSP_S2DEX.cpp \
            $(VIDEODIR_RICE)/Texture.cpp \
            $(VIDEODIR_RICE)/TextureManager.cpp \
            $(VIDEODIR_RICE)/VectorMath.cpp \
            $(VIDEODIR_RICE)/Video.cpp

LOCAL_SRC_FILES += $(VIDEODIR_GLN64)/3DMath.c \
            $(VIDEODIR_GLN64)/glN64Config.c \
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
            $(VIDEODIR_GLIDE)/Glide64/rdp.c \
            $(VIDEODIR_GLIDE)/Glide64/Combine.c \
            $(VIDEODIR_GLIDE)/Glide64/DepthBufferRender.c \
            $(VIDEODIR_GLIDE)/Glide64/TexCache.c
LOCAL_SRC_FILES   += $(VIDEODIR_GLIDE)/Glitch64/combiner.c \
            $(VIDEODIR_GLIDE)/Glitch64/geometry.c \
            $(VIDEODIR_GLIDE)/Glitch64/glitchmain.c \
            $(VIDEODIR_GLIDE)/Glitch64/textures.c \
            $(VIDEODIR_GLIDE)/Glide64/glide64_crc.c

LOCAL_SRC_FILES +=  $(VIDEODIR_ANGRYLION)/n64video_main.c \
						  $(VIDEODIR_ANGRYLION)/n64video_vi.c \
						  $(VIDEODIR_ANGRYLION)/n64video_rdp.c \
						  $(VIDEODIR_ANGRYLION)/n64video.c

ifeq ($(HAVE_HWFBE), 1)
COMMON_FLAGS += -DHAVE_HWFBE
LOCAL_SRC_FILES += $(VIDEODIR_GLIDE)/Glide64/TexBuffer.c
endif

ifeq ($(HAVE_SHARED_CONTEXT), 1)
COMMON_FLAGS += -DHAVE_SHARED_CONTEXT
endif

ifeq ($(SINGLE_THREAD), 1)
COMMON_FLAGS += -DSINGLE_THREAD
endif

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
    $(COREDIR)/src/plugin/plugin.c \
    $(COREDIR)/src/r4300/profile.c \
    $(COREDIR)/src/r4300/recomp.c \
    $(COREDIR)/src/r4300/exception.c \
    $(COREDIR)/src/r4300/cached_interp.c \
    $(COREDIR)/src/r4300/pure_interp.c \
    $(COREDIR)/src/r4300/reset.c \
    $(COREDIR)/src/r4300/interupt.c \
    $(COREDIR)/src/r4300/tlb.c \
    $(COREDIR)/src/r4300/cp0.c \
    $(COREDIR)/src/r4300/cp1.c \
    $(COREDIR)/src/r4300/r4300.c

LOCAL_SRC_FILES += $(CXD4DIR)/rsp.c

COMMON_FLAGS += -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DM64P_PLUGIN_API -D__LIBRETRO__ -DINLINE="inline" -DSDL_VIDEO_OPENGL_ES2=1 -DHAVE_OPENGLES2 -DDISABLE_3POINT -DANDROID -DSINC_LOWER_QUALITY -DHAVE_LOGGER
COMMON_OPTFLAGS = -O3 -ffast-math

LOCAL_CFLAGS += $(COMMON_OPTFLAGS) $(COMMON_FLAGS)
LOCAL_CXXFLAGS += $(COMMON_OPTFLAGS) $(COMMON_FLAGS)
LOCAL_LDLIBS += -llog -lGLESv2
LOCAL_C_INCLUDES = $(INCFLAGS) $(COREDIR)/src $(COREDIR)/src/api ../libco ../

ifeq ($(PERFTEST), 1)
LOCAL_CFLAGS += -DPERF_TEST
LOCAL_CXXFLAGS += -DPERF_TEST
endif

include $(BUILD_SHARED_LIBRARY)

