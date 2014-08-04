DEBUG=0
GLIDE2GL=1
PERF_TEST=0
HAVE_SHARED_CONTEXT=0
SINGLE_THREAD=0

DYNAFLAGS :=
INCFLAGS  :=
COREFLAGS :=
CPUFLAGS  :=

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

ifeq ($(firstword $(filter x86_64,$(UNAME))),x86_64)
	WITH_DYNAREC=x86_64
endif
ifeq ($(firstword $(filter amd64,$(UNAME))),amd64)
	WITH_DYNAREC=x86_64
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
   PLATFORM_EXT := unix
else ifneq (,$(findstring rpi,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T
   fpic = -fPIC
   GLES = 1
   GL_LIB := -lGLESv2
   INCFLAGS += -I/opt/vc/include
	CPUFLAGS += -DARMv5_ONLY -DNO_ASM -DNOSSE
   PLATFORM_EXT := unix
   WITH_DYNAREC=arm

else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   LDFLAGS += -dynamiclib
   OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(shell sw_vers | grep -c 10.9),1)
   LDFLAGS += -mmacosx-version-min=10.5
endif
   fpic = -fPIC

   PLATCFLAGS += -D__MACOSX__
   GL_LIB := -framework OpenGL
   PLATFORM_EXT := unix
else ifneq (,$(findstring ios,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   PLATCFLAGS += -DIOS -marm -mllvm -arm-reserve-r9
   LDFLAGS += -dynamiclib -marm
   fpic = -fPIC
   GLES = 1
   GL_LIB := -framework OpenGLES
   GLIDE2GL=0

   CC = clang -arch armv7 -isysroot $(IOSSDK)
   CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
   OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   CC += -miphoneos-version-min=5.0
   CC_AS += -miphoneos-version-min=5.0
   CXX += -miphoneos-version-min=5.0
   PLATCFLAGS += -miphoneos-version-min=5.0
endif
   PLATCFLAGS += -DIOS -DHAVE_POSIX_MEMALIGN
	CPUFLAGS += -DARMv5_ONLY -DNO_ASM -DNOSSE -DARM
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
	PLATCFLAGS += -DANDROID
   CPUCFLAGS  += -DNO_ASM -DNOSSE
   HAVE_NEON = 1
	CPUFLAGS += -marm -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -D__arm__ -DARM_ASM -D__NEON_OPT
	CFLAGS += -DANDROID
   
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
   PLATCFLAGS += -DNO_ASM -DNOSSE -D__BLACKBERRY_QNX__
   HAVE_NEON = 1
	CPUFLAGS += -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp -D__arm__ -DARM_ASM -D__NEON_OPT
	CFLAGS += -D__QNX__
   
   PLATFORM_EXT := unix
else ifneq (,$(findstring armv,$(platform)))
   CC = gcc
   CXX = g++
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=libretro/link.T -Wl,--no-undefined
   INCFLAGS += -I.
	WITH_DYNAREC=arm
ifneq (,$(findstring gles,$(platform)))
   GLES := 1
else
   GL_LIB := -lGL
endif
ifneq (,$(findstring cortexa8,$(platform)))
   CPUFLAGS += -marm -mcpu=cortex-a8
else ifneq (,$(findstring cortexa9,$(platform)))
   CPUFLAGS += -marm -mcpu=cortex-a9
endif
   CPUFLAGS += -marm
ifneq (,$(findstring neon,$(platform)))
   CPUFLAGS += -mfpu=neon
   HAVE_NEON = 1
endif
ifneq (,$(findstring softfloat,$(platform)))
   CPUFLAGS += -mfloat-abi=softfp
else ifneq (,$(findstring hardfloat,$(platform)))
   CPUFLAGS += -mfloat-abi=hard
endif
   PLATCFLAGS += -DARM
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_emscripten.bc
   GLES := 1
   #GLIDE2GL=0
   CPUFLAGS += -DNO_ASM -DNOSSE
   PLATCFLAGS += -DCC_resampler=mupen_CC_resampler -Dsinc_resampler=mupen_sinc_resampler \
               -Drglgen_symbol_map=mupen_rglgen_symbol_map -Dmain_exit=mupen_main_exit \
               -Dadler32=mupen_adler32 -Drarch_resampler_realloc=mupen_rarch_resampler_realloc \
               -Daudio_convert_s16_to_float_C=mupen_audio_convert_s16_to_float_C -Daudio_convert_float_to_s16_C=mupen_audio_convert_float_to_s16_C \
               -Daudio_convert_init_simd=mupen_audio_convert_init_simd -Drglgen_resolve_symbols_custom=mupen_rglgen_resolve_symbols_custom \
               -Drglgen_resolve_symbols=mupen_rglgen_resolve_symbols
   PLATFORM_EXT := unix
   #HAVE_SHARED_CONTEXT := 1
else ifneq (,$(findstring win,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dll
   LDFLAGS += -shared -static-libgcc -static-libstdc++ -Wl,--version-script=libretro/link.T -lwinmm -lgdi32
   GL_LIB := -lopengl32
   PLATFORM_EXT := win32
   CC = gcc
   CXX = g++
endif

# libco
CFILES += libretro/libco/libco.c

# Dirs
RSPDIR = mupen64plus-rsp-hle
CXD4DIR = mupen64plus-rsp-cxd4
COREDIR = mupen64plus-core
VIDEODIR_RICE=gles2rice/src
VIDEODIR_GLN64 = gles2n64/src
ifeq ($(GLIDE2GL), 1)
VIDEODIR_GLIDE = glide2gl/src
else
VIDEODIR_GLIDE = gles2glide64/src
endif

INCFLAGS := \
	-I$(COREDIR)/src \
	-I$(COREDIR)/src/api \
	-I$(VIDEODIR_GLIDE)/Glitch64/inc \
	-Ilibretro/libco \
	-Ilibretro

CFILES += $(wildcard $(RSPDIR)/src/*.c)
CXXFILES += $(wildcard $(RSPDIR)/src/*.cpp)

CFILES += $(CXD4DIR)/rsp.c

# Core
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
   DYNAFLAGS += -DDYNAREC
   ifeq ($(WITH_DYNAREC), arm)
      DYNAFLAGS += -DNEW_DYNAREC=3

      CFILES += $(COREDIR)/src/r4300/new_dynarec/new_dynarec.c \
		$(COREDIR)/src/r4300/empty_dynarec.c \
		$(COREDIR)/src/r4300/instr_counters.c

      OBJECTS += \
         $(COREDIR)/src/r4300/new_dynarec/linkage_$(WITH_DYNAREC).o

   else
	   CPUFLAGS := -msse -msse2
      CFILES += $(wildcard $(COREDIR)/src/r4300/$(WITH_DYNAREC)/*.c)
   endif
else
   CFILES += $(COREDIR)/src/r4300/empty_dynarec.c
endif

### VIDEO PLUGINS ###

# Rice

CXXFILES += $(wildcard $(VIDEODIR_RICE)/*.cpp)

LIBRETRO_SRC += $(wildcard libretro/*.c)


ifeq ($(HAVE_NEON), 1)
CFILES += $(wildcard $(VIDEODIR_GLN64)/*.c)
OBJECTS += libretro/utils_neon.o libretro/sinc_neon.o
else
GLN64VIDEO_BLACKLIST = $(VIDEODIR_GLN64)/3DMathNeon.c $(VIDEODIR_GLN64)/gSPNeon.c
CFILES += $(filter-out $(GLN64VIDEO_BLACKLIST), $(wildcard $(VIDEODIR_GLN64)/*.c))
endif

ifeq ($(PERF_TEST), 1)
COREFLAGS += -DPERF_TEST
endif

ifeq ($(HAVE_SHARED_CONTEXT), 1)
COREFLAGS += -DHAVE_SHARED_CONTEXT
endif

ifeq ($(SINGLE_THREAD), 1)
COREFLAGS += -DSINGLE_THREAD
endif

CFILES += $(LIBRETRO_SRC)

# Glide64
ifeq ($(GLIDE2GL), 1)
CFILES += $(VIDEODIR_GLIDE)/Glide64/3dmath.c \
			 $(VIDEODIR_GLIDE)/Glide64/Combine.c \
			 $(VIDEODIR_GLIDE)/Glide64/DepthBufferRender.c \
			 $(VIDEODIR_GLIDE)/Glide64/FBtoScreen.c \
			 $(VIDEODIR_GLIDE)/Glide64/glide64_crc.c \
			 $(VIDEODIR_GLIDE)/Glide64/glidemain.c \
			 $(VIDEODIR_GLIDE)/Glide64/rdp.c \
			 $(VIDEODIR_GLIDE)/Glide64/TexCache.c \
			 $(VIDEODIR_GLIDE)/Glide64/Util.c
CFILES += $(wildcard $(VIDEODIR_GLIDE)/Glitch64/*.c)
else
CXXFILES += $(wildcard $(VIDEODIR_GLIDE)/Glide64/*.cpp) \
				$(wildcard $(VIDEODIR_GLIDE)/Glitch64/*.cpp)
endif

### Angrylion's renderer ###
VIDEODIR_ANGRYLION = angrylionrdp
CFILES += $(wildcard $(VIDEODIR_ANGRYLION)/*.c)
CXXFILES += $(wildcard $(VIDEODIR_ANGRYLION)/*.cpp)

ifeq ($(GLES), 1)
GLFLAGS += -DGLES -DHAVE_OPENGLES2 -DDISABLE_3POINT
CFILES += libretro/glsym/glsym_es2.c
else
GLFLAGS += -DHAVE_OPENGL
CFILES += libretro/glsym/glsym_gl.c
endif

CFILES += libretro/glsym/rglgen.c

COREFLAGS += -D__LIBRETRO__ -DINLINE="inline" -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES \
				 -D_ENDUSER_RELEASE -DSDL_VIDEO_OPENGL_ES2=1 -DSINC_LOWER_QUALITY \

ifeq ($(DEBUG), 1)
   CPUOPTS += -O0 -g
   CPUOPTS += -DOPENGL_DEBUG
else
   CPUOPTS += -O2 -DNDEBUG
endif

### Finalize ###
OBJECTS    += $(CXXFILES:.cpp=.o) $(CFILES:.c=.o)
CXXFLAGS   +=  $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)
CFLAGS     +=  $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)

LDFLAGS    += -lm $(fpic)

all: $(TARGET)

$(COREDIR)/src/r4300/new_dynarec/linkage_arm.o: $(COREDIR)/src/r4300/new_dynarec/linkage_arm.S
	$(CC_AS) $(CFLAGS) -c $^ -o $@

%.o: %.S
	$(CC_AS) $(CFLAGS) -c $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $^ -o $@

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(GL_LIB)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean
