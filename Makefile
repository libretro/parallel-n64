DEBUG=0
PERF_TEST=0
HAVE_SHARED_CONTEXT=0
WITH_CRC=brumme
FORCE_GLES=0
HAVE_OPENGL=1
HAVE_VULKAN_DEBUG=0
GLIDEN64=0
GLIDEN64ES=0
HAVE_RSP_DUMP=0
HAVE_RDP_DUMP=0
HAVE_GLIDE64=1
HAVE_GLIDEN64=1
HAVE_GLN64=1
HAVE_RICE=1
HAVE_PARALLEL?=0
HAVE_PARALLEL_RSP?=0
HAVE_THR_AL?=1
STATIC_LINKING=0
HAVE_LTCG ?= 0

ifeq ($(ECHO),1)
    E := 1
endif

DYNAFLAGS :=
INCFLAGS  :=
COREFLAGS :=
CPUFLAGS  :=
GLFLAGS   :=

UNAME=$(shell uname -a)

SPACE :=
SPACE := $(SPACE) $(SPACE)
BACKSLASH :=
BACKSLASH := \$(BACKSLASH)
filter_out1 = $(filter-out $(firstword $1),$1)
filter_out2 = $(call filter_out1,$(call filter_out1,$1))

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
else ifneq (,$(findstring webos,$(platform)))
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
	ifeq ($(shell uname -p),arm)
		arch = arm
	endif
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
else ifeq ($(ARCH), $(filter $(ARCH), aarch64 arm64))
   WITH_DYNAREC = aarch64
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

   HAVE_THR_AL=1
   LDFLAGS += -lpthread
   # clock_gettime (libretro-common features_cpu.c) lives in librt on
   # pre-2.17 glibc and some cross toolchains; harmless on modern glibc
   # where it is in libc (the linker drops the unused -lrt).
   LDFLAGS += -lrt
   # dlopen (GLideN64's GraphicBuffer loader) lives in libdl before glibc
   # 2.34; the link is --no-undefined, so it has to be named.
   LDFLAGS += -ldl



   ifeq ($(FORCE_GLES),1)
      GLES = 1
   else ifneq (,$(findstring gles,$(platform)))
      GLES = 1
   endif

   # Raspberry Pi
   ifneq (,$(findstring rpi,$(platform)))
      GLES = 1
      WITH_DYNAREC=arm
      CPUFLAGS += -DARM_FIX

      ifeq (,$(findstring mesa,$(platform)))
         INCFLAGS += -I/opt/vc/include -I/opt/vc/include/interface/vcos -I/opt/vc/include/interface/vcos/pthreads
      endif

      ifneq (,$(findstring rpi2,$(platform)))
         CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
         CPUFLAGS += -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
         HAVE_NEON = 1
      else ifneq (,$(findstring rpi3,$(platform)))
         CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE
         CPUFLAGS += -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
         HAVE_NEON = 1
      # Raspberry pi 4 in 64bit mode with VULKAN
      else ifneq (,$(findstring rpi4_64,$(platform)))
         CPUFLAGS += -DNO_ASM -DARM -DARM_ASM -DDONT_WANT_ARM_OPTIMIZATIONS -DARM_FIX -DCLASSIC -DARM64
         CPUFLAGS += -march=armv8-a+crc+simd -mtune=cortex-a72
         HAVE_PARALLEL = 1
         WITH_DYNAREC = aarch64
         HAVE_NEON = 0
         HAVE_OPENGL = 0
         GLES = 0
      else
         CPUFLAGS += -DARMv5_ONLY -DNO_ASM
      endif

   endif
   
   # ODROIDs
   ifneq (,$(findstring odroid,$(platform)))
      BOARD ?= $(shell cat /proc/cpuinfo | grep -i odroid | awk '{print $$3}')
      GLES = 1
      CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE -DARM_FIX
      CPUFLAGS += -marm -mfloat-abi=hard
      HAVE_NEON = 1
      WITH_DYNAREC=arm
      ifneq (,$(findstring ODROIDC,$(BOARD)))
         # ODROID-C1
         CPUFLAGS += -mcpu=cortex-a5 -mfpu=neon
      else ifneq (,$(findstring ODROID-XU,$(BOARD)))
         # ODROID-XU3 XU4 and XU3-Lite
         CPUFLAGS += -mcpu=cortex-a15 -mtune=cortex-a15.cortex-a7 -mfpu=neon-vfpv4
      else
         # ODROID-U3, U2, X2 & X
          CPUFLAGS += -mcpu=cortex-a9 -mfpu=neon
      endif
   endif
   
   # webOS
   ifneq (,$(findstring webos,$(platform)))
      GLES = 1
      GL_LIB := -lGLESv2
      ifneq (,$(findstring aarch64,$(CROSS_COMPILE)))
         CPUFLAGS += -DARM -DDONT_WANT_ARM_OPTIMIZATIONS -DARM64
         WITH_DYNAREC=aarch64
         HAVE_NEON = 0
      else
         CPUFLAGS += -DARM -D__arm__ -D__NEON_OPT -DNOSSE
         CPUFLAGS += -marm -mfloat-abi=softfp
         CPUFLAGS += -mcpu=cortex-a9 -mfpu=neon
         WITH_DYNAREC=arm
         HAVE_NEON = 1
      endif
      LDFLAGS += -static-libgcc -static-libstdc++
   endif

   # Classic Platforms ####################
   # Platform affix = classic_<ISA>_<µARCH>
   # Help at https://modmyclassic.com/comp

   # (armv7 a7, hard point, neon based) ### 
   # NESC, SNESC, C64 mini 
   ifneq (,$(findstring classic_armv7_a7, $(platform)))
      GLES = 1
      HAVE_NEON = 1
      WITH_DYNAREC=arm
      ASFLAGS += -D__ARM_NEON__ -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
      CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE -DARM_FIX -DCLASSIC
      CPUFLAGS += -Ofast \
      -flto=4 -fwhole-program -fuse-linker-plugin \
      -fdata-sections -ffunction-sections -Wl,--gc-sections \
      -fno-stack-protector -fno-ident -fomit-frame-pointer \
      -falign-functions=1 -falign-jumps=1 -falign-loops=1 \
      -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-unroll-loops \
      -fmerge-all-constants -fno-math-errno \
      -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
      LDFLAGS += -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
      CPUFLAGS += -march=armv7ve
      # If gcc is 5.0 or later
      ifeq ($(shell echo `$(CC) -dumpversion` ">= 5" | bc -l), 1)
         LDFLAGS += -static-libgcc -static-libstdc++
      endif
   endif  
   
   
   # (armv8 a35, hard point, neon based) ###
   # PlayStation Classic
   ifneq (,$(findstring classic_armv8_a35, $(platform)))
      GLES = 1
      HAVE_NEON = 1
      WITH_DYNAREC=arm
      ASFLAGS += -D__ARM_NEON__ -marm -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard
      CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -D__NEON_OPT -DNOSSE -DARM_FIX -DCLASSIC
      CPUFLAGS += -Ofast \
      -flto -fwhole-program -fuse-linker-plugin \
      -fdata-sections -ffunction-sections -Wl,--gc-sections \
      -fno-stack-protector -fno-ident -fomit-frame-pointer \
      -falign-functions=1 -falign-jumps=1 -falign-loops=1 \
      -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-unroll-loops \
      -fmerge-all-constants -fno-math-errno \
      -marm -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard
      LDFLAGS += -marm -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard
      CPUFLAGS += -march=armv8-a
      LDFLAGS += -static-libgcc -static-libstdc++
   endif

   #######################################
   # Generic ARMV8 - cross - No GL 
   ifneq (,$(findstring armv8,$(platform)))
      CC = aarch64-linux-gnu-gcc
      CXX = aarch64-linux-gnu-g++
      CPUFLAGS += -DNO_ASM -DARM -DARM_ASM -DDONT_WANT_ARM_OPTIMIZATIONS -DARM_FIX -DCLASSIC -DARM64
      LDFLAGS += -static-libgcc -static-libstdc++
      GLES = 0
      HAVE_NEON = 0
      WITH_DYNAREC=aarch64
      ifneq (,$(findstring neon,$(platform)))
         CPUFLAGS += -D__NEON_OPT -mfpu=neon
         HAVE_NEON = 1
      endif
   endif

   #######################################
   # Generic ARM
   ifneq (,$(findstring armv,$(platform)))
      CPUFLAGS += -DNO_ASM -DARM -D__arm__ -DARM_ASM -DNOSSE -DARM_FIX
      WITH_DYNAREC=arm
      ifneq (,$(findstring neon,$(platform)))
         CPUFLAGS += -D__NEON_OPT -mfpu=neon
         HAVE_NEON = 1
      endif
      PLATFORM_EXT := unix
   endif

