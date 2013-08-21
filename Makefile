DEBUG=0

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

TARGET_NAME := mupen64plus

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
EXE_EXT = .exe
   system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   system_platform = osx
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   system_platform = win
endif

ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T
   
   fpic = -fPIC
ifneq (,$(findstring gles,$(platform)))
   GLES = 1
   GL_LIB := -lGLESv2
else
   GL_LIB := -lGL
endif
   PLATFORM_EXT := unix
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   LDFLAGS += -dynamiclib
   fpic = -fPIC

   CPPFLAGS += -D__MACOSX__
   GL_LIB := -framework OpenGL
   PLATFORM_EXT := unix
else ifeq ($(platform), ios)
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   LDFLAGS += -dynamiclib
   fpic = -fPIC
   GLES = 1
   GL_LIB := -framework OpenGLES

   OBJECTS += libretro/libco/armeabi_asm.o

   CC = clang -arch armv7 -isysroot $(IOSSDK)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
   CPPFLAGS += -DNO_ASM -DIOS -DNOSSE -DHAVE_POSIX_MEMALIGN
   PLATFORM_EXT := unix


else ifeq ($(platform), android)
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T -Wl,--no-undefined -Wl,--warn-common
   GL_LIB := -lGLESv2

   OBJECTS += libretro/libco/armeabi_asm.o

   CC = arm-linux-androideabi-gcc
   CXX = arm-linux-androideabi-g++
   GLES = 1
   CPPFLAGS += -DNO_ASM -DNOSSE
   
   fpic = -fPIC
   PLATFORM_EXT := unix
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_libretro_psp1.a
   CC = psp-gcc$(EXE_EXT)
   CXX = psp-g++$(EXE_EXT)
   AR = psp-ar$(EXE_EXT)
   CPPFLAGS += -DPSP -G0
   STATIC_LINKING = 1
else ifeq ($(platform), wii)
   TARGET := $(TARGET_NAME)_libretro_wii.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   CPPFLAGS += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -D__POWERPC__ -D__ppc__ -DWORDS_BIGENDIAN=1
   STATIC_LINKING = 1
else ifneq (,$(findstring armv,$(platform)))
   CC = gcc
   CXX = g++
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,--no-undefined
   CPPFLAGS += -I.
   LIBS := -lz
ifneq (,$(findstring gles,$(platform)))
   GLES := 1
else
   GL_LIB := -lGL
endif
ifneq (,$(findstring cortexa8,$(platform)))
   CPPFLAGS += -marm -mcpu=cortex-a8
else ifneq (,$(findstring cortexa9,$(platform)))
   CPPFLAGS += -marm -mcpu=cortex-a9
endif
   CPPFLAGS += -marm
ifneq (,$(findstring neon,$(platform)))
   CPPFLAGS += -mfpu=neon
   HAVE_NEON = 1
endif
ifneq (,$(findstring softfloat,$(platform)))
   CPPFLAGS += -mfloat-abi=softfp
else ifneq (,$(findstring hardfloat,$(platform)))
   CPPFLAGS += -mfloat-abi=hard
endif
   CPPFLAGS += -DARM
