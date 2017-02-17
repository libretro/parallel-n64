DEBUG=0
PERF_TEST=0
HAVE_SHARED_CONTEXT=0
WITH_CRC=brumme
FORCE_GLES=0
HAVE_OPENGL=1
HAVE_VULKAN_DEBUG=0
GLIDEN64=0
GLIDEN64CORE=0
GLIDEN64ES=0
HAVE_RSP_DUMP=0
HAVE_RDP_DUMP=0
HAVE_RICE=1
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0

DYNAFLAGS :=
INCFLAGS  :=
COREFLAGS :=
CPUFLAGS  :=
GLFLAGS   :=

UNAME=$(shell uname -a)

ifeq ($(HAVE_VULKAN_DEBUG),1)
HAVE_RSP_DUMP=1
HAVE_RDP_DUMP=1
endif

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
else ifneq (,$(findstring armv,$(platform)))
   override platform += unix
else ifneq (,$(findstring rpi,$(platform)))
   override platform += unix
else ifneq (,$(findstring odroid,$(platform)))
   override platform += unix
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
else ifeq ($(ARCH), $(filter $(ARCH), arm))
   WITH_DYNAREC = arm
endif

ifeq ($(HAVE_VULKAN_DEBUG),1)
TARGET_NAME := parallel_n64_debug
else
TARGET_NAME := parallel_n64 
endif

CC_AS ?= $(CC)

GIT_VERSION ?= " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

# Unix
ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--no-undefined
	ifeq ($(DEBUG_JIT),)
	   LDFLAGS += -Wl,--version-script=$(LIBRETRO_DIR)/link.T
	endif
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

   # Raspberry Pi
   ifneq (,$(findstring rpi,$(platform)))
      GLES = 1
      GL_LIB := -L/opt/vc/lib -lGLESv2
      INCFLAGS += -I/opt/vc/include
      WITH_DYNAREC=arm
      ifneq (,$(findstring rpi2,$(platform)))
         CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
         CPUFLAGS += -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
         HAVE_NEON = 1
      else ifneq (,$(findstring rpi3,$(platform)))
         CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
         CPUFLAGS += -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
         HAVE_NEON = 1
      else
         CPUFLAGS += -DARMv5_ONLY -DNO_ASM
      endif
   endif

   # ODROIDs
   ifneq (,$(findstring odroid,$(platform)))
      BOARD := $(shell cat /proc/cpuinfo | grep -i odroid | awk '{print $$3}')
      GLES = 1
      GL_LIB := -lGLESv2
      CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
      CPUFLAGS += -marm -mfloat-abi=hard
      HAVE_NEON = 1
      WITH_DYNAREC=arm
      ifneq (,$(findstring ODROIDC,$(BOARD)))
         # ODROID-C1
         CPUFLAGS += -mcpu=cortex-a5 -mfpu=neon
      else ifneq (,$(findstring ODROID-XU3,$(BOARD)))
         # ODROID-XU4, XU3 & XU3 Lite
         ifeq "$(shell expr `gcc -dumpversion` \>= 4.9)" "1"
            CPUFLAGS += -march=armv7ve -mcpu=cortex-a15.cortex-a7 -mfpu=neon-vfpv4
         else
            CPUFLAGS += -mcpu=cortex-a9 -mfpu=neon
         endif
      else
         # ODROID-U3, U2, X2 & X
         CPUFLAGS += -mcpu=cortex-a9 -mfpu=neon
      endif
   endif

   # Generic ARM
   ifneq (,$(findstring armv,$(platform)))
      CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -DNOSSE
      WITH_DYNAREC=arm
      ifneq (,$(findstring neon,$(platform)))
         CPUFLAGS += -D__NEON_OPT -mfpu=neon
         HAVE_NEON = 1
      endif
   endif

   PLATFORM_EXT := unix

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
   ifneq ($(platform),ios9)
      WITH_DYNAREC=arm
   endif
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
   TARGET := $(TARGET_NAME)_libretro_$(platform).so
   LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T -Wl,--no-undefined -Wl,--warn-common
   GL_LIB := -lGLESv2

   CC = qcc -Vgcc_ntoarmv7le
   CC_AS = qcc -Vgcc_ntoarmv7le
   CXX = QCC -Vgcc_ntoarmv7le
   AR = QCC -Vgcc_ntoarmv7le
   WITH_DYNAREC=arm
   GLES = 1
   PLATCFLAGS += -DNO_ASM -D__BLACKBERRY_QNX__
   CPUFLAGS += -DNOSSE
   HAVE_NEON = 1
   CPUFLAGS += -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
   CFLAGS += -D__QNX__

   PLATFORM_EXT := unix

