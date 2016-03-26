DEBUG=0
PERF_TEST=0
HAVE_SHARED_CONTEXT=0
SINGLE_THREAD=0
WITH_CRC=brumme
FORCE_GLES=0
HAVE_OPENGL=1
GLIDEN64=0
GLIDEN64CORE=0
GLIDEN64ES=0

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

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
	EXE_EXT = .exe
	system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
	system_platform = osx
	arch = intel
ifeq ($(shell uname -p),powerpc)
	arch = ppc
endif
else ifneq ($(findstring MINGW,$(shell uname -a)),)
	system_platform = win
endif

# Cross compile ?

ifeq (,$(ARCH))
	ARCH = $(shell uname -m)
endif

# Target Dynarec
WITH_DYNAREC = $(ARCH)

ifeq ($(ARCH), $(filter $(ARCH), i386 i686))
	WITH_DYNAREC = x86
endif

TARGET_NAME := mupen64plus
CC_AS ?= $(CC)

# Unix
ifneq (,$(findstring unix,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined

	fpic = -fPIC

   ifeq ($(FORCE_GLES),1)
		GLES = 1
		GL_LIB := -lGLESv2
	else ifneq (,$(findstring gles,$(platform)))
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
	ifneq (,$(findstring rpi2,$(platform)))
		CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
		CFLAGS = -mcpu=cortex-a7 -mfloat-abi=hard
		CXXFLAGS = -mcpu=cortex-a7 -mfloat-abi=hard
		HAVE_NEON = 0
	else
		CPUFLAGS += -DARMv5_ONLY -DNO_ASM
	endif
	PLATFORM_EXT := unix
	WITH_DYNAREC=arm

# ODROIDs
else ifneq (,$(findstring odroid,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	BOARD := $(shell cat /proc/cpuinfo | grep -i odroid | awk '{print $$3}')
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T
	fpic = -fPIC
	GLES = 1
	GL_LIB := -lGLESv2
	CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
	CFLAGS += -marm -mfloat-abi=hard -mfpu=neon
	CXXFLAGS += -marm -mfloat-abi=hard -mfpu=neon
	HAVE_NEON = 1
	ifneq (,$(findstring ODROIDC,$(BOARD)))
		# ODROID-C1
		CFLAGS += -mcpu=cortex-a5
		CXXFLAGS += -mcpu=cortex-a5
	else ifneq (,$(findstring ODROID-XU3,$(BOARD)))
		# ODROID-XU3 & -XU3 Lite
		ifeq "$(shell expr `gcc -dumpversion` \>= 4.9)" "1"
			CFLAGS += -march=armv7ve -mcpu=cortex-a15.cortex-a7
			CXXFLAGS += -march=armv7ve -mcpu=cortex-a15.cortex-a7
		else
			CFLAGS += -mcpu=cortex-a9
			CXXFLAGS += -mcpu=cortex-a9
		endif
	else
		# ODROID-U2, -U3, -X & -X2
		CFLAGS += -mcpu=cortex-a9
		CXXFLAGS += -mcpu=cortex-a9
	endif
	PLATFORM_EXT := unix
	WITH_DYNAREC=arm

# i.MX6
else ifneq (,$(findstring imx6,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T
	fpic = -fPIC
	GLES = 1
	GL_LIB := -lGLESv2
	CPUFLAGS += -DNO_ASM
	PLATFORM_EXT := unix
	WITH_DYNAREC=arm
	HAVE_NEON=1

# OS X
else ifneq (,$(findstring osx,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.dylib
	LDFLAGS += -dynamiclib
	OSXVER = `sw_vers -productVersion | cut -d. -f 2`
	OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
        LDFLAGS += -mmacosx-version-min=10.7
	LDFLAGS += -stdlib=libc++
	fpic = -fPIC

	PLATCFLAGS += -D__MACOSX__ -DOSX
	GL_LIB := -framework OpenGL
	PLATFORM_EXT := unix

	# Target Dynarec
	ifeq ($(ARCH), $(filter $(ARCH), ppc))
		WITH_DYNAREC =
	endif

# iOS
else ifneq (,$(findstring ios,$(platform)))
	ifeq ($(IOSSDK),)
		IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
	endif

	TARGET := $(TARGET_NAME)_libretro_ios.dylib
	DEFINES += -DIOS
	GLES = 1
	WITH_DYNAREC=arm
	PLATFORM_EXT := unix

	PLATCFLAGS += -DHAVE_POSIX_MEMALIGN -DNO_ASM
	PLATCFLAGS += -DIOS -marm
	CPUFLAGS += -DNO_ASM  -DARM -D__arm__ -DARM_ASM -D__NEON_OPT
	CPUFLAGS += -marm -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp
	LDFLAGS += -dynamiclib
	HAVE_NEON=1

	fpic = -fPIC
	GL_LIB := -framework OpenGLES

	CC = clang -arch armv7 -isysroot $(IOSSDK)
	CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
	CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
ifeq ($(platform),ios9)
	CC         += -miphoneos-version-min=8.0
	CC_AS      += -miphoneos-version-min=8.0
	CXX        += -miphoneos-version-min=8.0
	PLATCFLAGS += -miphoneos-version-min=8.0
else
	CC += -miphoneos-version-min=5.0
	CC_AS += -miphoneos-version-min=5.0
	CXX += -miphoneos-version-min=5.0
	PLATCFLAGS += -miphoneos-version-min=5.0
endif

# Theos iOS
else ifneq (,$(findstring theos_ios,$(platform)))
	DEPLOYMENT_IOSVERSION = 5.0
	TARGET = iphone:latest:$(DEPLOYMENT_IOSVERSION)
	ARCHS = armv7
	TARGET_IPHONEOS_DEPLOYMENT_VERSION=$(DEPLOYMENT_IOSVERSION)
	THEOS_BUILD_DIR := objs
	include $(THEOS)/makefiles/common.mk

	LIBRARY_NAME = $(TARGET_NAME)_libretro_ios
	DEFINES += -DIOS
	GLES = 1
	WITH_DYNAREC=arm

	PLATCFLAGS += -DHAVE_POSIX_MEMALIGN -DNO_ASM
	PLATCFLAGS += -DIOS -marm
	CPUFLAGS += -DNO_ASM  -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
	HAVE_NEON=1


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
	CPUCFLAGS  += -DNO_ASM
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
	PLATCFLAGS += -DNO_ASM -D__BLACKBERRY_QNX__
	HAVE_NEON = 1
	CPUFLAGS += -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
	CFLAGS += -D__QNX__

	PLATFORM_EXT := unix

# ARM
else ifneq (,$(findstring armv,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined
	fpic := -fPIC
	CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -DNOSSE
	WITH_DYNAREC=arm
	PLATCFLAGS += -DARM
	ifneq (,$(findstring gles,$(platform)))
		GLES = 1
		GL_LIB := -lGLESv2
	else
		GL_LIB := -lGL
	endif
	ifneq (,$(findstring cortexa5,$(platform)))
		CPUFLAGS += -marm -mcpu=cortex-a5
	else ifneq (,$(findstring cortexa8,$(platform)))
		CPUFLAGS += -marm -mcpu=cortex-a8
	else ifneq (,$(findstring cortexa9,$(platform)))
		CPUFLAGS += -marm -mcpu=cortex-a9
	else ifneq (,$(findstring cortexa15a7,$(platform)))
		CPUFLAGS += -marm -mcpu=cortex-a15.cortex-a7
	else
		CPUFLAGS += -marm
	endif
	ifneq (,$(findstring neon,$(platform)))
		CPUFLAGS += -D__NEON_OPT -mfpu=neon
		HAVE_NEON = 1
	endif
	ifneq (,$(findstring softfloat,$(platform)))
		CPUFLAGS += -mfloat-abi=softfp
	else ifneq (,$(findstring hardfloat,$(platform)))
		CPUFLAGS += -mfloat-abi=hard
	endif

# emscripten
else ifeq ($(platform), emscripten)
	TARGET := $(TARGET_NAME)_libretro_emscripten.bc
	GLES := 1
	WITH_DYNAREC :=
	CPUFLAGS += -Dasm=asmerror -D__asm__=asmerror -DNO_ASM -DNOSSE
	SINGLE_THREAD := 1
	PLATCFLAGS += -Drglgen_symbol_map=mupen_rglgen_symbol_map \
					  -Dmain_exit=mupen_main_exit \
					  -Dadler32=mupen_adler32 \
					  -Drglgen_resolve_symbols_custom=mupen_rglgen_resolve_symbols_custom \
					  -Drglgen_resolve_symbols=mupen_rglgen_resolve_symbols \
					  -Dsinc_resampler=mupen_sinc_resampler \
					  -Dnearest_resampler=mupen_nearest_resampler \
					  -DCC_resampler=mupen_CC_resampler \
					  -Daudio_resampler_driver_find_handle=mupen_audio_resampler_driver_find_handle \
					  -Daudio_resampler_driver_find_ident=mupen_audio_resampler_driver_find_ident \
					  -Drarch_resampler_realloc=mupen_rarch_resampler_realloc \
					  -Daudio_convert_s16_to_float_C=mupen_audio_convert_s16_to_float_C \
					  -Daudio_convert_float_to_s16_C=mupen_audio_convert_float_to_s16_C \
					  -Daudio_convert_init_simd=mupen_audio_convert_init_simd

	HAVE_NEON = 0
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

COREFLAGS += -D__LIBRETRO__ -DINLINE="inline" -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DSINC_LOWER_QUALITY


ifeq ($(HAVE_OPENGL),1)
COREFLAGS += -DHAVE_OPENGL
endif

ifeq ($(DEBUG), 1)
	CPUOPTS += -O0 -g
	CPUOPTS += -DOPENGL_DEBUG
else
	CPUOPTS += -O2 -DNDEBUG
endif

ifeq ($(platform), qnx)
	CFLAGS   += -Wp,-MMD
	CXXFLAGS += -Wp,-MMD
else
	CFLAGS   += -std=gnu89 -MMD
ifeq ($(GLIDEN64),1)
	CXXFLAGS += -std=c++0x -MMD
else
	CXXFLAGS += -std=gnu++98 -MMD
endif
ifeq ($(GLIDEN64ES),1)
	CFLAGS   += -DGLIDEN64ES
	CXXFLAGS += -DGLIDEN64ES
endif
ifeq ($(GLIDEN64CORE),1)
	CFLAGS += -DCORE
	CXXFLAGS += -DCORE
endif
endif

### Finalize ###
OBJECTS		+= $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o) $(SOURCES_ASM:.S=.o)
CXXFLAGS	+= $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)
CFLAGS		+= $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)

ifeq ($(findstring Haiku,$(UNAME)),)
	LDFLAGS += -lm
endif

LDFLAGS	 += $(fpic)

ifeq ($(platform), theos_ios)
COMMON_FLAGS := -DIOS $(COMMON_DEFINES) $(INCFLAGS) -I$(THEOS_INCLUDE_PATH) -Wno-error
$(LIBRARY_NAME)_ASFLAGS += $(CFLAGS) $(COMMON_FLAGS)
$(LIBRARY_NAME)_CFLAGS += $(CFLAGS) $(COMMON_FLAGS)
$(LIBRARY_NAME)_CXXFLAGS += $(CXXFLAGS) $(COMMON_FLAGS)
${LIBRARY_NAME}_FILES = $(SOURCES_CXX) $(SOURCES_C) $(SOURCES_ASM)
${LIBRARY_NAME}_FRAMEWORKS = OpenGLES
${LIBRARY_NAME}_LIBRARIES = z
include $(THEOS_MAKE_PATH)/library.mk
else
all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(GL_LIB)

%.o: %.S
	$(CC_AS) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@


clean:
	rm -f $(OBJECTS) $(TARGET) $(OBJECTS:.o=.d)

.PHONY: clean
-include $(OBJECTS:.o=.d)
endif