else ifneq (,$(findstring win,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dll
   LDFLAGS += -shared -static-libgcc -static-libstdc++ -Wl,--version-script=libretro/link.T -lwinmm -lgdi32
   GL_LIB := -lopengl32
   CPPFLAGS += -msse -msse2
   PLATFORM_EXT := win32
endif

ifeq ($(DEBUG), 1)
   CPPFLAGS += -O0 -g
   CPPFLAGS += -DOPENGL_DEBUG
else
   CPPFLAGS += -O3
endif

# libretro
CFILES += libretro/libretro.c libretro/glsym.c libretro/libco/libco.c libretro/opengl_state_machine.c \
          libretro/audio_plugin.c libretro/input_plugin.c libretro/resampler.c libretro/sinc.c libretro/utils.c

# RSP Plugin
RSPDIR = mupen64plus-rsp-hle
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

# Video Plugin
WITH_RICE = 0

ifeq ($(GLES), 1)
CPPFLAGS += -DGLES
endif

ifeq ($(WITH_GLIDE), 1)
VIDEODIR = gles2glide64/src
CPPFLAGS += -I$(VIDEODIR)/Glitch64/inc -DGLIDE64
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
else ifeq ($(WITH_RICE), 1)
VIDEODIR = gles2rice/src

CPPFLAGS += -DSDL_VIDEO_OPENGL_ES2=1
#LDFLAGS += -lpng

CXXFILES += \
   $(VIDEODIR)/Blender.cpp \
   $(VIDEODIR)/Combiner.cpp \
   $(VIDEODIR)/CombinerTable.cpp \
   $(VIDEODIR)/Config.cpp \
   $(VIDEODIR)/ConvertImage.cpp \
   $(VIDEODIR)/ConvertImage16.cpp \
   $(VIDEODIR)/Debugger.cpp \
   $(VIDEODIR)/DecodedMux.cpp \
   $(VIDEODIR)/DeviceBuilder.cpp \
   $(VIDEODIR)/DirectXDecodedMux.cpp \
   $(VIDEODIR)/FrameBuffer.cpp \
   $(VIDEODIR)/GeneralCombiner.cpp \
   $(VIDEODIR)/GraphicsContext.cpp \
   $(VIDEODIR)/OGLCombiner.cpp \
   $(VIDEODIR)/OGLDecodedMux.cpp \
   $(VIDEODIR)/OGLES2FragmentShaders.cpp \
   $(VIDEODIR)/OGLExtCombiner.cpp \
   $(VIDEODIR)/OGLExtRender.cpp \
   $(VIDEODIR)/OGLGraphicsContext.cpp \
   $(VIDEODIR)/OGLRender.cpp \
   $(VIDEODIR)/OGLRenderExt.cpp \
   $(VIDEODIR)/OGLTexture.cpp \
   $(VIDEODIR)/RSP_Parser.cpp \
   $(VIDEODIR)/RSP_S2DEX.cpp \
   $(VIDEODIR)/Render.cpp \
   $(VIDEODIR)/RenderBase.cpp \
   $(VIDEODIR)/RenderExt.cpp \
   $(VIDEODIR)/RenderTexture.cpp \
   $(VIDEODIR)/Texture.cpp \
   $(VIDEODIR)/TextureFilters.cpp \
   $(VIDEODIR)/TextureFilters_2xsai.cpp \
   $(VIDEODIR)/TextureFilters_hq2x.cpp \
   $(VIDEODIR)/TextureFilters_hq4x.cpp \
   $(VIDEODIR)/TextureManager.cpp \
   $(VIDEODIR)/VectorMath.cpp \
   $(VIDEODIR)/Video.cpp

CFILES += \
   $(VIDEODIR)/osal_files_$(PLATFORM_EXT).c \
   $(VIDEODIR)/liblinux/BMGImage.c \
   $(VIDEODIR)/liblinux/BMGUtils.c \
   $(VIDEODIR)/liblinux/bmp.c 
#   $(VIDEODIR)/liblinux/pngrw.c
else ifeq ($(WITH_GLN64), 1)
VIDEODIR = gles2n64/src

CXXFILES += \
   $(VIDEODIR)/2xSAI.cpp \
   $(VIDEODIR)/3DMath.cpp \
   $(VIDEODIR)/CRC.cpp \
   $(VIDEODIR)/Config.cpp \
   $(VIDEODIR)/DepthBuffer.cpp \
   $(VIDEODIR)/F3D.cpp \
   $(VIDEODIR)/F3DCBFD.cpp \
   $(VIDEODIR)/F3DDKR.cpp \
   $(VIDEODIR)/F3DEX.cpp \
   $(VIDEODIR)/F3DEX2.cpp \
   $(VIDEODIR)/F3DPD.cpp \
   $(VIDEODIR)/F3DWRUS.cpp \
   $(VIDEODIR)/FrameSkipper.cpp \
   $(VIDEODIR)/GBI.cpp \
   $(VIDEODIR)/L3D.cpp \
   $(VIDEODIR)/L3DEX.cpp \
   $(VIDEODIR)/L3DEX2.cpp \
   $(VIDEODIR)/N64.cpp \
   $(VIDEODIR)/OpenGL.cpp \
   $(VIDEODIR)/RDP.cpp \
   $(VIDEODIR)/RSP.cpp \
   $(VIDEODIR)/S2DEX.cpp \
   $(VIDEODIR)/S2DEX2.cpp \
   $(VIDEODIR)/ShaderCombiner.cpp \
   $(VIDEODIR)/Textures.cpp \
   $(VIDEODIR)/VI.cpp \
   $(VIDEODIR)/gDP.cpp \
   $(VIDEODIR)/gSP.cpp \
   $(VIDEODIR)/gles2N64.cpp

CFILES += \
   $(VIDEODIR)/ticks.c

#NEON
#  $(VIDEODIR)/3DMathNeon.cpp
#  $(VIDEODIR)/gSPNeon.cpp

endif

# Core
COREDIR = mupen64plus-core
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

#   $(COREDIR)/src/api/debugger.c \
#   $(COREDIR)/src/main/eventloop.c \
#   $(COREDIR)/src/main/ini_reader.c \
#   $(COREDIR)/src/main/cheat.c \
#   $(COREDIR)/src/main/savestates.c \
#   $(COREDIR)/src/osal/dynamiclib_win32.c \
#   $(COREDIR)/src/osal/files_win32.c \
#   $(COREDIR)/src/osd/osd.cpp \
#   $(COREDIR)/src/osd/screenshot.cpp \
#   $(COREDIR)/src/plugin/dummy_video.c \
#   $(COREDIR)/src/plugin/dummy_audio.c \
#   $(COREDIR)/src/plugin/dummy_rsp.c \
#   $(COREDIR)/src/plugin/dummy_input.c \


OBJECTS    += $(CXXFILES:.cpp=.o) $(CFILES:.c=.o)
CPPFLAGS   += -D__LIBRETRO__ $(fpic) -I$(COREDIR)/src -I$(COREDIR)/src/api -Ilibretro/libco -Ilibretro
CPPFLAGS   += -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE $(fpic)
CFLAGS     += -std=gnu99 $(fpic)
LDFLAGS    += -lm $(fpic) -lz

all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(GL_LIB)
endif

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean
