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

ifeq ($(platform), unix)
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T
   
   fpic = -fPIC
   GL_LIB := -lGL
   PLATFORM_EXT := unix
else ifeq ($(platform), osx)
   TARGET := $(TARGET_NAME)_libretro.dylib
   LDFLAGS += -dynamiclib
   fpic = -fPIC

   CXXFLAGS += -D__MACOSX__
   GL_LIB := -framework OpenGL
   PLATFORM_EXT := unix
else ifeq ($(platform), ios)
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   LDFLAGS += -dynamiclib
   fpic = -fPIC

   CC = clang -arch armv7 -isysroot $(IOSSDK)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
   CXXFLAGS += -DNO_ASM
   PLATFORM_EXT := unix

else ifeq ($(platform), android)
   TARGET := $(TARGET_NAME)_libretro.so
   CXXFLAGS += -fpermissive 
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T

   CC = arm-linux-androideabi-gcc
   CXX = arm-linux-androideabi-g++
   CXXFLAGS += -DNO_ASM
   
   fpic = -fPIC
   PLATFORM_EXT := unix
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_libretro_psp1.a
   CC = psp-gcc$(EXE_EXT)
   CXX = psp-g++$(EXE_EXT)
   AR = psp-ar$(EXE_EXT)
   CXXFLAGS += -DPSP -G0
	STATIC_LINKING = 1
else ifeq ($(platform), wii)
   TARGET := $(TARGET_NAME)_libretro_wii.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   CXXFLAGS += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -D__POWERPC__ -D__ppc__ -DWORDS_BIGENDIAN=1
	STATIC_LINKING = 1
else
   TARGET := $(TARGET_NAME)_libretro.dll
   LDFLAGS += -shared -static-libgcc -static-libstdc++ -Wl,--version-script=libretro/link.T -lwinmm -lgdi32
   GL_LIB := -lopengl32
   CFLAGS += -msse -msse2
   CXXFLAGS += -msse -msse2
   PLATFORM_EXT := win32
endif

ifeq ($(DEBUG), 1)
   CXXFLAGS += -O0 -g
   CXXFLAGS += -DOPENGL_DEBUG
   CFLAGS += -O0 -g
   CFLAGS += -DOPENGL_DEBUG
else
   CXXFLAGS += -O3
   CFLAGS += -O3
endif

# libretro
CFILES += libretro/libretro.c libretro/glsym.c libretro/libco/libco.c

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
VIDEODIR = mupen64plus-video-rice/src

CXXFLAGS += -DSDL_VIDEO_OPENGL=1
CFILES += \
	$(VIDEODIR)/liblinux/BMGImage.c \
	$(VIDEODIR)/liblinux/bmp.c \
    \
	$(VIDEODIR)/osal_dynamiclib_$(PLATFORM_EXT).c \
	$(VIDEODIR)/osal_files_$(PLATFORM_EXT).c

CXXFILES += \
	$(VIDEODIR)/liblinux/BMGUtils.cpp \
	$(VIDEODIR)/Blender.cpp \
    $(VIDEODIR)/Combiner.cpp \
	$(VIDEODIR)/Config.cpp \
	$(VIDEODIR)/ConvertImage.cpp \
	$(VIDEODIR)/ConvertImage16.cpp \
	$(VIDEODIR)/Debugger.cpp \
	$(VIDEODIR)/DecodedMux.cpp \
	$(VIDEODIR)/DeviceBuilder.cpp \
	$(VIDEODIR)/FrameBuffer.cpp \
	$(VIDEODIR)/GraphicsContext.cpp \
	$(VIDEODIR)/OGLCombiner.cpp \
	$(VIDEODIR)/OGLDecodedMux.cpp \
	$(VIDEODIR)/OGLExtCombiner.cpp \
	$(VIDEODIR)/OGLExtRender.cpp \
	$(VIDEODIR)/OGLES2FragmentShaders.cpp \
	$(VIDEODIR)/OGLGraphicsContext.cpp \
	$(VIDEODIR)/OGLRender.cpp \
	$(VIDEODIR)/OGLRenderExt.cpp \
	$(VIDEODIR)/OGLTexture.cpp \
	$(VIDEODIR)/Render.cpp \
	$(VIDEODIR)/RenderBase.cpp \
	$(VIDEODIR)/RenderExt.cpp \
	$(VIDEODIR)/RenderTexture.cpp \
	$(VIDEODIR)/RSP_Parser.cpp \
	$(VIDEODIR)/RSP_S2DEX.cpp \
	$(VIDEODIR)/Texture.cpp \
	$(VIDEODIR)/TextureFilters.cpp \
	$(VIDEODIR)/TextureFilters_2xsai.cpp \
	$(VIDEODIR)/TextureFilters_hq2x.cpp \
	$(VIDEODIR)/TextureFilters_hq4x.cpp \
	$(VIDEODIR)/TextureManager.cpp \
	$(VIDEODIR)/VectorMath.cpp \
	$(VIDEODIR)/Video-libretro.cpp



# Audio Plugin
AUDIODIR = mupen64plus-audio-libretro
CFILES += $(AUDIODIR)/plugin.c

# Input Plugin
INPUTDIR = mupen64plus-input-libretro
CFILES += $(INPUTDIR)/plugin.c 

# Core
COREDIR = mupen64plus-core
CFILES += \
    $(COREDIR)/src/api/callbacks.c \
    $(COREDIR)/src/api/common.c \
    $(COREDIR)/src/api/config.c \
    $(COREDIR)/src/api/frontend.c \
    $(COREDIR)/src/api/vidext-retro.c \
    $(COREDIR)/src/main/lirc.c \
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
	 $(COREDIR)/src/osal/dynamiclib_$(PLATFORM_EXT).c \
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


OBJECTS    = $(CXXFILES:.cpp=.o) $(CFILES:.c=.o)
CPPFLAGS   += -D__LIBRETRO__ $(fpic) -I$(COREDIR)/src -I$(COREDIR)/src/api -Ilibretro/libco -Ilibretro
CXXFLAGS   += -DM64P_CORE_PROTOTYPES $(fpic)
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