# i.MX6
else ifneq (,$(findstring imx6,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=$(LIBRETRO_DIR)/link.T
   fpic = -fPIC
   GLES = 1
   CPUFLAGS += -DNO_ASM
   PLATFORM_EXT := unix
   WITH_DYNAREC=arm
   HAVE_NEON=1

# OS X
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   LDFLAGS += -dynamiclib
   MINVERSION := -mmacosx-version-min=10.7
   OSXVER = `sw_vers -productVersion | cut -d. -f 2`
   OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
   LDFLAGS += $(MINVERSION)
   LDFLAGS += -stdlib=libc++
   fpic = -fPIC

   PLATCFLAGS += -D__MACOSX__ -DOSX
   PLATFORM_EXT := unix
   PLATCFLAGS += -DHAVE_POSIX_MEMALIGN

	COREFLAGS += -DOS_MAC_OS_X -stdlib=libc++

   # Target Dynarec
   ifeq ($(ARCH), $(filter $(ARCH), ppc))
      WITH_DYNAREC =
   endif

   # Native Apple Silicon: uname -m reports arm64, the dynarec backend is
   # the same aarch64 one the cross-compile path uses. NO_ASM must stay
   # off - it compiles out dynarec_setup_code() in r4300.c, which would
   # leave the dynarec selectable but broken.
   ifeq ($(arch), arm)
		CFLAGS +=   -DDONT_WANT_ARM_OPTIMIZATIONS -D__NEON_OPT -DARM_FIX
		CPPFLAGS += -DDONT_WANT_ARM_OPTIMIZATIONS -D__NEON_OPT -DARM_FIX
		CXXFLAGS += -DDONT_WANT_ARM_OPTIMIZATIONS -D__NEON_OPT -DARM_FIX
		HAVE_NEON=1
		MINVERSION := -mmacosx-version-min=11.0
		WITH_DYNAREC = aarch64
   endif

   ifeq ($(CROSS_COMPILE),1)
		ifneq (,$(findstring arm,$(LIBRETRO_APPLE_PLATFORM)))
			CFLAGS +=   -DDONT_WANT_ARM_OPTIMIZATIONS -D__NEON_OPT -DARM_FIX
			CPPFLAGS += -DDONT_WANT_ARM_OPTIMIZATIONS -D__NEON_OPT -DARM_FIX
			CXXFLAGS += -DDONT_WANT_ARM_OPTIMIZATIONS -D__NEON_OPT -DARM_FIX
			CPUFLAGS += -fno-stack-protector -fomit-frame-pointer
			HAVE_NEON=1
			MINVERSION := -mmacosx-version-min=11.0
			WITH_DYNAREC = aarch64
		endif

		TARGET_RULE   = -target $(LIBRETRO_APPLE_PLATFORM) -isysroot $(LIBRETRO_APPLE_ISYSROOT)
		CFLAGS   += $(TARGET_RULE)
		CPPFLAGS += $(TARGET_RULE)
		CXXFLAGS += $(TARGET_RULE)
		LDFLAGS  += $(TARGET_RULE)
   endif

	CFLAGS  += $(ARCHFLAGS)
	CXXFLAGS  += $(ARCHFLAGS)
	LDFLAGS += $(ARCHFLAGS)
	CC_AS = perl ./tools/gas-preprocessor-new.pl -arch arm64 -- $(CC)
	# The .S linkage object (linkage_aarch64.S) is assembled via CC_AS, which
	# otherwise inherits no deployment target and gets stamped with the SDK
	# default (e.g. macOS 13.0), producing a "built for newer macOS version than
	# being linked" warning against the 11.0 link. Pass MINVERSION through
	# ASFLAGS so the object's minimum-OS load command matches the rest of the build.
	ASFLAGS += $(MINVERSION)
	# Same reasoning for the C/C++ objects: with -flto -fwhole-program the final
	# code generation happens at link time from the bitcode, and its deployment
	# target comes from the per-TU compile flags, not LDFLAGS. Without MINVERSION
	# in CFLAGS/CXXFLAGS the LTO intermediate (/tmp/lto.o) defaults to the SDK
	# version and triggers the same "newer macOS version" warning. MINVERSION is
	# final here (after the arch reassignment above), so add it to both.
	CFLAGS   += $(MINVERSION)
	CXXFLAGS += $(MINVERSION)
# iOS
else ifneq (,$(findstring ios,$(platform)))
   ifeq ($(IOSSDK),)
      IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
   endif

   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   DEFINES += -DIOS
   HAVE_OPENGL=0
   WITH_DYNAREC=
   PLATFORM_EXT := unix
   MINVERSION :=

   HAVE_PARALLEL=0
   PLATCFLAGS += -DHAVE_POSIX_MEMALIGN -DNO_ASM
   PLATCFLAGS += -DIOS
   CPUFLAGS += -DNO_ASM  -DARM -D__arm__ -DARM_ASM -D__NEON_OPT
   LDFLAGS += -dynamiclib

   fpic = -fPIC
   ifeq ($(platform),ios-arm64)
      CC = clang -arch arm64 -isysroot $(IOSSDK)
      CXX = clang++ -arch arm64 -isysroot $(IOSSDK)
      CFLAGS += -DDONT_WANT_ARM_OPTIMIZATIONS
      CXXFLAGS += -Wc++11-extensions -std=c++11 -stdlib=libc++ -Wc++11-long-long
      HAVE_NEON=0
   else
      # -mcpu=cortex-a8 is only valid for a pure 32-bit armv7 invocation.
      # clang rejects it when arm64 is also in the target arch list (e.g. the
      # fat armv7+arm64 ios10 super-script build, which sets both -arch flags
      # in CC), so only emit it when arm64 is not among the target arches.
      CPUFLAGS += -mfpu=neon -mfloat-abi=softfp
      ifeq (,$(findstring arm64,$(CC)))
         CPUFLAGS += -marm -mcpu=cortex-a8
         CC = clang -arch armv7 -isysroot $(IOSSDK)
         CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
      endif
      PLATCFLAGS += -marm
      HAVE_NEON=1
   endif
   CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
ifeq ($(platform),$(filter $(platform),ios9 ios-arm64))
      MINVERSION = -miphoneos-version-min=8.0
   else
      MINVERSION = -miphoneos-version-min=5.0
   endif
   PLATCFLAGS += $(MINVERSION)
   LDFLAGS += $(MINVERSION)

else ifeq ($(platform), tvos-arm64)
   ifeq ($(IOSSDK),)
      IOSSDK := $(shell xcodebuild -version -sdk appletvos Path)
   endif

   TARGET := $(TARGET_NAME)_libretro_tvos.dylib
   DEFINES += -DIOS
   WITH_DYNAREC=
   PLATFORM_EXT := unix
   MINVERSION :=

   HAVE_PARALLEL=0
   PLATCFLAGS += -DHAVE_POSIX_MEMALIGN -DNO_ASM
   PLATCFLAGS += -DIOS -marm
   CPUFLAGS += -DNO_ASM  -DARM -D__arm__ -DARM_ASM -D__NEON_OPT
   LDFLAGS += -dynamiclib

   fpic = -fPIC
   HAVE_OPENGL=0
      CC = cc -arch arm64 -isysroot $(IOSSDK)
      CXX = c++ -arch arm64 -isysroot $(IOSSDK)
      CFLAGS += -DDONT_WANT_ARM_OPTIMIZATIONS
      FORCE_GLES=1
      CXXFLAGS += -Wc++11-extensions -std=c++11 -stdlib=libc++ -Wc++11-long-long
      CPUFLAGS += -marm -mfpu=neon -mfloat-abi=softfp
      HAVE_NEON=0
   CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
   MINVERSION = -miphoneos-version-min=8.0
   PLATCFLAGS += $(MINVERSION)

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
   HAVE_OPENGL=0
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

   HAVE_PARALLEL = 0
   HAVE_THR_AL = 1
   CPUFLAGS += -DNOSSE -DEMSCRIPTEN -DNO_ASM

   WITH_DYNAREC =
   CC = emcc
   CXX = em++
   HAVE_NEON = 0
   PLATFORM_EXT := unix
   STATIC_LINKING = 1
   SOURCES_C += $(CORE_DIR)/src/device/r4300/empty_dynarec.c

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
   HAVE_PARALLEL=0
   SOURCES_C += $(CORE_DIR)/src/device/r4300/empty_dynarec.c

   PLATFORM_EXT := unix
   STATIC_LINKING=1

# Windows MSVC 2017 all architectures
else ifneq (,$(findstring windows_msvc2017,$(platform)))

	PlatformSuffix = $(subst windows_msvc2017_,,$(platform))
	ifneq (,$(findstring desktop,$(PlatformSuffix)))
		WinPartition = desktop
        MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP
		LDFLAGS += -MANIFEST -LTCG:incremental -NXCOMPAT -DYNAMICBASE -DEBUG -OPT:REF -INCREMENTAL:NO -SUBSYSTEM:WINDOWS -MANIFESTUAC:"level='asInvoker' uiAccess='false'" -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1
		LIBS += kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib
	else ifneq (,$(findstring uwp,$(PlatformSuffix)))
		WinPartition = uwp
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_APP -D_WINDLL -D_UNICODE -DUNICODE -D__WRL_NO_DEFAULT_LIB__ -EHsc
		LDFLAGS += -APPCONTAINER -NXCOMPAT -DYNAMICBASE -MANIFEST:NO -LTCG -OPT:REF -SUBSYSTEM:CONSOLE -MANIFESTUAC:NO -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1 -DEBUG:FULL -WINMD:NO
		LIBS += WindowsApp.lib

        HAVE_OPENGL = 0
	endif

    WITH_DYNAREC :=
    SOURCES_C += $(CORE_DIR)/src/device/r4300/empty_dynarec.c

	CFLAGS += $(MSVC2017CompileFlags)
	CXXFLAGS += $(MSVC2017CompileFlags)

	TargetArchMoniker = $(subst $(WinPartition)_,,$(PlatformSuffix))

	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE
	ASFLAGS += -f win64
	HAVE_PARALLEL=0
	HAVE_PARALLEL_RSP=0
	HAVE_THR_AL=1

	reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
	fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))

	ProgramFiles86w := $(shell cmd //c "echo %PROGRAMFILES(x86)%")
	ProgramFiles86 := $(shell cygpath "$(ProgramFiles86w)")

	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir := $(WindowsSdkDir)

	WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
	WindowsSDKVersion := $(WindowsSDKVersion)

	VsInstallBuildTools = $(ProgramFiles86)/Microsoft Visual Studio/2017/BuildTools
	VsInstallEnterprise = $(ProgramFiles86)/Microsoft Visual Studio/2017/Enterprise
	VsInstallProfessional = $(ProgramFiles86)/Microsoft Visual Studio/2017/Professional
	VsInstallCommunity = $(ProgramFiles86)/Microsoft Visual Studio/2017/Community

	VsInstallRoot ?= $(shell if [ -d "$(VsInstallBuildTools)" ]; then echo "$(VsInstallBuildTools)"; fi)
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallEnterprise)" ]; then echo "$(VsInstallEnterprise)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallProfessional)" ]; then echo "$(VsInstallProfessional)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallCommunity)" ]; then echo "$(VsInstallCommunity)"; fi)
	endif
	VsInstallRoot := $(VsInstallRoot)

	VcCompilerToolsVer := $(shell cat "$(VsInstallRoot)/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" | grep -o '[0-9\.]*')
	VcCompilerToolsDir := $(VsInstallRoot)/VC/Tools/MSVC/$(VcCompilerToolsVer)

	WindowsSDKSharedIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\shared")
	WindowsSDKUCRTIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\ucrt")
	WindowsSDKUMIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\um")
	WindowsSDKUCRTLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\ucrt\$(TargetArchMoniker)")
	WindowsSDKUMLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\um\$(TargetArchMoniker)")

	# For some reason the HostX86 compiler doesn't like compiling for x64
	# ("no such file" opening a shared library), and vice-versa.
	# Work around it for now by using the strictly x86 compiler for x86, and x64 for x64.
	# NOTE: What about ARM?
	ifneq (,$(findstring x64,$(TargetArchMoniker)))
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX64
		LDFLAGS += -MACHINE:X64
	else
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX86
		LDFLAGS += -MACHINE:X86
	endif

	PATH := $(shell IFS=$$'\n'; cygpath "$(VCCompilerToolsBinDir)/$(TargetArchMoniker)"):$(PATH)
	PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VsInstallRoot)/Common7/IDE")
	INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/include")
	LIB := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/lib/$(TargetArchMoniker)")
	ifneq (,$(findstring uwp,$(PlatformSuffix)))
		LIB := $(shell IFS=$$'\n'; cygpath -w "$(LIB)/store")
	endif
    
	export INCLUDE := $(INCLUDE);$(WindowsSDKSharedIncludeDir);$(WindowsSDKUCRTIncludeDir);$(WindowsSDKUMIncludeDir)
	export LIB := $(LIB);$(WindowsSDKUCRTLibDir);$(WindowsSDKUMLibDir)
	TARGET := $(TARGET_NAME)_libretro.dll
	PSS_STYLE :=2
	LDFLAGS += -DLL

# Windows MSVC 2013 x86
else ifeq ($(platform), windows_msvc2013_x86)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE

PATH := $(shell IFS=$$'\n'; cygpath "$(VS120COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS120COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS120COMNTOOLS)../../VC/lib")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS120COMNTOOLS)../../VC/include")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -v "KitsRoot81" | grep -o '[A-Z]:\\.*')

WindowsSdkDirInc := $(WindowsSdkDir)Include

INCFLAGS_PLATFORM = -I"$(WindowsSdkDirInc)\um" -I"$(WindowsSdkDirInc)\shared"
export INCLUDE := $(INCLUDE)
export LIB := $(LIB);$(WindowsSdkDir)\Lib;$(WindowsSdkDir)\Lib\winv6.3\um\x86
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
ASFLAGS += -f win32
LDFLAGS += -DLL -MACHINE:X86
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0
WITH_DYNAREC=x86

# Windows MSVC 2013 x64
else ifeq ($(platform), windows_msvc2013_x64)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE

PATH := $(shell IFS=$$'\n'; cygpath "$(VS120COMNTOOLS)../../VC/bin/amd64"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS120COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS120COMNTOOLS)../../VC/lib/amd64")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS120COMNTOOLS)../../VC/include")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -v "KitsRoot81" | grep -o '[A-Z]:\\.*')

WindowsSdkDirInc := $(WindowsSdkDir)Include

INCFLAGS_PLATFORM = -I"$(WindowsSdkDirInc)\um" -I"$(WindowsSdkDirInc)\shared"
export INCLUDE := $(INCLUDE)
export LIB := $(LIB);$(WindowsSdkDir)\Lib;$(WindowsSdkDir)\Lib\winv6.3\um\x64
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
ASFLAGS += -f win64
LDFLAGS += -DLL -MACHINE:X64
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0

# Windows MSVC 2015 x64
else ifeq ($(platform), windows_msvc2015_x64)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE

PATH := $(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../../VC/bin/amd64"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/lib/amd64")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/include")
ASFLAGS += -f win64

reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir := $(WindowsSdkDir)

WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
WindowsSDKVersion := $(WindowsSDKVersion)

export INCLUDE := $(INCLUDE);$(VCINSTALLDIR)INCLUDE;$(VCINSTALLDIR)ATLMFC\INCLUDE;$(WindowsSdkDir)include\$(WindowsSDKVersion)ucrt;$(WindowsSdkDir)include\$(WindowsSDKVersion)shared;$(WindowsSdkDir)include\$(WindowsSDKVersion)um
export LIB := $(LIB);$(VCINSTALLDIR)LIB\amd64;$(VCINSTALLDIR)ATLMFC\LIB\amd64;$(WindowsSdkDir)lib\$(WindowsSDKVersion)ucrt\x64;$(WindowsSdkDir)lib\$(WindowsSDKVersion)um\x64

INCFLAGS_PLATFORM = -I"$(WindowsSDKVersion)um" -I"$(WindowsSDKVersion)shared"
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL -MACHINE:X64
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0

# Windows MSVC 2015 x86
else ifeq ($(platform), windows_msvc2015_x86)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE

PATH := $(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/lib")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/include")
ASFLAGS += -f win32

reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir := $(WindowsSdkDir)

WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
WindowsSDKVersion := $(WindowsSDKVersion)

