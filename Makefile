DEBUG=0
GLIDE2GL=1
GLES2GLIDE64_NEW=0
GLIDE64MK2=0
PERF_TEST=0
HAVE_SHARED_CONTEXT=0
HAVE_CUSTOMCRC=0
SINGLE_THREAD=0

DYNAFLAGS :=
INCFLAGS  :=
COREFLAGS :=
CPUFLAGS  :=

UNAME=$(shell uname -a)

# Dirs
ROOT_DIR := .
LIBRETRO_DIR := $(ROOT_DIR)/libretro

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

# Unix
ifneq (,$(findstring unix,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined

	fpic = -fPIC

	ifneq (,$(findstring gles,$(platform)))
		GLES = 1
		GL_LIB := -lGLESv2
	else
		GL_LIB := -lGL
	endif

	PLATFORM_EXT := unix

# Raspberry Pi
else ifneq (,$(findstring rpi,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T
	fpic = -fPIC
	GLES = 1
	GL_LIB := -L/opt/vc/lib -lGLESv2
	INCFLAGS += -I/opt/vc/include
	CPUFLAGS += -DARMv5_ONLY -DNO_ASM -DNOSSE
	PLATFORM_EXT := unix
	WITH_DYNAREC=arm

# i.MX6
else ifneq (,$(findstring imx6,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T
	fpic = -fPIC
	GLES = 1
	GL_LIB := -lGLESv2
	CPUFLAGS += -DNO_ASM -DNOSSE
	PLATFORM_EXT := unix
	WITH_DYNAREC=arm
	HAVE_NEON=1

# OS X
else ifneq (,$(findstring osx,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.dylib
	LDFLAGS += -dynamiclib
	OSXVER = `sw_vers -productVersion | cut -d. -f 2`
	OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
	ifeq ($(OSX_LT_MAVERICKS),"YES")
		LDFLAGS += -mmacosx-version-min=10.5
	endif
	LDFLAGS += -stdlib=libc++
	fpic = -fPIC

	PLATCFLAGS += -D__MACOSX__
	GL_LIB := -framework OpenGL
	PLATFORM_EXT := unix

# iOS
else ifneq (,$(findstring ios,$(platform)))
	ifeq ($(IOSSDK),)
		IOSSDK := $(shell xcrun -sdk iphoneos -show-sdk-path)
	endif
	TARGET := $(TARGET_NAME)_libretro_ios.dylib
	PLATCFLAGS += -DHAVE_POSIX_MEMALIGN -DNO_ASM -DNOSSE
	CPUFLAGS += -DNO_ASM -DNOSSE -DARM
	PLATCFLAGS += -DIOS -marm
	LDFLAGS += -dynamiclib
	fpic = -fPIC
	GLES = 1
	GL_LIB := -framework OpenGLES
	GLIDE2GL=0
	HAVE_NEON=1
	CPUFLAGS += -marm -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -D__arm__ -DARM_ASM -D__NEON_OPT

	CC = clang -arch armv7 -isysroot $(IOSSDK)
	CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
	CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
	OSXVER = `sw_vers -productVersion | cut -d. -f 2`
	OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
	ifeq ($(OSX_LT_MAVERICKS),"YES")
		CC += -miphoneos-version-min=5.0
		CC_AS += -miphoneos-version-min=5.0
		CXX += -miphoneos-version-min=5.0
		PLATCFLAGS += -miphoneos-version-min=5.0
	endif
	LDFLAGS += -stdlib=libc++
	PLATFORM_EXT := unix
	WITH_DYNAREC=arm

# Android
else ifneq (,$(findstring android,$(platform)))
	fpic = -fPIC
	TARGET := $(TARGET_NAME)_libretro_android.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined -Wl,--warn-common
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

# QNX
else ifeq ($(platform), qnx)
	fpic = -fPIC
	TARGET := $(TARGET_NAME)_libretro_qnx.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined -Wl,--warn-common
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

# ARM
else ifneq (,$(findstring armv,$(platform)))
	CC = gcc
	CXX = g++
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined
	INCFLAGS += -I.
	CPUFLAGS += -DNO_ASM -DNOSSE
	WITH_DYNAREC=arm
	ifneq (,$(findstring gles,$(platform)))
		GLES := 1
		GL_LIB := -lGLESv2
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

# emscripten
else ifeq ($(platform), emscripten)
	TARGET := $(TARGET_NAME)_libretro_emscripten.bc
	GLES := 1
	CPUFLAGS += -DNO_ASM -DNOSSE
	PLATCFLAGS += -DCC_resampler=mupen_CC_resampler -Dsinc_resampler=mupen_sinc_resampler \
					-Drglgen_symbol_map=mupen_rglgen_symbol_map -Dmain_exit=mupen_main_exit \
					-Dadler32=mupen_adler32 -Drarch_resampler_realloc=mupen_rarch_resampler_realloc \
					-Daudio_convert_s16_to_float_C=mupen_audio_convert_s16_to_float_C -Daudio_convert_float_to_s16_C=mupen_audio_convert_float_to_s16_C \
					-Daudio_convert_init_simd=mupen_audio_convert_init_simd -Drglgen_resolve_symbols_custom=mupen_rglgen_resolve_symbols_custom \
					-Drglgen_resolve_symbols=mupen_rglgen_resolve_symbols
	PLATFORM_EXT := unix
	#HAVE_SHARED_CONTEXT := 1

# Windows
else ifneq (,$(findstring win,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.dll
	LDFLAGS += -shared -static-libgcc -static-libstdc++ -Wl,--version-script=$(LIBRETRO_DIR)/link.T -lwinmm -lgdi32
	GL_LIB := -lopengl32
	PLATFORM_EXT := win32
	CC = gcc
	CXX = g++
	
endif

include Makefile.common

ifeq ($(HAVE_NEON), 1)
	COREFLAGS += -DHAVE_NEON
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

COREFLAGS += -D__LIBRETRO__ -DINLINE="inline" -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES \
			-D_ENDUSER_RELEASE -DSDL_VIDEO_OPENGL_ES2=1 -DSINC_LOWER_QUALITY 

ifeq ($(DEBUG), 1)
	CPUOPTS += -O0 -g
	CPUOPTS += -DOPENGL_DEBUG
else
	CPUOPTS += -O2 -DNDEBUG
endif

### Finalize ###
OBJECTS		+= $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o) $(SOURCES_ASM:.S=.o)
CXXFLAGS	+= $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)
CFLAGS		+= $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)

ifeq ($(findstring Haiku,$(UNAME)),)
	LDFLAGS += -lm
endif

LDFLAGS	 += $(fpic)

all: $(TARGET)

$(CORE_DIR)/src/r4300/new_dynarec/linkage_arm.o: $(CORE_DIR)/src/r4300/new_dynarec/linkage_arm.S
	$(CC_AS) $(CFLAGS) -c $^ -o $@

$(VIDEODIR_RICE)/RenderBase_neon.o: $(VIDEODIR_RICE)/RenderBase_neon.S
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