# emscripten
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_$(platform).bc
   GLES := 1
   WITH_DYNAREC :=

   CPUFLAGS += -DNOSSE
   CPUFLAGS += -DEMSCRIPTEN -DNO_ASM -s USE_ZLIB=1
   PLATCFLAGS += \
      -Dsinc_resampler=mupen_sinc_resampler \
      -DCC_resampler=mupen_CC_resampler \
      -Drglgen_symbol_map=mupen_rglgen_symbol_map \
      -Drglgen_resolve_symbols_custom=mupen_rglgen_resolve_symbols_custom \
      -Drglgen_resolve_symbols=mupen_rglgen_resolve_symbols \
      -Dmemalign_alloc=mupen_memalign_alloc \
      -Dmemalign_free=mupen_memalign_free \
      -Dmemalign_alloc_aligned=mupen_memalign_alloc_aligned \
      -Daudio_resampler_driver_find_handle=mupen_audio_resampler_driver_find_handle \
      -Daudio_resampler_driver_find_ident=mupen_audio_resampler_driver_find_ident \
      -Drarch_resampler_realloc=mupen_rarch_resampler_realloc \
      -Dconvert_float_to_s16_C=mupen_convert_float_to_s16_C \
      -Dconvert_float_to_s16_init_simd=mupen_convert_float_to_s16_init_simd \
      -Dconvert_s16_to_float_C=mupen_convert_s16_to_float_C \
      -Dconvert_s16_to_float_init_simd=mupen_convert_s16_to_float_init_simd \
      -Dcpu_features_get_perf_counter=mupen_cpu_features_get_perf_counter \
      -Dcpu_features_get_time_usec=mupen_cpu_features_get_time_usec \
      -Dcpu_features_get_core_amount=mupen_cpu_features_get_core_amount \
      -Dcpu_features_get=mupen_cpu_features_get \
      -Dffs=mupen_ffs \
      -Dstrlcpy_retro__=mupen_strlcpy_retro__ \
      -Dstrlcat_retro__=mupen_strlcat_retro__


   WITH_DYNAREC =
	CC = emcc
   CXX = em++
   HAVE_NEON = 0
   PLATFORM_EXT := unix
	STATIC_LINKING=1
   #HAVE_SHARED_CONTEXT := 1

# PlayStation Vita
else ifneq (,$(findstring vita,$(platform)))
   TARGET:= $(TARGET_NAME)_libretro_$(platform).a
   CPUFLAGS += -DNO_ASM  -DARM -D__arm__ -DARM_ASM -D__NEON_OPT
   CPUFLAGS += -w -mthumb -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -D__arm__ -DARM_ASM -D__NEON_OPT -g
   HAVE_NEON = 1

   PREFIX = arm-vita-eabi
   CC = $(PREFIX)-gcc
   CXX = $(PREFIX)-g++
   AR  = $(PREFIX)-ar
   WITH_DYNAREC = arm
   DYNAREC_USED = 0
   GLES = 0
   HAVE_OPENGL = 0
   PLATCFLAGS += -DVITA
   CPUCFLAGS += -DNO_ASM
   CFLAGS += -DVITA -lm
   VITA = 1
   SOURCES_C += $(CORE_DIR)/src/r4300/empty_dynarec.c

   PLATFORM_EXT := unix
   STATIC_LINKING=1

# Windows
else ifneq (,$(findstring win,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dll
   LDFLAGS += -shared -static-libgcc -static-libstdc++ -Wl,--version-script=$(LIBRETRO_DIR)/link.T -lwinmm -lgdi32
   GL_LIB := -lopengl32
   PLATFORM_EXT := win32
   CC = gcc
   CXX = g++

endif

ifneq ($(SANITIZER),)
    CFLAGS   += -fsanitize=$(SANITIZER)
    CXXFLAGS += -fsanitize=$(SANITIZER)
    LDFLAGS  += -fsanitize=$(SANITIZER)
endif

ifeq ($(HAVE_OPENGL),0)
	HAVE_GLIDE64=0
	HAVE_GLN64=0
	HAVE_GLIDEN64=0
	HAVE_RICE=0
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

COREFLAGS += -D__LIBRETRO__ -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DSINC_LOWER_QUALITY


ifeq ($(DEBUG), 1)
   CPUOPTS += -O0 -g
   CPUOPTS += -DOPENGL_DEBUG
else
	CPUOPTS += -O2 -g -DNDEBUG
endif

ifeq ($(platform), qnx)
   CFLAGS   += -Wp,-MMD
   CXXFLAGS += -Wp,-MMD
else
ifeq ($(platform), emscripten)
   CFLAGS   += -std=gnu99 -MMD
else
   CFLAGS   += -std=gnu89 -MMD
endif
ifeq ($(GLIDEN64),1)
   CFLAGS   += -DGLIDEN64
   CXXFLAGS += -DGLIDEN64
   CXXFLAGS += -std=c++0x -MMD
else
	CXXFLAGS += -std=gnu++11 -MMD
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
OBJECTS     += $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o) $(SOURCES_ASM:.S=.o)
CXXFLAGS    += $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)
CFLAGS      += $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(PLATCFLAGS) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)

ifeq ($(findstring Haiku,$(UNAME)),)
   LDFLAGS += -lm
endif

LDFLAGS    += $(fpic)

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
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(GL_LIB)
endif

%.o: %.S
	$(CC_AS) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@


clean:
	rm -f $(OBJECTS) $(TARGET) $(OBJECTS:.o=.d)

.PHONY: clean
-include $(OBJECTS:.o=.d)
endif