export INCLUDE := $(INCLUDE);$(VCINSTALLDIR)INCLUDE;$(VCINSTALLDIR)ATLMFC\INCLUDE;$(WindowsSdkDir)include\$(WindowsSDKVersion)ucrt;$(WindowsSdkDir)include\$(WindowsSDKVersion)shared;$(WindowsSdkDir)include\$(WindowsSDKVersion)um
export LIB := $(LIB);$(VCINSTALLDIR)LIB;$(VCINSTALLDIR)ATLMFC\LIB;$(WindowsSdkDir)lib\$(WindowsSDKVersion)ucrt\x86;$(WindowsSdkDir)lib\$(WindowsSDKVersion)um\x86

INCFLAGS_PLATFORM = -I"$(WindowsSDKVersion)um" -I"$(WindowsSDKVersion)shared"
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL -MACHINE:X86
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0

# Windows MSVC 2010 x64
else ifeq ($(platform), windows_msvc2010_x64)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE

PATH := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/bin/amd64"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/lib/amd64")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS100COMNTOOLS)../../VC/include")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.0A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')lib/x64
WindowsSdkDir ?= $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.1A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')lib/x64

WindowsSdkDirInc := $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.0A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')Include
WindowsSdkDirInc ?= $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.1A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')Include


INCFLAGS_PLATFORM = -I"$(WindowsSdkDirInc)"
export INCLUDE := $(INCLUDE)
export LIB := $(LIB);$(WindowsSdkDir)
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
ASFLAGS += -f win64
LDFLAGS += -DLL -MACHINE:X64
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0

# Windows MSVC 2010 x86
else ifeq ($(platform), windows_msvc2010_x86)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE

PATH := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS100COMNTOOLS)../../VC/lib")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS100COMNTOOLS)../../VC/include")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.0A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')lib
WindowsSdkDir ?= $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.1A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')lib

WindowsSdkDirInc := $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.0A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')Include
WindowsSdkDirInc ?= $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.1A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')Include


INCFLAGS_PLATFORM = -I"$(WindowsSdkDirInc)"
export INCLUDE := $(INCLUDE)
export LIB := $(LIB);$(WindowsSdkDir)
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
ASFLAGS += -f win32
LDFLAGS += -DLL -MACHINE:X86
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0

# Windows MSVC 2005 x86
else ifeq ($(platform), windows_msvc2005_x86)
	CC  = cl.exe
	CXX = cl.exe
	CC_AS = nasm.exe
	COREFLAGS += -DOS_WINDOWS -DUNICODE
	HAS_GCC := 0

