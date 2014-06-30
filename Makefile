DEBUG=0
GLIDE2GL=1
HAVE_HWFBE=0
PERF_TEST=0
HAVE_SHARED_CONTEXT=0

UNAME=$(shell uname -a)

ifeq ($(platform),)
platform = unix
ifeq ($(UNAME),)
   platform = win
else ifneq ($(findstring MINGW,$(UNAME)),)
   platform = win
else ifneq ($(findstring Darwin,$(UNAME)),)
   platform = osx
else ifneq ($(findstring win,$(UNAME)),)
   platform = win
endif
endif

TARGET_NAME := mupen64plus
CC_AS ?= $(CC)

ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T -Wl,--no-undefined
   
   fpic = -fPIC
ifneq (,$(findstring gles,$(platform)))
   GLES = 1
   GL_LIB := -lGLESv2
else
   GL_LIB := -lGL
endif
   CPPFLAGS += -msse -msse2
   PLATFORM_EXT := unix
else ifneq (,$(findstring rpi,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T
   fpic = -fPIC
   GLES = 1
   GL_LIB := -lGLESv2
   CPPFLAGS = -DNOSSE -I/opt/vc/include -DARMv5_ONLY -DNO_ASM
   PLATFORM_EXT := unix
   WITH_DYNAREC=arm

else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   LDFLAGS += -dynamiclib
   OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   LDFLAGS += -mmacosx-version-min=10.5
endif
   fpic = -fPIC

   CPPFLAGS += -D__MACOSX__
   GL_LIB := -framework OpenGL
   PLATFORM_EXT := unix
ifeq ($(firstword $(filter x86_64,$(UNAME))),x86_64)
	WITH_DYNAREC=x86
endif
ifeq ($(firstword $(filter amd64,$(UNAME))),amd64)
	WITH_DYNAREC=x86_64
endif
else ifneq (,$(findstring ios,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   CPPFLAGS += -DIOS -marm -mllvm -arm-reserve-r9
   LDFLAGS += -dynamiclib -marm
   fpic = -fPIC
   GLES = 1
   GL_LIB := -framework OpenGLES

   CC = clang -arch armv7 -isysroot $(IOSSDK)
   CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
   OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   CC += -miphoneos-version-min=5.0
   CC_AS += -miphoneos-version-min=5.0
   CXX += -miphoneos-version-min=5.0
   CPPFLAGS += -miphoneos-version-min=5.0
endif
   CPPFLAGS += -DNO_ASM -DIOS -DNOSSE -DHAVE_POSIX_MEMALIGN -DDISABLE_3POINT
   CPPFLAGS += -DARM
   PLATFORM_EXT := unix
   WITH_DYNAREC=arm
else ifneq (,$(findstring android,$(platform)))
   fpic = -fPIC
   TARGET := $(TARGET_NAME)_libretro_android.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T -Wl,--no-undefined -Wl,--warn-common
   GL_LIB := -lGLESv2

   CC = arm-linux-androideabi-gcc
   CXX = arm-linux-androideabi-g++
	WITH_DYNAREC=arm
   GLES = 1
   CPPFLAGS += -DNO_ASM -DNOSSE -DANDROID
   HAVE_NEON = 1
	CPUFLAGS += -marm -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -D__arm__
   CPPFLAGS += $(CPUFLAGS)
	CFLAGS += $(CPUFLAGS) -DANDROID
	CPPFLAGS += -DARM_ASM -D__NEON_OPT
   
   PLATFORM_EXT := unix
else ifeq ($(platform), qnx)
   fpic = -fPIC
   TARGET := $(TARGET_NAME)_libretro_qnx.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T -Wl,--no-undefined -Wl,--warn-common
   GL_LIB := -lGLESv2

   CC = qcc -Vgcc_ntoarmv7le
   CC_AS = qcc -Vgcc_ntoarmv7le
   CXX = QCC -Vgcc_ntoarmv7le
   AR = QCC -Vgcc_ntoarmv7le
	WITH_DYNAREC=arm
   GLES = 1
   CPPFLAGS += -DNO_ASM -DNOSSE -D__BLACKBERRY_QNX__
   HAVE_NEON = 1
	CPUFLAGS += -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp -D__arm__
   CPPFLAGS += $(CPUFLAGS)
	CFLAGS += $(CPUFLAGS) -D__QNX__
	CPPFLAGS += -DARM_ASM -D__NEON_OPT
   
   PLATFORM_EXT := unix
else ifneq (,$(findstring armv,$(platform)))
   CC = gcc
   CXX = g++
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=libretro/link.T -Wl,--no-undefined
   CPPFLAGS += -I.
	WITH_DYNAREC=arm
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
   CC = gcc
   CXX = g++
endif

# libco
CFILES += libretro/libco/libco.c

# RSP Plugins
RSPDIR = mupen64plus-rsp-hle
CFILES += $(wildcard $(RSPDIR)/src/*.c)
CXXFILES += $(wildcard $(RSPDIR)/src/*.cpp)

CXB4DIR = mupen64plus-rsp-cxd4
CFILES += $(CXB4DIR)/rsp.c

# Core
COREDIR = mupen64plus-core
CFILES += \
    $(COREDIR)/src/api/callbacks.c \
    $(COREDIR)/src/api/common.c \
    $(COREDIR)/src/api/config.c \
    $(COREDIR)/src/api/frontend.c \
    $(COREDIR)/src/main/cheat.c \
    $(COREDIR)/src/main/eventloop.c \
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


#   $(COREDIR)/src/api/debugger.c \
#   $(COREDIR)/src/main/ini_reader.c \
#   

### DYNAREC ###
ifdef WITH_DYNAREC
   CPPFLAGS += -DDYNAREC
   ifeq ($(WITH_DYNAREC), arm)
      CPPFLAGS += -DNEW_DYNAREC=3

      CFILES += $(COREDIR)/src/r4300/new_dynarec/new_dynarec.c \
		$(COREDIR)/src/r4300/empty_dynarec.c \
		$(COREDIR)/src/r4300/instr_counters.c

      OBJECTS += \
         $(COREDIR)/src/r4300/new_dynarec/linkage_$(WITH_DYNAREC).o

   else
      CFILES += $(wildcard $(COREDIR)/src/r4300/$(WITH_DYNAREC)/*.c)
   endif
endif

### VIDEO PLUGINS ###

# Rice
VIDEODIR_RICE=gles2rice/src
CPPFLAGS += -DSDL_VIDEO_OPENGL_ES2=1
#LDFLAGS += -lpng

CXXFILES += $(wildcard $(VIDEODIR_RICE)/*.cpp)

CFILES += \
   $(VIDEODIR_RICE)/osal_files_$(PLATFORM_EXT).c

# gln64
VIDEODIR_GLN64 = gles2n64/src

# TODO: Use neon versions when possible

libretrosrc += $(wildcard libretro/*.c)

CFLAGS += -DSINC_LOWER_QUALITY
CPPFLAGS += -DSINC_LOWER_QUALITY

ifeq ($(HAVE_NEON), 1)
CFILES += $(wildcard $(VIDEODIR_GLN64)/*.c)
CFLAGS += -DHAVE_NEON
CPPFLAGS += -DHAVE_NEON
OBJECTS += libretro/utils_neon.o libretro/sinc_neon.o
else
gln64videoblack = $(VIDEODIR_GLN64)/3DMathNeon.c $(VIDEODIR_GLN64)/gSPNeon.c
CFILES += $(filter-out $(gln64videoblack), $(wildcard $(VIDEODIR_GLN64)/*.c))
endif

ifeq ($(PERF_TEST), 1)
CFLAGS += -DPERF_TEST
CPPFLAGS += -DPERF_TEST
endif

ifeq ($(HAVE_SHARED_CONTEXT), 1)
CFLAGS += -DHAVE_SHARED_CONTEXT
CPPFLAGS += -DHAVE_SHARED_CONTEXT
endif


CFILES += $(libretrosrc)

# Glide64
ifeq ($(GLIDE2GL), 1)
VIDEODIR_GLIDE = glide2gl/src
else
VIDEODIR_GLIDE = gles2glide64/src
CFILES += $(VIDEODIR_GLIDE)/Glide64/TexBuffer.c
endif
CPPFLAGS += -I$(VIDEODIR_GLIDE)/Glitch64/inc
CFILES += $(VIDEODIR_GLIDE)/Glide64/3dmath.c \
			 $(VIDEODIR_GLIDE)/Glide64/Combine.c \
			 $(VIDEODIR_GLIDE)/Glide64/DepthBufferRender.c \
			 $(VIDEODIR_GLIDE)/Glide64/FBtoScreen.c \
			 $(VIDEODIR_GLIDE)/Glide64/glide64_crc.c \
			 $(VIDEODIR_GLIDE)/Glide64/glidemain.c \
			 $(VIDEODIR_GLIDE)/Glide64/rdp.c \
			 $(VIDEODIR_GLIDE)/Glide64/TexCache.c \
			 $(VIDEODIR_GLIDE)/Glide64/Util.c
CXXFILES += $(wildcard $(VIDEODIR_GLIDE)/Glide64/*.cpp)

CFILES += $(wildcard $(VIDEODIR_GLIDE)/Glitch64/*.c)
CXXFILES += $(wildcard $(VIDEODIR_GLIDE)/Glitch64/*.cpp)

ifeq ($(HAVE_HWFBE), 1)
HWFBE_FLAGS := -DHAVE_HWFBE
CFLAGS += $(HWFBE_FLAGS)
CPPFLAGS += $(HWFBE_FLAGS)
CXXFILES += $(VIDEODIR_GLIDE)/Glide64/TexBuffer.c
endif

### Angrylion's renderer ###
VIDEODIR_ANGRYLION = angrylionrdp
CFILES += $(wildcard $(VIDEODIR_ANGRYLION)/*.c)
CXXFILES += $(wildcard $(VIDEODIR_ANGRYLION)/*.cpp)

ifeq ($(GLES), 1)
CPPFLAGS += -DHAVE_OPENGLES2 -DDISABLE_3POINT
CFILES += libretro/glsym/glsym_es2.c
else
CPPFLAGS += -DHAVE_OPENGL
CFILES += libretro/glsym/glsym_gl.c
endif

CFILES += libretro/glsym/rglgen.c

### Finalize ###
OBJECTS    += $(CXXFILES:.cpp=.o) $(CFILES:.c=.o)
CPPFLAGS   += -D__LIBRETRO__ -DINLINE="inline" -DM64P_PLUGIN_API -I$(COREDIR)/src -I$(COREDIR)/src/api -Ilibretro/libco -Ilibretro
CPPFLAGS   += -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE $(fpic)
LDFLAGS    += -lm $(fpic)


ifeq ($(DEBUG), 1)
   CPPFLAGS += -O0 -g
   CPPFLAGS += -DOPENGL_DEBUG
else
   CPPFLAGS += -O3 -DNDEBUG
endif

all: $(TARGET)

$(COREDIR)/src/r4300/new_dynarec/linkage_arm.o: $(COREDIR)/src/r4300/new_dynarec/linkage_arm.S
	$(CC_AS) $(CFLAGS) -c $^ -o $@

%.o: %.S
	$(CC_AS) $(CFLAGS) -c $^ -o $@

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(GL_LIB)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean
