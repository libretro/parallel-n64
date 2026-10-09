LOCAL_PATH := $(call my-dir)

ROOT_DIR      := $(LOCAL_PATH)/..
LIBRETRO_DIR  := $(ROOT_DIR)/libretro

SOURCES_C     :=
SOURCES_CXX   :=
SOURCES_ASM   :=
INCFLAGS      :=
CFLAGS        :=
CXXFLAGS      :=
DYNAFLAGS     :=
HAVE_NEON     := 0
WITH_DYNAREC  :=

HAVE_OPENGL   := 1
GLES          := 1
HAVE_PARALLEL := 1
HAVE_PARALLEL_RSP := 1
HAVE_THR_AL   := 1

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
  WITH_DYNAREC := aarch64
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
  WITH_DYNAREC := arm
  HAVE_NEON := 1
else ifeq ($(TARGET_ARCH_ABI),x86)
  # X86 dynarec isn't position independent, so it will not run on api 23+
  # This core uses vulkan which is api 24+, so dynarec cannot be used
  WITH_DYNAREC := bogus
else ifeq ($(TARGET_ARCH_ABI),x86_64)
  # Both x86_64 dynarecs put their entry points (new_dyna_start, dyna_start) in
  # nasm sources, and ndk-build has no assembler for those - the core links
  # against symbols that were never built. Interpreters only here as well.
  WITH_DYNAREC := bogus
endif

include $(ROOT_DIR)/Makefile.common

COREFLAGS := -ffast-math -DM64P_CORE_PROTOTYPES -DM64P_PLUGIN_API -D__LIBRETRO__ -DINLINE="inline" -DANDROID -DARM_FIX $(GLFLAGS) $(INCFLAGS) $(DYNAFLAGS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

# GLideN64 resolves its headers from its own src/ and src/osal/ trees, and
# its names (Combiner.h, Config.h, N64.h) collide with the other video
# plugins, so it builds as its own module with those directories first on
# the include path - the same flags the Makefile's GLideN64 pattern rules
# use. The NDK ships no GL/glcorearb.h, so the copy in src/inc is used.
GLIDEN64_SRC       := $(ROOT_DIR)/mupen64plus-video-gliden64/src
GLIDEN64_SOURCES   := $(filter $(GLIDEN64_SRC)/%,$(SOURCES_CXX) $(SOURCES_C))
GLIDEN64_INCFLAGS  := -I$(GLIDEN64_SRC) -I$(GLIDEN64_SRC)/osal -I$(GLIDEN64_SRC)/inc

ifneq ($(GLIDEN64_SOURCES),)
include $(CLEAR_VARS)
LOCAL_MODULE       := gliden64
LOCAL_SRC_FILES    := $(GLIDEN64_SOURCES)
LOCAL_CFLAGS       := $(GLIDEN64_INCFLAGS) $(COREFLAGS) $(CFLAGS)
LOCAL_CXXFLAGS     := -std=c++11 $(GLIDEN64_INCFLAGS) $(COREFLAGS) $(CXXFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_ARM_NEON     := true
LOCAL_ARM_MODE     := arm
include $(BUILD_STATIC_LIBRARY)
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(filter-out $(GLIDEN64_SOURCES),$(SOURCES_CXX) $(SOURCES_C)) $(SOURCES_ASM)
ifneq ($(GLIDEN64_SOURCES),)
LOCAL_WHOLE_STATIC_LIBRARIES := gliden64
endif
# -fcommon as in the Makefile: the C plugins' tentative definitions of the
# state GLideN64 also defines (gDP, gSP, GBI, the G_* opcodes, RDRAM, TMEM)
# merge with GLideN64's at link, and only one plugin is ever active.
LOCAL_CFLAGS       := -fcommon $(COREFLAGS) $(CFLAGS)
LOCAL_CXXFLAGS     := -std=c++11 $(COREFLAGS) $(CXXFLAGS)
LOCAL_LDFLAGS      := -Wl,-version-script=$(LIBRETRO_DIR)/link.T
ifeq ($(GLES3),1)
LOCAL_LDLIBS       := -lGLESv3 -llog
else
LOCAL_LDLIBS       := -lGLESv2 -llog
endif
LOCAL_CPP_FEATURES := exceptions
LOCAL_ARM_NEON     := true
LOCAL_ARM_MODE     := arm
include $(BUILD_SHARED_LIBRARY)