PATH := $(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../IDE")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS80COMNTOOLS)../../VC/include")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS80COMNTOOLS)../../VC/lib")
BIN := $(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../../VC/bin")

WindowsSdkDir := $(INETSDK)

export INCLUDE := $(INCLUDE);$(INETSDK)/Include;libretro-common/include/compat/msvc
export LIB := $(LIB);$(WindowsSdkDir);$(INETSDK)/Lib
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
ASFLAGS += -f win32
LDFLAGS += -DLL -MACHINE:X86
CFLAGS += -D_CRT_SECURE_NO_DEPRECATE
LIBS =
HAVE_PARALLEL=0
HAVE_PARALLEL_RSP=0
NOSSE=1

# Windows
else ifneq (,$(findstring win,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dll
   LDFLAGS += -shared -static-libgcc -static-libstdc++ -Wl,--version-script=$(LIBRETRO_DIR)/link.T -lwinmm -lgdi32
   PLATFORM_EXT := win32
   CC ?= gcc
   CXX ?= g++
   HAVE_THR_AL=1
	COREFLAGS += -DOS_WINDOWS -DUNICODE

#ifeq ($(WITH_DYNAREC), $(filter $(WITH_DYNAREC), x86_64 x64))
#ifeq ($(HAVE_PARALLEL), 1)
	#HAVE_PARALLEL_RSP=1
   #CPUFLAGS += -D_POSIX_SOURCE
#endif
#endif

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

# A GLES build compiles glsym_es2.c, which references the GLESv2 entry
# points directly (the desktop glsym_gl.c resolves them at runtime), so
# the library must be linked. Only the webOS block set GL_LIB, leaving
# the generic unix/rpi/odroid GLES paths to fail at link with undefined
# references to glUseProgram etc. Default it here for any GLES build
# that has not already chosen a GL library.
ifeq ($(GLES),1)
   ifeq ($(GL_LIB),)
      GL_LIB := -lGLESv2
   endif
endif

# Ari64 new_dynarec on x86/x86_64: assembled with nasm. Enabled on the
# desktop targets where the static code cache and linkage are wired up.
NASM ?= nasm
ifeq (,$(findstring msvc,$(platform)))
ifeq ($(WITH_DYNAREC), $(filter $(WITH_DYNAREC), x86_64 x64))
   ifneq (,$(findstring unix,$(platform)))
      HAVE_ARI64_X64 = 1
      NASMFLAGS = -f elf64
   else ifneq (,$(findstring osx,$(platform)))
      HAVE_ARI64_X64 = 1
      NASMFLAGS = -f macho64 -DLEADING_UNDERSCORE
   else ifneq (,$(findstring win,$(platform)))
      HAVE_ARI64_X64 = 1
      NASMFLAGS = -f win64 -DWIN64
   endif
endif
ifeq ($(WITH_DYNAREC), x86)
   # The 32-bit (i686) ari64 dynarec; HAVE_ARI64_X86=0 builds the
   # interpreters only.
   ifneq (,$(findstring unix,$(platform)))
      HAVE_ARI64_X86 ?= 1
      NASMFLAGS = -f elf32 -DELF_TYPE -DPIC
   else ifneq (,$(findstring win,$(platform)))
      HAVE_ARI64_X86 ?= 1
      NASMFLAGS = -f win32 -DLEADING_UNDERSCORE
   endif
   ifeq ($(HAVE_ARI64_X86), 1)
      # The generated code and linkage only keep 4-byte stack alignment
      # (the pre-SSE i386 ABI); have gcc realign in functions that spill
      # SSE registers instead of assuming 16-byte incoming alignment.
      CPUFLAGS += -mincoming-stack-boundary=2
   endif
endif
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

COREFLAGS += -D__LIBRETRO__ -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES

OBJOUT   = -o 
LINKOUT  = -o 

ifneq (,$(findstring msvc,$(platform)))
	OBJOUT = -Fo
	LINKOUT = -out:
	LD = link.exe
else
	LD = $(CXX)
endif

ifeq ($(DEBUG), 1)
   CPUOPTS += -DOPENGL_DEBUG

   ifneq (,$(findstring msvc,$(platform)))
      ifeq (,$(findstring msvc2005,$(platform)))
      ifeq (,$(findstring msvc2010,$(platform)))
				 # /FS was added in 2013
         CPUOPTS += -FS
      endif
      endif

      ifeq ($(STATIC_LINKING),1)
      CPUOPTS += -Od -MTd -Zi
      else
      CPUOPTS += -Od -MDd -Zi
      endif

      LDFLAGS += -DEBUG
   else
      CPUOPTS += -O0 -g
   endif
else
ifneq (,$(findstring msvc,$(platform)))
   CPUOPTS += -O2
else ifeq ($(platform), emscripten)
   CPUOPTS += -O3
else ifneq ($(SANITIZER),)
	# what a sanitizer finds does not need -Ofast, and -Ofast takes far longer
	CPUOPTS += -O1
else
	CPUOPTS += -Ofast
endif
   CPUOPTS += -DNDEBUG
ifneq ($(findstring Darwin,$(UNAME)),)
   CPUOPTS += -flto
else ifeq ($(findstring msvc,$(platform)),)
ifneq ($(platform), emscripten)
ifneq ($(shell $(CC) -v 2>&1 | grep -c "clang"),1)
   CPUOPTS += -fipa-pta
endif
endif
endif


   ifneq (,$(findstring msvc,$(platform)))
      ifeq (,$(findstring msvc2005,$(platform)))
      ifeq (,$(findstring msvc2010,$(platform)))
				 # /FS was added in 2013
         CPUOPTS += -FS
      endif
      endif

      ifeq ($(STATIC_LINKING),1)
      CPUOPTS += -MT -Zi
      else
      CPUOPTS += -MD -Zi
      endif

      ifeq (,$(findstring msvc2017,$(platform)))
      CPUOPTS += -EHsc -D_CRT_SECURE_NO_WARNINGS -D__LIBRETRO_WIN64__ -D__SSE2__ -DUNICODE -D_UNICODE -D_USRDLL -DWIN32 -D_WINDLL -D_WINDOWS -WX- -Zc:forScope -Zc:wchar_t -Zi -wd4996 -W0 -fp:precise -Gd -GL -Gm- -GS- -Gy -DMSVC2010_EXPORTS -Oi -Ot
      LDFLAGS += -LTCG -DYNAMICBASE -ERRORREPORT:QUEUE -INCREMENTAL:NO -MANIFEST:NO -NXCOMPAT -OPT:ICF -OPT:REF -SUBSYSTEM:WINDOWS,"5.02" -TLBID:1 advapi32.lib comdlg32.lib gdi32.lib kernel32.lib odbc32.lib odbccp32.lib ole32.lib oleaut32.lib shell32.lib user32.lib uuid.lib winspool.lib
      else
      CPUOPTS += -EHsc -D_CRT_SECURE_NO_WARNINGS -D__LIBRETRO_WIN64__ -D__SSE2__ -DUNICODE -D_UNICODE -D_USRDLL -DWIN32 -D_WINDLL -D_WINDOWS -WX- -Zc:forScope -Zc:wchar_t -Zi -wd4996 -W0 -fp:precise -Gd -GL -Gm- -GS- -Gy -DMSVC2010_EXPORTS -Oi -Ot
      LDFLAGS += -LTCG -DYNAMICBASE -ERRORREPORT:QUEUE -INCREMENTAL:NO -NXCOMPAT -OPT:ICF -OPT:REF
      endif
   endif
endif

WANT_CXX11=0

ifeq ($(platform), qnx)
   CFLAGS   += -Wp,-MMD
   CXXFLAGS += -Wp,-MMD
else
ifeq ($(GLIDEN64),1)
   CFLAGS   += -DGLIDEN64
   CXXFLAGS += -DGLIDEN64
   WANT_CXX11=1
   CFLAGS   += -MMD
   CXXFLAGS += -MMD
else ifeq ($(HAVE_PARALLEL), 1)
   WANT_CXX11=1
   ifeq (,$(findstring msvc,$(platform)))
     CFLAGS   += -MMD
     CXXFLAGS += -MMD
   else
     CFLAGS   += -MT
     CXXFLAGS += -MT
   endif
else ifeq (,$(findstring msvc,$(platform)))
    CFLAGS   += -MMD
    ifneq ($(platform),ios-arm64)
        CXXFLAGS += -std=c++11 -MMD
    endif
endif
ifeq ($(GLIDEN64ES),1)
   CFLAGS   += -DGLIDEN64ES
   CXXFLAGS += -DGLIDEN64ES
endif
endif

ifeq ($(HAVE_THR_AL), 1)
WANT_CXX11=1
endif

ifeq ($(WANT_CXX11),1)
ifeq (,$(findstring msvc,$(platform)))
CXXFLAGS += -std=c++11
endif
endif

ifneq (,$(findstring msvc,$(platform)))
CFLAGS += -DINLINE="_inline"
CXXFLAGS += -DINLINE="_inline"
else
CFLAGS += -DINLINE="inline"
CXXFLAGS += -DINLINE="inline"
endif

# Fix for GCC 10, make sure its added to all stages of the compiler
ifneq ($(platform), emscripten)
ifeq "$(shell expr `gcc -dumpversion` \>= 10)" "1"
  CPUFLAGS += -fcommon
endif
CFLAGS += -fcommon
endif

# LTO
ifeq ($(HAVE_LTCG),1)
  CPUFLAGS += -flto
endif

ASFLAGS     := $(ASFLAGS) $(CFLAGS) $(CPUFLAGS)

### Finalize ###
OBJECTS     += $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o) $(SOURCES_ASM:.S=.o)
OBJECTS     += $(SOURCES_NASM:.asm=.o)
CXXFLAGS    += $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(INCFLAGS_PLATFORM) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)
CFLAGS      += -fcommon $(CPUOPTS) $(COREFLAGS) $(INCFLAGS) $(INCFLAGS_PLATFORM) $(fpic) $(PLATCFLAGS) $(CPUFLAGS) $(GLFLAGS) $(DYNAFLAGS)

ifeq ($(findstring Haiku,$(UNAME)),)
ifeq (,$(findstring msvc,$(platform)))
   LDFLAGS += -lm
endif
endif

LDFLAGS    += $(fpic) $(CPUFLAGS)

ifeq ($(platform), theos_ios)
COMMON_FLAGS := -DIOS $(COMMON_DEFINES) $(INCFLAGS) $(INCFLAGS_PLATFORM) -I$(THEOS_INCLUDE_PATH) -Wno-error
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
	$(LD) $(LINKOUT)$@ $(OBJECTS) $(LDFLAGS) $(GL_LIB) $(LIBS)
endif


%.o: %.S
ifneq (,$(findstring msvc,$(platform)))
	$(CC_AS) $(ASFLAGS) -o$@ $<
else
	$(CC_AS) $(ASFLAGS) -c $< $(OBJOUT)$@
endif

# nasm is invoked from the repo root, so it needs an include search path to
# the directory holding the COMMITTED asm_defines_nasm.h that linkage_x64.asm
# %includes (region 14). The -i flag only points nasm at that committed
# header; nothing is generated at build time.
%.o: %.asm
	$(NASM) $(NASMFLAGS) -i$(ASM_DEFINES_DIR)/ -o $@ $<

# region 14: the asm_defines headers (x64/ and x86/asm_defines_{nasm,gas}.h) are
# GENERATED from the shared C struct (new_dynarec_hot_state) but COMMITTED to the
# tree and treated as source. A normal build NEVER regenerates or touches them --
# doing so on every compile needlessly rebuilt the assembly and (on Windows, via
# line-ending conversion) left the working tree permanently dirty. They are also
# marked -text in .gitattributes so git never rewrites their line endings.
#
# They only need refreshing when struct new_dynarec_hot_state (or struct
# r4300_core's dynarec members) changes, via the explicit `make asm-defines-regen`
# target below; the refreshed headers are then committed. Mechanism: asm_defines.c
# bakes each struct offset into its object as a printable "@ASM_DEFINE ..." string;
# strings|awk turns those into the headers. Adopted from mupen64plus-next.
AWK     ?= awk
STRINGS ?= strings
TR      ?= tr
ifeq ($(WITH_DYNAREC), x86)
ASM_DEFINES_DIR := $(CORE_DIR)/src/device/r4300/new_dynarec/x86
ASM_DEFINES_DYNAREC := 1
else
ASM_DEFINES_DIR := $(CORE_DIR)/src/device/r4300/new_dynarec/x64
ASM_DEFINES_DYNAREC := 2
endif

.PHONY: asm-defines-regen
asm-defines-regen:
	$(CC) $(CFLAGS) $(DYNAFLAGS) -DNEW_DYNAREC=$(ASM_DEFINES_DYNAREC) -c $(CORE_DIR)/src/asm_defines/asm_defines.c $(OBJOUT)$(CORE_DIR)/src/asm_defines/asm_defines.o
	$(STRINGS) "$(CORE_DIR)/src/asm_defines/asm_defines.o" | $(TR) -d '\r' | $(AWK) -v dest_dir="$(ASM_DEFINES_DIR)" -f $(CORE_DIR)/tools/gen_asm_defines.awk
	$(RM) $(CORE_DIR)/src/asm_defines/asm_defines.o

mupen64plus-video-gliden64/src/%.o: mupen64plus-video-gliden64/src/%.c
	$(CC) -I$(VIDEODIR_GLIDEN64)/src $(CPPFLAGS) $(CFLAGS) -c $< $(OBJOUT)$@

mupen64plus-video-gliden64/src/%.o: mupen64plus-video-gliden64/src/%.cpp
	$(CXX) -I$(VIDEODIR_GLIDEN64)/src $(CPPFLAGS) $(CXXFLAGS) -c $< $(OBJOUT)$@

# Objects do not otherwise depend on the flags they were built with, so
# toggling one of the diagnostic switches left every object stale and the
# tool silently absent from the build - enabling HAVE_HLE_AUDIT and seeing
# no captures appear, with nothing to say why.  Stamp the switch state and
# have the objects depend on the stamp, so flipping one rebuilds what it
# affects.
BUILD_FLAG_STATE := HLE_AUDIT=$(HAVE_HLE_AUDIT) RSP_DMA_TRACE=$(HAVE_RSP_DMA_TRACE) RDP_DUMP=$(HAVE_RDP_DUMP)

.build-flags: .FORCE
	@if [ ! -f $@ ] || [ "`cat $@`" != "$(BUILD_FLAG_STATE)" ]; then \
		echo "$(BUILD_FLAG_STATE)" > $@; \
	fi

.FORCE:
.PHONY: .FORCE

$(OBJECTS): .build-flags

# angrylion models the RDP's adders and shifters with plain int arithmetic
# that wraps as the hardware's does: with -fwrapv that is defined.
ifeq (,$(findstring msvc,$(platform)))
$(VIDEODIR_ANGRYLION)/%.o: CFLAGS += -fwrapv
endif

%.o: %.c
	@$(if $(E), $(shell echo echo CC $<),)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< $(OBJOUT)$@

%.o: %.cpp
	@$(if $(E), $(shell echo echo CXX $<),)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< $(OBJOUT)$@

ifeq (,$(findstring msvc,$(platform)))
$(PARALLEL_RDP_NOEXCEPT_OBJECTS): CXXFLAGS += -fno-exceptions
endif

clean: clean-tools
	rm -f $(OBJECTS) $(TARGET) $(OBJECTS:.o=.d) .build-flags

clean-tools:
	rm -f test_gliden64_spirv$(EXE_EXT) $(ANGRYLION_TOOLS) lrhost$(EXE_EXT) lrhost_vk$(EXE_EXT) lrhost_gl$(EXE_EXT) $(GLITCH64_TESTS) angrylion_replay$(EXE_EXT) $(ANGRYLION_CHECKS) dd_ipl_hle_test$(EXE_EXT) \
		$(CORE_CHECKS) dynarec_long_block_test$(EXE_EXT) savestate_latch_test$(EXE_EXT) savestate_code_test$(EXE_EXT) cpu_fallback_test$(EXE_EXT) tools/test_prsp_dmem_wrap.o tools/test_granite_util_rthreads.o $(GRANITE_POOL_TEST_OBJS)

# Angrylion benchmark and bit-exactness harnesses (tools/angrylion_*.c),
# linked against the core's own angrylion objects: `make tools` after a
# normal build, or on its own.
ANGRYLION_TOOL_OBJS := $(VIDEODIR_ANGRYLION)/n64video.o \
	$(VIDEODIR_ANGRYLION)/parallel_al.o \
	$(LIBRETRO_COMM_DIR)/rthreads/rthreads.o \
	$(LIBRETRO_COMM_DIR)/rthreads/retro_eventcount.o \
	$(LIBRETRO_COMM_DIR)/features/features_cpu.o \
	$(LIBRETRO_COMM_DIR)/time/rtime.o
ANGRYLION_TOOLS := angrylion_bench$(EXE_EXT) angrylion_verify$(EXE_EXT) \
	angrylion_pool_overhead$(EXE_EXT)

angrylion_%$(EXE_EXT): tools/angrylion_%.c $(ANGRYLION_TOOL_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_TOOL_OBJS) -o $@ -lpthread -lm

# Headless libretro host: loads the built core and a ROM, times retro_run.
ifeq ($(EXE_EXT),.exe)
LRHOST_LIBS :=
else
LRHOST_LIBS := -ldl
endif
lrhost$(EXE_EXT): tools/lrhost.c $(LIBRETRO_COMM_DIR)/features/features_cpu.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -w $< $(LIBRETRO_COMM_DIR)/features/features_cpu.o -o $@ $(LRHOST_LIBS) -lpthread -lm

# The same host with a Vulkan HW context (headless; lavapipe works), for
# running parallel-rdp: make lrhost_vk
lrhost_vk$(EXE_EXT): tools/lrhost.c tools/lrhost_vulkan.h $(LIBRETRO_COMM_DIR)/features/features_cpu.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -DLRHOST_VULKAN -I$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan-headers/include -w $< $(LIBRETRO_COMM_DIR)/features/features_cpu.o -o $@ $(LRHOST_LIBS) -lpthread -lm

# The same host with an OpenGL HW context (headless EGL; Mesa's llvmpipe
# with EGL_PLATFORM=surfaceless works), for the GL renderers: make lrhost_gl
lrhost_gl$(EXE_EXT): tools/lrhost.c tools/lrhost_gl.h $(LIBRETRO_COMM_DIR)/features/features_cpu.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -DLRHOST_GL -w $< $(LIBRETRO_COMM_DIR)/features/features_cpu.o -o $@ $(LRHOST_LIBS) -lEGL -lpthread -lm

# Glitch64 run on the GL the host gives it (headless EGL in the GL lane),
# built from the renderer's own sources, with glsm, as the core is: its LFB
# write, and its texture uploads for desktop GL and for GLES 2
GLITCH64_TEST_SRCS := $(VIDEODIR_GLIDE)/Glitch64/glitchmain.c $(VIDEODIR_GLIDE)/Glitch64/glitch64_combiner.c \
	$(VIDEODIR_GLIDE)/Glitch64/geometry.c $(VIDEODIR_GLIDE)/Glitch64/glitch64_textures.c \
	$(ROOT_DIR)/libretro/glsm/glsm.c $(LIBRETRO_COMM_DIR)/gfx/gl_capabilities.c $(LIBRETRO_COMM_DIR)/glsym/rglgen.c \
	tools/glitch64_test_stubs.c
GLITCH64_TEST_FLAGS := -D__LIBRETRO__ -DINLINE=inline -DM64P_PLUGIN_API -DM64P_CORE_PROTOTYPES -DHAVE_GLIDE64 \
	-I$(VIDEODIR_GLIDE)/Glitch64/inc -I$(ROOT_DIR)/include -I$(ROOT_DIR)/libretro -I$(LIBRETRO_COMM_DIR)/include \
	-I$(CORE_DIR)/src -I$(CORE_DIR)/src/api -I$(CORE_DIR)/custom $(filter -fsanitize=% -O% -g,$(CFLAGS)) -w
GLITCH64_GL_FLAGS := -DHAVE_OPENGL -DENABLE_3POINT
GLITCH64_GLES_FLAGS := -DHAVE_OPENGLES -DHAVE_OPENGLES2 -DDISABLE_3POINT
test_glitch64_lfb_write$(EXE_EXT): tools/test_glitch64_lfb_write.c $(GLITCH64_TEST_SRCS)
	$(CC) $(GLITCH64_TEST_FLAGS) $(GLITCH64_GL_FLAGS) $< $(GLITCH64_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_gl.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGL -lm
test_glitch64_textures$(EXE_EXT): tools/test_glitch64_textures.c $(GLITCH64_TEST_SRCS)
	$(CC) $(GLITCH64_TEST_FLAGS) $(GLITCH64_GL_FLAGS) $< $(GLITCH64_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_gl.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGL -lm
test_glitch64_textures_gles$(EXE_EXT): tools/test_glitch64_textures.c $(GLITCH64_TEST_SRCS)
	$(CC) $(GLITCH64_TEST_FLAGS) $(GLITCH64_GLES_FLAGS) $< $(GLITCH64_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_es2.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGLESv2 -lm
# glsm's draw target: the frontend presents the frame a renderer swapped,
# through the blit (desktop GL) and the texture copy (GLES 2); and the
# state glsm keeps is the state GL holds while the core draws
GLSM_TEST_SRCS := $(ROOT_DIR)/libretro/glsm/glsm.c $(LIBRETRO_COMM_DIR)/gfx/gl_capabilities.c $(LIBRETRO_COMM_DIR)/glsym/rglgen.c
GLSM_TEST_FLAGS := -D__LIBRETRO__ -I$(ROOT_DIR)/libretro -I$(LIBRETRO_COMM_DIR)/include \
	$(filter -fsanitize=% -O% -g,$(CFLAGS)) -w
test_glsm_draw_target$(EXE_EXT): tools/test_glsm_draw_target.c $(GLSM_TEST_SRCS)
	$(CC) $(GLSM_TEST_FLAGS) -DHAVE_OPENGL $< $(GLSM_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_gl.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGL -lm
test_glsm_draw_target_gles$(EXE_EXT): tools/test_glsm_draw_target.c $(GLSM_TEST_SRCS)
	$(CC) $(GLSM_TEST_FLAGS) -DHAVE_OPENGLES -DHAVE_OPENGLES2 $< $(GLSM_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_es2.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGLESv2 -lm

test_glsm_state$(EXE_EXT): tools/test_glsm_state.c $(GLSM_TEST_SRCS)
	$(CC) $(GLSM_TEST_FLAGS) -DHAVE_OPENGL $< $(GLSM_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_gl.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGL -lm
test_glsm_state_gles$(EXE_EXT): tools/test_glsm_state.c $(GLSM_TEST_SRCS)
	$(CC) $(GLSM_TEST_FLAGS) -DHAVE_OPENGLES -DHAVE_OPENGLES2 $< $(GLSM_TEST_SRCS) $(LIBRETRO_COMM_DIR)/glsym/glsym_es2.c \
		-o $@ $(filter -fsanitize=%,$(LDFLAGS)) -lEGL -lGLESv2 -lm

GLITCH64_TESTS := test_glitch64_lfb_write$(EXE_EXT) test_glitch64_textures$(EXE_EXT) test_glitch64_textures_gles$(EXE_EXT) \
	test_glsm_draw_target$(EXE_EXT) test_glsm_draw_target_gles$(EXE_EXT) test_glsm_state$(EXE_EXT) test_glsm_state_gles$(EXE_EXT)

angrylion_replay$(EXE_EXT): tools/angrylion_replay.c $(ANGRYLION_TOOL_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_TOOL_OBJS) -o $@ -lpthread -lm

angrylion_vemit_test$(EXE_EXT): tools/angrylion_vemit_test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION)/n64video -w $< -o $@

angrylion_gen_test$(EXE_EXT): tools/angrylion_gen_test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION)/n64video -w $< -o $@

# HLE geometry frontend: the clipper's output must not depend on its
# scratch vertices' previous contents.
ANGRYLION_EMIT_OBJS := $(VIDEODIR_ANGRYLION)/rdp_emit_frontend.o \
	$(VIDEODIR_ANGRYLION)/rdp_emit_bridge.o \
	$(VIDEODIR_ANGRYLION)/rdp_emit_rsp.o \
	$(VIDEODIR_ANGRYLION)/rdp_emit_f3dtri.o \
	$(VIDEODIR_ANGRYLION)/rdp_emit.o
angrylion_clip_test$(EXE_EXT): tools/angrylion_clip_test.c $(ANGRYLION_EMIT_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_EMIT_OBJS) -o $@ -lm

# Fast3D walker: the near clip plane comes from the microcode data segment.
# The walkers reference the HLE activation glue for mid-list microcode
# swaps; the test satisfies those entry points itself, so the glue and the
# rasterizer stay out of the link.
ANGRYLION_WALKER_OBJS := $(filter-out $(VIDEODIR_ANGRYLION)/rdp_emit_hle.o, \
	$(patsubst %.c,%.o,$(wildcard $(VIDEODIR_ANGRYLION)/rdp_emit*.c))) \
	$(LIBRETRO_COMM_DIR)/encodings/encoding_crc32.o \
	$(LIBRETRO_COMM_DIR)/features/features_cpu.o \
	$(LIBRETRO_COMM_DIR)/time/rtime.o
angrylion_f3d_near_test$(EXE_EXT): tools/angrylion_f3d_near_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

angrylion_f3d_gbi1_test$(EXE_EXT): tools/angrylion_f3d_gbi1_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

angrylion_texrecip_test$(EXE_EXT): tools/angrylion_texrecip_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

angrylion_cbfd_light_test$(EXE_EXT): tools/angrylion_cbfd_light_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

angrylion_rare_clip_test$(EXE_EXT): tools/angrylion_rare_clip_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

angrylion_far_reject_test$(EXE_EXT): tools/angrylion_far_reject_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

# Legacy (GoldenEye / Perfect Dark) lighting: detection and rounding.
angrylion_legacy_light_test$(EXE_EXT): tools/angrylion_legacy_light_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

# S2DEX 1.x object commands: tile history, sprite triangles, RECTANGLE_R,
# SELECT_DL.
angrylion_s2dex1_test$(EXE_EXT): tools/angrylion_s2dex1_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

# Emitter divide unit against the cxd4 interpreter's VRCP/VRSQ pairs.
angrylion_divide_test$(EXE_EXT): tools/angrylion_divide_test.c $(CXD4DIR)/rsp.o $(ANGRYLION_EMIT_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(CXD4DIR)/rsp.o $(ANGRYLION_EMIT_OBJS) -o $@ -lm

# Rogue Squadron walker: the list's own SYNC_FULL is not forwarded.
angrylion_rs_fullsync_test$(EXE_EXT): tools/angrylion_rs_fullsync_test.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

# Fast3D triangle setup, clipper and vertex write (GoldenEye, Perfect Dark,
# Mario Kart 64) against LLE-captured vertex records and RDP words.
angrylion_f3dtri_test$(EXE_EXT): tools/angrylion_f3dtri_test.c tools/angrylion_f3dtri_expected.h $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -Itools -w $< $(ANGRYLION_WALKER_OBJS) -o $@ -lm

# The same vectors through the portable vector-unit lane loops.
angrylion_f3dtri_c_test$(EXE_EXT): tools/angrylion_f3dtri_test.c tools/angrylion_f3dtri_expected.h $(VIDEODIR_ANGRYLION)/rdp_emit_f3dtri.c $(ANGRYLION_WALKER_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DF3D_NO_SSE2 -I$(VIDEODIR_ANGRYLION) -Itools -w $< $(VIDEODIR_ANGRYLION)/rdp_emit_f3dtri.c $(filter-out $(VIDEODIR_ANGRYLION)/rdp_emit_f3dtri.o,$(ANGRYLION_WALKER_OBJS)) -o $@ -lm

# The F3DEX2-family triangle writer on a fixed pseudo-random stream, with
# the SSE2 attribute lanes and with the scalar loop.
angrylion_tri_write_test$(EXE_EXT): tools/angrylion_tri_write_test.c $(VIDEODIR_ANGRYLION)/rdp_emit_rsp.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(VIDEODIR_ANGRYLION) -w $< $(VIDEODIR_ANGRYLION)/rdp_emit_rsp.c -o $@ -lm

angrylion_tri_write_c_test$(EXE_EXT): tools/angrylion_tri_write_test.c $(VIDEODIR_ANGRYLION)/rdp_emit_rsp.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRSP_NO_SSE2 -I$(VIDEODIR_ANGRYLION) -w $< $(VIDEODIR_ANGRYLION)/rdp_emit_rsp.c -o $@ -lm

ANGRYLION_CHECKS := angrylion_vemit_test$(EXE_EXT) angrylion_gen_test$(EXE_EXT) \
	angrylion_clip_test$(EXE_EXT) angrylion_f3d_near_test$(EXE_EXT) \
	angrylion_divide_test$(EXE_EXT) angrylion_rs_fullsync_test$(EXE_EXT) \
	angrylion_far_reject_test$(EXE_EXT) angrylion_f3d_gbi1_test$(EXE_EXT) \
	angrylion_hazard_wrap_test$(EXE_EXT) angrylion_texrecip_test$(EXE_EXT) \
	angrylion_cbfd_light_test$(EXE_EXT) angrylion_rare_clip_test$(EXE_EXT) \
	angrylion_dpc_regs_test$(EXE_EXT) angrylion_noise_frame_test$(EXE_EXT) \
	angrylion_legacy_light_test$(EXE_EXT) angrylion_s2dex1_test$(EXE_EXT) \
	angrylion_f3dtri_test$(EXE_EXT) angrylion_f3dtri_c_test$(EXE_EXT) \
	angrylion_tri_write_test$(EXE_EXT) angrylion_tri_write_c_test$(EXE_EXT)

tools: $(ANGRYLION_TOOLS) lrhost$(EXE_EXT) angrylion_replay$(EXE_EXT) $(ANGRYLION_CHECKS)

# Run the angrylion self-tests: the vector emitter, the generated pipeline
# stage, and the HLE geometry frontend's regression lanes.
check-angrylion: $(ANGRYLION_CHECKS)
	@set -e; for t in $(ANGRYLION_CHECKS); do echo "== $$t"; ./$$t; done

# Built-in 64DD IPL: boot test disks through the core with no IPL ROM and
# check the state their boot code finds against the retail IPL's (for a
# 43-block and an 8-block boot). Needs the core built first.
dd_ipl_hle_test$(EXE_EXT): tools/dd_ipl_hle_test.c tools/dd_ipl_hle_expected.h
	$(CC) $(CHECK_CFLAGS) -Itools $< -o $@

DD_IPL_CHECK_DIR := dd_ipl_check.tmp
check-dd-ipl: $(TARGET) lrhost$(EXE_EXT) dd_ipl_hle_test$(EXE_EXT)
	@set -e; rm -rf $(DD_IPL_CHECK_DIR); mkdir -p $(DD_IPL_CHECK_DIR)/lrhost-system; \
	for n in 43 8; do \
		echo "== $$n-block boot"; \
		./dd_ipl_hle_test$(EXE_EXT) disk $(DD_IPL_CHECK_DIR)/disk_$$n.ndd $$n; \
		( cd $(DD_IPL_CHECK_DIR) && LRHOST_RAMDUMP=ram_$$n.bin ../lrhost$(EXE_EXT) ../$(TARGET) disk_$$n.ndd 4 \
			parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle \
			parallel-n64-cpucore=pure_interpreter > /dev/null ); \
		./dd_ipl_hle_test$(EXE_EXT) check $(DD_IPL_CHECK_DIR)/ram_$$n.bin $$n; \
	done; rm -rf $(DD_IPL_CHECK_DIR)

# A straight-line block longer than ari64's instruction buffer, booted
# through the core with each CPU core. Built with SANITIZER=address,undefined
# it also fails on any memory error or undefined behaviour in the CPU cores.
DYNAREC_BLOCK_CHECK_DIR := dynarec_block_check.tmp

dynarec_long_block_test$(EXE_EXT): tools/dynarec_long_block_test.c
	$(CC) -O1 -g $< -o $@

check-dynarec-block: $(TARGET) lrhost$(EXE_EXT) dynarec_long_block_test$(EXE_EXT)
	@set -e; rm -rf $(DYNAREC_BLOCK_CHECK_DIR); mkdir -p $(DYNAREC_BLOCK_CHECK_DIR)/lrhost-system; \
	./dynarec_long_block_test$(EXE_EXT) rom $(DYNAREC_BLOCK_CHECK_DIR)/long_block.z64; \
	for core in pure_interpreter cached_interpreter dynamic_recompiler dynamic_recompiler_ari64; do \
		echo "== $$core"; \
		( cd $(DYNAREC_BLOCK_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 LRHOST_RAMDUMP=ram_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) long_block.z64 4 \
			parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle \
			parallel-n64-cpucore=$$core > /dev/null ); \
		./dynarec_long_block_test$(EXE_EXT) check $(DYNAREC_BLOCK_CHECK_DIR)/ram_$$core.bin; \
	done; rm -rf $(DYNAREC_BLOCK_CHECK_DIR)

# The COP0 and COP2 move latches across a savestate: a state saved between
# the moves and their read-back is loaded into a fresh boot, with each CPU
# core, and must save back byte for byte. The `saved` step checks the save
# frame lands in that window.
SAVESTATE_LATCH_CHECK_DIR := savestate_latch_check.tmp
SAVESTATE_LATCH_SAVE_AT := 16

savestate_latch_test$(EXE_EXT): tools/savestate_latch_test.c
	$(CC) -O1 -g $< -o $@

check-savestate-latch: $(TARGET) lrhost$(EXE_EXT) savestate_latch_test$(EXE_EXT)
	@set -e; rm -rf $(SAVESTATE_LATCH_CHECK_DIR); mkdir -p $(SAVESTATE_LATCH_CHECK_DIR)/lrhost-system; \
	./savestate_latch_test$(EXE_EXT) rom $(SAVESTATE_LATCH_CHECK_DIR)/latch.z64; \
	for core in pure_interpreter cached_interpreter dynamic_recompiler dynamic_recompiler_ari64; do \
		echo "== $$core"; \
		( cd $(SAVESTATE_LATCH_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 LRHOST_SAVE=state_$$core.bin LRHOST_SAVE_AT=$(SAVESTATE_LATCH_SAVE_AT) \
			LRHOST_RAMDUMP=saved_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) latch.z64 $$(( $(SAVESTATE_LATCH_SAVE_AT) + 1 )) \
			parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle \
			parallel-n64-cpucore=$$core > /dev/null ); \
		./savestate_latch_test$(EXE_EXT) saved $(SAVESTATE_LATCH_CHECK_DIR)/saved_$$core.bin; \
		( cd $(SAVESTATE_LATCH_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 LRHOST_STATE=state_$$core.bin LRHOST_RESAVE=resave_$$core.bin \
			LRHOST_RAMDUMP=ram_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) latch.z64 120 \
			parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle \
			parallel-n64-cpucore=$$core > /dev/null ); \
		cmp $(SAVESTATE_LATCH_CHECK_DIR)/state_$$core.bin $(SAVESTATE_LATCH_CHECK_DIR)/resave_$$core.bin; \
		./savestate_latch_test$(EXE_EXT) check $(SAVESTATE_LATCH_CHECK_DIR)/ram_$$core.bin; \
	done; rm -rf $(SAVESTATE_LATCH_CHECK_DIR)

# Cached code across a state loaded back into the running session, as
# run-ahead and rewind load one: the state is saved before a function the
# payload later rewrites is called, and loaded once the rewritten one has
# run, so every CPU core must invalidate what it compiled from the page.
# A run-ahead pass, saving and loading around every frame, must end as the
# plain run does.
SAVESTATE_CODE_CHECK_DIR := savestate_code_check.tmp
SAVESTATE_CODE_SAVE_AT := 16
SAVESTATE_CODE_LOAD_AT := 100
SAVESTATE_CODE_OPTS := parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle

savestate_code_test$(EXE_EXT): tools/savestate_code_test.c
	$(CC) -O1 -g $< -o $@

check-savestate-code: $(TARGET) lrhost$(EXE_EXT) savestate_code_test$(EXE_EXT)
	@set -e; rm -rf $(SAVESTATE_CODE_CHECK_DIR); mkdir -p $(SAVESTATE_CODE_CHECK_DIR)/lrhost-system; \
	./savestate_code_test$(EXE_EXT) rom $(SAVESTATE_CODE_CHECK_DIR)/code.z64; \
	for core in pure_interpreter cached_interpreter dynamic_recompiler dynamic_recompiler_ari64; do \
		echo "== $$core"; \
		( cd $(SAVESTATE_CODE_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			LRHOST_RAMDUMP=saved_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) code.z64 $$(( $(SAVESTATE_CODE_SAVE_AT) + 1 )) \
			$(SAVESTATE_CODE_OPTS) parallel-n64-cpucore=$$core > /dev/null ); \
		./savestate_code_test$(EXE_EXT) saved $(SAVESTATE_CODE_CHECK_DIR)/saved_$$core.bin; \
		( cd $(SAVESTATE_CODE_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			LRHOST_RAMDUMP=before_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) code.z64 $$(( $(SAVESTATE_CODE_LOAD_AT) + 1 )) \
			$(SAVESTATE_CODE_OPTS) parallel-n64-cpucore=$$core > /dev/null ); \
		./savestate_code_test$(EXE_EXT) check $(SAVESTATE_CODE_CHECK_DIR)/before_$$core.bin; \
		( cd $(SAVESTATE_CODE_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			LRHOST_SAVE=state_$$core.bin LRHOST_SAVE_AT=$(SAVESTATE_CODE_SAVE_AT) LRHOST_LOAD_AT=$(SAVESTATE_CODE_LOAD_AT) \
			LRHOST_RAMDUMP=after_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) code.z64 $$(( $(SAVESTATE_CODE_LOAD_AT) * 2 )) \
			$(SAVESTATE_CODE_OPTS) parallel-n64-cpucore=$$core > /dev/null ); \
		./savestate_code_test$(EXE_EXT) check $(SAVESTATE_CODE_CHECK_DIR)/after_$$core.bin; \
		( cd $(SAVESTATE_CODE_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			LRHOST_RAMDUMP=plain_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) code.z64 $$(( $(SAVESTATE_CODE_LOAD_AT) * 2 )) \
			$(SAVESTATE_CODE_OPTS) parallel-n64-cpucore=$$core > /dev/null ); \
		( cd $(SAVESTATE_CODE_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 LRHOST_RUNAHEAD=1 \
			LRHOST_RAMDUMP=runahead_$$core.bin ../lrhost$(EXE_EXT) ../$(TARGET) code.z64 $$(( $(SAVESTATE_CODE_LOAD_AT) * 2 )) \
			$(SAVESTATE_CODE_OPTS) parallel-n64-cpucore=$$core > /dev/null ); \
		cmp $(SAVESTATE_CODE_CHECK_DIR)/plain_$$core.bin $(SAVESTATE_CODE_CHECK_DIR)/runahead_$$core.bin; \
	done; rm -rf $(SAVESTATE_CODE_CHECK_DIR)

# Instructions the recompilers run through the interpreter (BREAK, the
# COP2 moves, DCFC1/DCTC1): the pure interpreter's exception and value log is
# the reference, and every other CPU core must log the same.
CPU_FALLBACK_CHECK_DIR := cpu_fallback_check.tmp

cpu_fallback_test$(EXE_EXT): tools/cpu_fallback_test.c
	$(CC) -O1 -g $< -o $@

check-cpu-fallback: $(TARGET) lrhost$(EXE_EXT) cpu_fallback_test$(EXE_EXT)
	@set -e; rm -rf $(CPU_FALLBACK_CHECK_DIR); mkdir -p $(CPU_FALLBACK_CHECK_DIR)/lrhost-system; \
	./cpu_fallback_test$(EXE_EXT) rom $(CPU_FALLBACK_CHECK_DIR)/fallback.z64; \
	for core in pure_interpreter cached_interpreter dynamic_recompiler dynamic_recompiler_ari64; do \
		echo "== $$core"; \
		( cd $(CPU_FALLBACK_CHECK_DIR) && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 LRHOST_RAMDUMP=ram_$$core.bin \
			../lrhost$(EXE_EXT) ../$(TARGET) fallback.z64 4 \
			parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=hle \
			parallel-n64-cpucore=$$core > /dev/null ); \
		if [ $$core = pure_interpreter ]; then \
			./cpu_fallback_test$(EXE_EXT) check $(CPU_FALLBACK_CHECK_DIR)/ram_$$core.bin; \
		else \
			./cpu_fallback_test$(EXE_EXT) compare $(CPU_FALLBACK_CHECK_DIR)/ram_pure_interpreter.bin \
				$(CPU_FALLBACK_CHECK_DIR)/ram_$$core.bin; \
		fi; \
	done; rm -rf $(CPU_FALLBACK_CHECK_DIR)

# Core self-tests: MMIO register and DMA bounds, the frontend memory map,
# framebuffer write notifications, COP0 exceptions, PI status, the MusyX
# voice loaders, the BOSS audio arithmetic and, in a HAVE_PARALLEL_RSP=1
# build, the paraLLEl-RSP DMEM wrap.  Each unit under test is compiled into
# its test; sanitizers are used where the toolchain has them.
ifeq ($(EXE_EXT),.exe)
CHECK_SANITIZE ?=
else
CHECK_SANITIZE ?= -fsanitize=address,undefined -fno-sanitize-recover=all
endif
CHECK_CFLAGS  := -O1 -g -w -ffunction-sections -fdata-sections
CHECK_LDFLAGS := -Wl,--gc-sections

CORE_DEVICE_DIR := $(CORE_DIR)/src/device
TEST_RCP_BOUNDS_SRCS := $(CORE_DEVICE_DIR)/rcp/ai/ai_controller.c \
	$(CORE_DEVICE_DIR)/rcp/mi/mi_controller.c \
	$(CORE_DEVICE_DIR)/rcp/pi/pi_controller.c \
	$(CORE_DEVICE_DIR)/rcp/rdp/rdp_core.c \
	$(CORE_DEVICE_DIR)/rcp/ri/ri_controller.c \
	$(CORE_DEVICE_DIR)/rcp/rsp/rsp_core.c \
	$(CORE_DEVICE_DIR)/rcp/si/si_controller.c \
	$(CORE_DEVICE_DIR)/rcp/vi/vi_controller.c \
	$(CORE_DEVICE_DIR)/cart/sram.c \
	$(CORE_DEVICE_DIR)/cart/is_viewer.c \
	$(CORE_DEVICE_DIR)/dd/dd_controller.c \
	$(CORE_DIR)/src/api/callbacks.c
test_rcp_bounds$(EXE_EXT): tools/test_rcp_bounds.c $(TEST_RCP_BOUNDS_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_memory_map$(EXE_EXT): tools/test_memory_map.c $(CORE_DEVICE_DIR)/device_memory_map.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_fb_write$(EXE_EXT): tools/test_fb_write.c $(CORE_DEVICE_DIR)/rcp/rdp/fb.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

TEST_CP0_SRCS := $(CORE_DEVICE_DIR)/r4300/cp0.c $(CORE_DEVICE_DIR)/r4300/interrupt.c
test_cp0_exceptions$(EXE_EXT): tools/test_cp0_exceptions.c $(TEST_CP0_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# -Ofast as the core itself is built, so the compares are checked under the
# same floating-point assumptions
test_fpu_compare$(EXE_EXT): tools/test_fpu_compare.c
	$(CC) $(CHECK_CFLAGS) -Ofast $(CHECK_SANITIZE) -I$(CORE_DIR)/src \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

TEST_R4300_INTERP_SRCS := $(CORE_DEVICE_DIR)/r4300/cp0.c $(CORE_DEVICE_DIR)/r4300/interrupt.c \
	$(CORE_DEVICE_DIR)/r4300/cp1.c $(CORE_DEVICE_DIR)/r4300/idec.c \
	$(LIBRETRO_COMM_DIR)/encodings/encoding_xxh3.c $(LIBRETRO_COMM_DIR)/features/features_cpu.c
test_r4300_interp$(EXE_EXT): tools/test_r4300_interp.c $(CORE_DEVICE_DIR)/r4300/pure_interp.c \
		$(CORE_DEVICE_DIR)/r4300/mips_instructions.def $(TEST_R4300_INTERP_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$< $(TEST_R4300_INTERP_SRCS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

TEST_PI_SRCS := $(CORE_DEVICE_DIR)/rcp/pi/pi_controller.c $(CORE_DEVICE_DIR)/rcp/mi/mi_controller.c
test_pi_status$(EXE_EXT): tools/test_pi_status.c $(TEST_PI_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

TEST_MUSYX_SRCS := $(RSPDIR)/src/hle_memory.c $(RSPDIR)/src/audio.c \
	$(RSPDIR)/src/resample_hq.c
test_musyx_bounds$(EXE_EXT): tools/test_musyx_bounds.c $(RSPDIR)/src/musyx.c $(TEST_MUSYX_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$< $(TEST_MUSYX_SRCS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

test_musyx_aux$(EXE_EXT): tools/test_musyx_aux.c $(RSPDIR)/src/musyx.c $(TEST_MUSYX_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$< $(TEST_MUSYX_SRCS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

test_hle_dispatch$(EXE_EXT): tools/test_hle_dispatch.c $(RSPDIR)/src/hle.c $(RSPDIR)/src/hle_memory.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$< $(RSPDIR)/src/hle_memory.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

test_pif_write$(EXE_EXT): tools/test_pif_write.c $(CORE_DEVICE_DIR)/pif/pif.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

TEST_GB_CART_SRCS := $(CORE_DEVICE_DIR)/gb/gb_cart.c $(CORE_DEVICE_DIR)/gb/mbc3_rtc.c \
	$(CORE_DEVICE_DIR)/gb/m64282fp.c

test_gb_cart_banks$(EXE_EXT): tools/test_gb_cart_banks.c $(TEST_GB_CART_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_dynarec_cache_pages$(EXE_EXT): tools/test_dynarec_cache_pages.c \
		$(CORE_DEVICE_DIR)/r4300/new_dynarec/cache_protect.h
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src \
		$< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_re2_fill$(EXE_EXT): tools/test_re2_fill.c $(RSPDIR)/src/re2.c $(RSPDIR)/src/hle_memory.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$< $(RSPDIR)/src/hle_memory.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_hle_dram_wrap$(EXE_EXT): tools/test_hle_dram_wrap.c $(RSPDIR)/src/hle_memory.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_alist_ops$(EXE_EXT): tools/test_alist_ops.c $(RSPDIR)/src/alist.c $(RSPDIR)/src/alist_nead.c $(TEST_MUSYX_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$< $(TEST_MUSYX_SRCS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

test_alist_abi1$(EXE_EXT): tools/test_alist_abi1.c $(RSPDIR)/src/alist.c $(RSPDIR)/src/alist_audio.c $(TEST_MUSYX_SRCS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(RSPDIR)/src -I$(LIBRETRO_COMM_DIR)/include \
		$< $(TEST_MUSYX_SRCS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

ZBOSS_TEST_OBJS := $(filter-out $(VIDEODIR_ANGRYLION)/rdp_emit_zboss.o,$(ANGRYLION_WALKER_OBJS))
angrylion_zboss_audio_test$(EXE_EXT): tools/angrylion_zboss_audio_test.c \
		$(VIDEODIR_ANGRYLION)/rdp_emit_zboss.c $(ZBOSS_TEST_OBJS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_ANGRYLION) -I$(LIBRETRO_COMM_DIR)/include \
		$< $(ZBOSS_TEST_OBJS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

# The controller backend is #included by the test, which defines the
# frontend globals it reads.
test_mouse_accum$(EXE_EXT): tools/test_mouse_accum.c \
		$(CORE_DIR)/src/plugin/emulate_game_controller_via_libretro.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -I$(CORE_DIR)/src/api \
		-I$(CORE_DIR)/custom -I$(LIBRETRO_COMM_DIR)/include -I$(ROOT_DIR)/libretro \
		$< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

test_temporal_upscaler$(EXE_EXT): tools/test_temporal_upscaler.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp/temporal_upscaler.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp \
		-I$(LIBRETRO_COMM_DIR)/include $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

# The list module and the libretro-common file and CRC code it reads
# and writes the list with.
PIPELINE_RECORDS_TEST_SRC := $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/pipeline_record_list.c \
	$(LIBRETRO_COMM_DIR)/streams/file_stream.c $(LIBRETRO_COMM_DIR)/vfs/vfs_implementation.c \
	$(LIBRETRO_COMM_DIR)/encodings/encoding_crc32.c $(LIBRETRO_COMM_DIR)/string/stdstring.c \
	$(LIBRETRO_COMM_DIR)/compat/compat_strl.c $(LIBRETRO_COMM_DIR)/file/file_path.c \
	$(LIBRETRO_COMM_DIR)/file/file_path_io.c $(LIBRETRO_COMM_DIR)/time/rtime.c \
	$(LIBRETRO_COMM_DIR)/encodings/encoding_utf.c $(LIBRETRO_COMM_DIR)/compat/fopen_utf8.c \
	$(LIBRETRO_COMM_DIR)/features/features_cpu.c
test_pipeline_records$(EXE_EXT): tools/test_pipeline_records.c $(PIPELINE_RECORDS_TEST_SRC)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan \
		-I$(LIBRETRO_COMM_DIR)/include $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# parallel-RDP's tile binning overflow test, at the int32 extremes
test_rdp_x_range$(EXE_EXT): tools/test_rdp_x_range.cpp $(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp/rdp_data_structures.hpp
	$(CXX) -std=c++11 $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp \
		-I$(VIDEODIR_PARALLEL)/parallel-rdp/util $< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's object pools (util/pool.c) and aligned allocation, built as C
# once for the tests that use them
GRANITE_POOL_TEST_OBJS := tools/granite_pool_for_tests.o tools/granite_aligned_for_tests.o
tools/granite_pool_for_tests.o: $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.h
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(LIBRETRO_COMM_DIR)/include -c $< -o $@
tools/granite_aligned_for_tests.o: $(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.h
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(LIBRETRO_COMM_DIR)/include -c $< -o $@

# The slot pools themselves, for odd slot sizes and from several threads
test_granite_pool$(EXE_EXT): tools/test_granite_pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c $(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_PARALLEL)/parallel-rdp/util -I$(LIBRETRO_COMM_DIR)/include \
		$^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's util containers, written without the standard library
test_granite_util$(EXE_EXT): tools/test_granite_util.cpp $(wildcard $(VIDEODIR_PARALLEL)/parallel-rdp/util/*.hpp) \
		$(wildcard $(VIDEODIR_PARALLEL)/parallel-rdp/util/*.h) $(GRANITE_POOL_TEST_OBJS) \
		$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(LIBRETRO_COMM_DIR)/include -c $(LIBRETRO_COMM_DIR)/rthreads/rthreads.c -o tools/test_granite_util_rthreads.o
	$(CXX) -std=c++11 $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW -I$(VIDEODIR_PARALLEL)/parallel-rdp/util \
		-I$(LIBRETRO_COMM_DIR)/include $< $(GRANITE_POOL_TEST_OBJS) tools/test_granite_util_rthreads.o \
		-o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's device memory allocator (tools/test_vk_memory_allocator.c)
test_vk_memory_allocator$(EXE_EXT): tools/test_vk_memory_allocator.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/memory_allocator.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/legion_allocator.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# parallel-RDP's VI intermediate image reuse rules
test_incoherent_pages$(EXE_EXT): tools/test_incoherent_pages.cpp $(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp/incoherent_pages.hpp
	$(CXX) -std=c++11 $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp -I$(VIDEODIR_PARALLEL)/parallel-rdp/util $< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_vi_image_pool$(EXE_EXT): tools/test_vi_image_pool.cpp $(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp/vi_image_pool.hpp \
		$(GRANITE_POOL_TEST_OBJS)
	$(CXX) -std=c++11 $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DGRANITE_VULKAN_MT -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) \
		$< $(GRANITE_POOL_TEST_OBJS) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

COMMAND_RING_TEST_INC := -I$(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp -I$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan \
	-I$(VIDEODIR_PARALLEL)/parallel-rdp/util -I$(VIDEODIR_PARALLEL)/parallel-rdp/volk \
	-I$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan-headers/include -I$(LIBRETRO_COMM_DIR)/include

# The command ring alone, with a stand-in processor (tools/test_command_ring.c).
COMMAND_RING_TEST_SRC := tools/test_command_ring.c $(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp/command_ring.c \
	$(VIDEODIR_PARALLEL)/parallel-rdp/util/thread_id.c \
	$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c $(LIBRETRO_COMM_DIR)/rthreads/retro_eventcount.c
test_command_ring$(EXE_EXT): $(COMMAND_RING_TEST_SRC)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's fence wait protocol, fence and semaphore pools and cookies (tools/test_vk_sync.c)
VK_SYNC_TEST_SRC := tools/test_vk_sync.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/fence_wait.c \
	$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/sync_pools.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/cookie.c \
	$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c $(LIBRETRO_COMM_DIR)/rthreads/retro_eventcount.c
test_vk_sync$(EXE_EXT): $(VK_SYNC_TEST_SRC)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's command pools (tools/test_vk_command_pool.c)
test_vk_command_pool$(EXE_EXT): tools/test_vk_command_pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/command_pool.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's texture formats and upload layout (tools/test_vk_texture_format.c)
test_vk_texture_format$(EXE_EXT): tools/test_vk_texture_format.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/texture_format.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's descriptor set allocator (tools/test_vk_descriptor_set.c)
test_vk_descriptor_set$(EXE_EXT): tools/test_vk_descriptor_set.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/descriptor_set.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's shader and pipeline layouts (tools/test_vk_shader.c)
test_vk_shader$(EXE_EXT): tools/test_vk_shader.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/shader.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's per-program pipeline map, from threads (tools/test_vk_pipeline_map.c)
test_vk_pipeline_map$(EXE_EXT): tools/test_vk_pipeline_map.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/pipeline_map.c \
		$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's render passes (tools/test_vk_render_pass.c)
test_vk_render_pass$(EXE_EXT): tools/test_vk_render_pass.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/render_pass.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's framebuffer allocator (tools/test_vk_framebuffer.c)
test_vk_framebuffer$(EXE_EXT): tools/test_vk_framebuffer.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/framebuffer.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's pipeline hashes and create infos (tools/test_vk_pipeline_compile.c)
test_vk_pipeline_compile$(EXE_EXT): tools/test_vk_pipeline_compile.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/pipeline_compile.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/render_pass.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's resource bindings and descriptor set flushing (tools/test_vk_resource_bindings.c)
test_vk_resource_bindings$(EXE_EXT): tools/test_vk_resource_bindings.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/resource_bindings.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/descriptor_set.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/shader.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's command buffer state and its flushing (tools/test_vk_command_state.c)
test_vk_command_state$(EXE_EXT): tools/test_vk_command_state.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/command_state.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/resource_bindings.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/descriptor_set.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/shader.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's command buffer operations on images (tools/test_vk_command_ops.c)
test_vk_command_ops$(EXE_EXT): tools/test_vk_command_ops.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/command_ops.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's queue submission composer (tools/test_vk_submit_batch.c)
test_vk_submit_batch$(EXE_EXT): tools/test_vk_submit_batch.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/submit_batch.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's memory type choice and buffer creation (tools/test_vk_buffer_create.c)
test_vk_buffer_create$(EXE_EXT): tools/test_vk_buffer_create.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/buffer_create.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/memory_allocator.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/legion_allocator.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's image and image view creation (tools/test_vk_image_create.c)
test_vk_image_create$(EXE_EXT): tools/test_vk_image_create.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/image_create.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/buffer_create.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/command_ops.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/memory_allocator.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/legion_allocator.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's image upload (tools/test_vk_image_upload.c)
test_vk_image_upload$(EXE_EXT): tools/test_vk_image_upload.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/image_upload.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/image_create.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/texture_format.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/buffer_create.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/command_ops.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/memory_allocator.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/legion_allocator.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's vertex and uniform block pool (tools/test_vk_buffer_pool.c)
test_vk_buffer_pool$(EXE_EXT): tools/test_vk_buffer_pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/buffer_pool.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's counted buffers and buffer views (tools/test_vk_buffer.c)
test_vk_buffer$(EXE_EXT): tools/test_vk_buffer.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/buffer.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/cookie.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c $(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's counted images and image views (tools/test_vk_image.c)
test_vk_image$(EXE_EXT): tools/test_vk_image.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/image.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/cookie.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's sampler (tools/test_vk_sampler.c)
test_vk_sampler$(EXE_EXT): tools/test_vk_sampler.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/sampler.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/cookie.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's counted fences (tools/test_vk_fence.c)
test_vk_fence$(EXE_EXT): tools/test_vk_fence.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/fence.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/fence_wait.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c $(LIBRETRO_COMM_DIR)/rthreads/retro_eventcount.c \
		$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's counted semaphores (tools/test_vk_semaphore.c)
test_vk_semaphore$(EXE_EXT): tools/test_vk_semaphore.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/semaphore.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/submit_batch.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/frame_garbage.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/sync_pools.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's queue waits and staging plan (tools/test_vk_queue_submit.c)
test_vk_queue_submit$(EXE_EXT): tools/test_vk_queue_submit.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/queue_submit.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/semaphore.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/submit_batch.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/frame_garbage.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/sync_pools.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's frame waits (tools/test_vk_frame_sync.c)
test_vk_frame_sync$(EXE_EXT): tools/test_vk_frame_sync.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/frame_sync.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# Granite's saved pipeline cache and pipeline record table (tools/test_vk_pipeline_cache.c)
test_vk_pipeline_cache$(EXE_EXT): tools/test_vk_pipeline_cache.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/pipeline_cache.c \
		$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -DRETRO_ATOMIC_FORCE_GCC_NEW $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's per-frame garbage lists (tools/test_frame_garbage.c)
test_frame_garbage$(EXE_EXT): tools/test_frame_garbage.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/frame_garbage.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/sync_pools.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# GLideN64's combiner key journal, from a thread (tools/test_combiner_journal.c)
COMBINER_JOURNAL_TEST_SRC := $(VIDEODIR_GLIDEN64)/src/CombinerKeyJournal.c $(LIBRETRO_COMM_DIR)/rthreads/rthreads.c \
	$(LIBRETRO_COMM_DIR)/rthreads/retro_eventcount.c $(LIBRETRO_COMM_DIR)/streams/file_stream.c \
	$(LIBRETRO_COMM_DIR)/vfs/vfs_implementation.c $(LIBRETRO_COMM_DIR)/string/stdstring.c \
	$(LIBRETRO_COMM_DIR)/compat/compat_strl.c $(LIBRETRO_COMM_DIR)/file/file_path.c \
	$(LIBRETRO_COMM_DIR)/file/file_path_io.c $(LIBRETRO_COMM_DIR)/time/rtime.c \
	$(LIBRETRO_COMM_DIR)/encodings/encoding_utf.c $(LIBRETRO_COMM_DIR)/compat/fopen_utf8.c \
	$(LIBRETRO_COMM_DIR)/encodings/encoding_crc32.c $(LIBRETRO_COMM_DIR)/features/features_cpu.c
test_combiner_journal$(EXE_EXT): tools/test_combiner_journal.c $(COMBINER_JOURNAL_TEST_SRC) $(VIDEODIR_GLIDEN64)/src/CombinerKeyJournal.h
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src -I$(LIBRETRO_COMM_DIR)/include tools/test_combiner_journal.c $(COMBINER_JOURNAL_TEST_SRC) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# What a parallel-rdp pipeline miss uses, allocated up front
test_pipeline_pending$(EXE_EXT): tools/test_pipeline_pending.c $(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp/pipeline_pending.h \
		$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) tools/test_pipeline_pending.c $(LIBRETRO_COMM_DIR)/rthreads/rthreads.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# Granite's release queue, from several threads (tools/test_release_queue.c)
test_release_queue$(EXE_EXT): tools/test_release_queue.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/release_queue.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/frame_garbage.c $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan/sync_pools.c \
		$(VIDEODIR_PARALLEL)/parallel-rdp/util/pool.c $(VIDEODIR_PARALLEL)/parallel-rdp/util/aligned_alloc.c \
		$(LIBRETRO_COMM_DIR)/rthreads/rthreads.c
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) $(COMMAND_RING_TEST_INC) $^ -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lpthread

# GLideN64's RDRAM writeback converting read-back rows in place, against
# the compacting copy it replaced
test_gliden64_write_rdram$(EXE_EXT): tools/test_gliden64_write_rdram.c $(VIDEODIR_GLIDEN64)/src/RdramWrite.c $(VIDEODIR_GLIDEN64)/src/gli_rdram_write.h
	$(CC) -std=c89 -D_DEFAULT_SOURCE $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src -I$(LIBRETRO_COMM_DIR)/include tools/test_gliden64_write_rdram.c $(VIDEODIR_GLIDEN64)/src/RdramWrite.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE) -lm

# Glide64's texture modifications at eight bits a channel
test_glide64_texmod$(EXE_EXT): tools/test_glide64_texmod.c $(VIDEODIR_GLIDE)/Glide64/TexMod.c $(VIDEODIR_GLIDE)/Glide64/TexMod.h
	$(CC) -std=c89 -D_DEFAULT_SOURCE $(CHECK_CFLAGS) $(CHECK_SANITIZE) tools/test_glide64_texmod.c $(VIDEODIR_GLIDE)/Glide64/TexMod.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# the host texture format holding every bit of the N64's texels
test_gliden64_texture_formats$(EXE_EXT): tools/test_gliden64_texture_formats.c $(VIDEODIR_GLIDEN64)/src/TextureDecode.c $(VIDEODIR_GLIDEN64)/src/convert.c $(VIDEODIR_GLIDEN64)/src/gli_texdecode.h
	$(CC) -std=c89 -D_DEFAULT_SOURCE $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src -I$(LIBRETRO_COMM_DIR)/include tools/test_gliden64_texture_formats.c $(VIDEODIR_GLIDEN64)/src/TextureDecode.c $(VIDEODIR_GLIDEN64)/src/convert.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# the RDRAM to colour buffer copy widening 5-bit channels to 8
test_gliden64_rdram_to_color$(EXE_EXT): tools/test_gliden64_rdram_to_color.c $(VIDEODIR_GLIDEN64)/src/RdramToColor.c $(VIDEODIR_GLIDEN64)/src/gli_rdram_to_color.h
	$(CC) -std=c89 -D_DEFAULT_SOURCE $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src -I$(LIBRETRO_COMM_DIR)/include tools/test_gliden64_rdram_to_color.c $(VIDEODIR_GLIDEN64)/src/RdramToColor.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# -Ofast as the core itself is built: that is what turned the quotient into
# a product with the reciprocal
test_gliden64_window_scale$(EXE_EXT): tools/test_gliden64_window_scale.c $(VIDEODIR_GLIDEN64)/src/DisplayWindow.c
	$(CC) $(CHECK_CFLAGS) -Ofast $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src -I$(LIBRETRO_COMM_DIR)/include -I$(CORE_DIR)/src/api tools/test_gliden64_window_scale.c $(VIDEODIR_GLIDEN64)/src/DisplayWindow.c -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_gliden64_deferred_readback$(EXE_EXT): tools/test_gliden64_deferred_readback.cpp $(VIDEODIR_GLIDEN64)/src/BufferCopy/DeferredReadback.h
	$(CXX) -std=c++11 $(CHECK_CFLAGS) $(CHECK_SANITIZE) $< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_glide64_fb_readback$(EXE_EXT): tools/test_glide64_fb_readback.c $(VIDEODIR_GLIDE)/Glide64/FBReadback.h Graphics/image_convert.h
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(LIBRETRO_COMM_DIR)/include $< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

# GLideN64 is moving to C89 a file at a time: what has moved stays there
GLIDEN64_C89 := N64.c convert.c RSP_LoadMatrix.c 3DMath.c CRC32.c RDP.c CRC_OPT.c GLideN64.c SoftwareRender.c DepthBufferRender/ClipPolygon.c DepthBufferRender/DepthBufferRender.c \
	gDP.c VI.c RSP.c GBI.c gSP.c TexrectDrawer.c FrameBufferInfo.c ZlutTexture.c PaletteTexture.c NoiseTexture.c StereoFrames.c TextureFilterHandler.c PostProcessor.c Config.c CombinerKey.c PluginAPI.c DisplayWindow.c DepthBuffer.c CombinerDecode.c TextureDecode.c TextureSizes.c TextureLRU.c VIUpdate.c FrameBufferRdram.c FrameBufferList.c RdramWrite.c RdramToColor.c TriangleList.c DrawerViewport.c DrawerStates.c DrawerRect.c TexrectSpecial.c BlendMode.c mupenplus/Config_mupenplus.c uCodes/F5Rogue.c uCodes/F5Indi_Naboo.c \
	uCodes/S2DEX.c \
	uCodes/ZSort.c uCodes/ZSortBOSS.c \
	uCodes/T3DUX.c uCodes/Turbo3D.c \
	uCodes/F3DEX2.c uCodes/F3DEX2ACCLAIM.c uCodes/F3DFLX2.c uCodes/L3D.c uCodes/L3DEX.c uCodes/L3DEX2.c uCodes/F3DEX3.c \
	uCodes/F3DEX095.c uCodes/F3DZEX2.c uCodes/F3DPD.c uCodes/F3DTEXA.c uCodes/F3DGOLDEN.c uCodes/S2DEX2.c uCodes/F3DBETA.c uCodes/F3DAM.c uCodes/F3DEX.c uCodes/F3DSETA.c uCodes/F3DDKR.c uCodes/F3DEX2CBFD.c uCodes/F3D.c \
	Graphics/VulkanContext/vk_spirv.c Graphics/VulkanContext/vk_shaders.c \
	Graphics/VulkanContext/vk_renderer.c Graphics/VulkanContext/vk_device.c Graphics/VulkanContext/vk_upscaler.c
check_gliden64_c89:
	@if grep -rn "pragma GCC diagnostic" $(VIDEODIR_GLIDEN64)/src --include=*.h --include=*.c --exclude-dir=inc; then echo "no diagnostic pragmas: they turn this check off"; exit 1; fi
	for f in $(GLIDEN64_C89); do $(CC) -std=c89 -pedantic -Wno-long-long -Werror=pedantic -Werror=declaration-after-statement \
		-Werror=implicit-function-declaration -Werror=implicit-int -Werror=strict-prototypes -D_DEFAULT_SOURCE \
		-isystem $(VIDEODIR_PARALLEL)/parallel-rdp/vulkan-headers/include -isystem mupen64plus-core/src/api -isystem $(LIBRETRO_COMM_DIR)/include \
		-I$(VIDEODIR_PARALLEL)/parallel-rdp/parallel-rdp -I$(VIDEODIR_GLIDEN64)/src -I$(VIDEODIR_GLIDEN64)/src/uCodes -I$(VIDEODIR_GLIDEN64)/src/Graphics/VulkanContext \
		-fsyntax-only $(VIDEODIR_GLIDEN64)/src/$$f || exit 1; done

GLIDEN64_SPIRV_SRC := $(VIDEODIR_GLIDEN64)/src/Graphics/VulkanContext/vk_spirv.c $(VIDEODIR_GLIDEN64)/src/Graphics/VulkanContext/vk_shaders.c
test_gliden64_spirv$(EXE_EXT): tools/test_gliden64_spirv.c $(GLIDEN64_SPIRV_SRC)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src/Graphics/VulkanContext $< $(GLIDEN64_SPIRV_SRC) -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

test_gliden64_ring_segments$(EXE_EXT): tools/test_gliden64_ring_segments.cpp $(VIDEODIR_GLIDEN64)/src/Graphics/OpenGLContext/opengl_RingSegments.h
	$(CXX) -std=c++11 $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(VIDEODIR_GLIDEN64)/src $< -o $@ $(CHECK_LDFLAGS) $(CHECK_SANITIZE)

CORE_CHECKS := test_rcp_bounds$(EXE_EXT) test_memory_map$(EXE_EXT) test_fb_write$(EXE_EXT) \
	test_cp0_exceptions$(EXE_EXT) test_r4300_interp$(EXE_EXT) test_fpu_compare$(EXE_EXT) test_pi_status$(EXE_EXT) \
	test_musyx_bounds$(EXE_EXT) test_musyx_aux$(EXE_EXT) test_alist_ops$(EXE_EXT) test_alist_abi1$(EXE_EXT) test_hle_dispatch$(EXE_EXT) angrylion_zboss_audio_test$(EXE_EXT) \
	test_mouse_accum$(EXE_EXT) test_hle_dram_wrap$(EXE_EXT) test_pif_write$(EXE_EXT) \
	test_re2_fill$(EXE_EXT) test_temporal_upscaler$(EXE_EXT) test_pipeline_records$(EXE_EXT) test_pipeline_pending$(EXE_EXT) test_rdp_x_range$(EXE_EXT) \
	test_command_ring$(EXE_EXT) test_vk_sync$(EXE_EXT) test_vk_command_pool$(EXE_EXT) test_vk_texture_format$(EXE_EXT) test_vk_descriptor_set$(EXE_EXT) test_vk_shader$(EXE_EXT) test_vk_pipeline_map$(EXE_EXT) test_vk_render_pass$(EXE_EXT) test_vk_framebuffer$(EXE_EXT) test_vk_pipeline_compile$(EXE_EXT) test_vk_resource_bindings$(EXE_EXT) test_vk_command_state$(EXE_EXT) test_vk_command_ops$(EXE_EXT) test_vk_submit_batch$(EXE_EXT) test_vk_buffer_create$(EXE_EXT) test_vk_image_create$(EXE_EXT) test_vk_image_upload$(EXE_EXT) test_vk_buffer_pool$(EXE_EXT) test_vk_buffer$(EXE_EXT) test_vk_image$(EXE_EXT) test_vk_sampler$(EXE_EXT) test_vk_fence$(EXE_EXT) test_vk_semaphore$(EXE_EXT) test_vk_queue_submit$(EXE_EXT) test_vk_frame_sync$(EXE_EXT) test_vk_pipeline_cache$(EXE_EXT) test_frame_garbage$(EXE_EXT) test_release_queue$(EXE_EXT) test_gb_cart_banks$(EXE_EXT) test_dynarec_cache_pages$(EXE_EXT) \
	test_combiner_journal$(EXE_EXT) test_gliden64_write_rdram$(EXE_EXT) test_gliden64_rdram_to_color$(EXE_EXT) test_gliden64_window_scale$(EXE_EXT) test_gliden64_texture_formats$(EXE_EXT) test_glide64_texmod$(EXE_EXT) test_gliden64_deferred_readback$(EXE_EXT) test_gliden64_ring_segments$(EXE_EXT) test_glide64_fb_readback$(EXE_EXT) test_granite_util$(EXE_EXT) test_granite_pool$(EXE_EXT) test_vk_memory_allocator$(EXE_EXT) \
	test_vi_image_pool$(EXE_EXT) test_incoherent_pages$(EXE_EXT)

ifeq ($(HAVE_PARALLEL_RSP), 1)
PRSP_TEST_OBJS := $(filter $(RSPDIR_PARALLEL)/%,$(OBJECTS))
test_prsp_dmem_wrap$(EXE_EXT): tools/test_prsp_dmem_wrap.c $(PRSP_TEST_OBJS)
	$(CC) $(CHECK_CFLAGS) $(CHECK_SANITIZE) -I$(CORE_DIR)/src -c $< -o tools/test_prsp_dmem_wrap.o
	$(CXX) tools/test_prsp_dmem_wrap.o $(PRSP_TEST_OBJS) -o $@ $(CHECK_SANITIZE) -lpthread -lm
CORE_CHECKS += test_prsp_dmem_wrap$(EXE_EXT)
endif

check-core: $(CORE_CHECKS)
	@set -e; for t in $(CORE_CHECKS); do echo "== $$t"; ./$$t; done

.PHONY: clean clean-tools tools check-angrylion check-core check-dynarec-block check-savestate-latch check-savestate-code check-cpu-fallback
-include $(OBJECTS:.o=.d)

# A header named by an existing .d file but since deleted is not an error:
# the objects that included it just rebuild.
%.h %.hpp:
	@:

# Nor is a source it names that was since removed or renamed (a .cpp
# that became a .c): the object is rebuilt from the source there is. The
# built-in rules that make a program from a source are dropped, so that
# no missing file is ever "made" from such a source.
%: %.c
%: %.cpp
%: %.o
%.c %.cpp:
	@:
endif

print-%:
	@echo '$*=$($*)'
