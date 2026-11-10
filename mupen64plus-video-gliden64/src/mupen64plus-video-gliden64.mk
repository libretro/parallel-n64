#############################
# mupen64plus-video-gliden64
#############################
include $(CLEAR_VARS)
LOCAL_PATH := $(JNI_LOCAL_PATH)
SRCDIR := ./$(BASE_DIR)/src

MY_LOCAL_MODULE := mupen64plus-video-gliden64
MY_LOCAL_SHARED_LIBRARIES := freetype osal
MY_LOCAL_STATIC_LIBRARIES := glidenhq
MY_LOCAL_ARM_MODE := arm

MY_LOCAL_C_INCLUDES :=                          \
    $(LOCAL_PATH)/$(SRCDIR)                     \
    $(M64P_API_INCLUDES)                        \
    $(SDL_INCLUDES)                             \
    $(FREETYPE_INCLUDES)                        \
    $(LOCAL_PATH)/$(SRCDIR)/osal                \
    $(GL_INCLUDES)

MY_LOCAL_SRC_FILES :=                                                              \
    $(SRCDIR)/Combiner.cpp                                                         \
    $(SRCDIR)/CombinerDecode.c                                                     \
    $(SRCDIR)/CombinerKey.c                                                       \
    $(SRCDIR)/PluginAPI.c                                                        \
    $(SRCDIR)/Config.c                                                            \
    $(SRCDIR)/convert.c                                                          \
    $(SRCDIR)/DepthBuffer.c                                                        \
    $(SRCDIR)/DepthBufferGpu.cpp                                                   \
    $(SRCDIR)/DisplayWindow.c                                                      \
    $(SRCDIR)/DisplayWindowGpu.cpp                                                 \
    $(SRCDIR)/FrameBuffer.cpp                                                      \
    $(SRCDIR)/FrameBufferInfo.c                                                   \
    $(SRCDIR)/GBI.c                                                              \
    $(SRCDIR)/gDP.c                                                              \
    $(SRCDIR)/GLideN64.c                                                         \
    $(SRCDIR)/GraphicsDrawer.cpp                                                   \
    $(SRCDIR)/gSP.c                                                              \
    $(SRCDIR)/N64.c                                                              \
    $(SRCDIR)/NoiseTexture.c                                                       \
    $(SRCDIR)/PaletteTexture.c                                                     \
    $(SRCDIR)/PostProcessor.c                                                     \
    $(SRCDIR)/RDP.c                                                              \
    $(SRCDIR)/RSP.c                                                              \
    $(SRCDIR)/SoftwareRender.c                                                   \
    $(SRCDIR)/TexrectDrawer.c                                                      \
    $(SRCDIR)/TexrectDrawerGpu.cpp                                                    \
    $(SRCDIR)/TextureFilterHandler.c                                              \
    $(SRCDIR)/Textures.cpp                                                         \
    $(SRCDIR)/VI.c                                                               \
    $(SRCDIR)/ZlutTexture.c                                                        \
    $(SRCDIR)/mupenplus/Config_mupenplus.c                                       \
    $(SRCDIR)/DepthBufferRender/ClipPolygon.c                                    \
    $(SRCDIR)/DepthBufferRender/DepthBufferRender.c                              \
    $(SRCDIR)/BufferCopy/ColorBufferToRDRAM.cpp                                    \
    $(SRCDIR)/BufferCopy/DepthBufferToRDRAM.cpp                                    \
    $(SRCDIR)/BufferCopy/RDRAMtoColorBuffer.cpp                                    \
    $(SRCDIR)/Graphics/Context.cpp                                                 \
    $(SRCDIR)/Graphics/ColorBufferReader.cpp                                       \
    $(SRCDIR)/Graphics/CombinerProgram.cpp                                         \
    $(SRCDIR)/Graphics/ObjectHandle.cpp                                            \
    $(SRCDIR)/Graphics/OpenGLContext/GLFunctions.cpp                               \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_Attributes.cpp                         \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_BufferedDrawer.cpp                     \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.cpp    \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_CachedFunctions.cpp                    \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_ColorBufferReaderWithBufferStorage.cpp \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_ColorBufferReaderWithPixelBuffer.cpp   \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_ColorBufferReaderWithReadPixels.cpp    \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_ContextImpl.cpp                        \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_GLInfo.cpp                             \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_Parameters.cpp                         \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.cpp   \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_UnbufferedDrawer.cpp                   \
    $(SRCDIR)/Graphics/OpenGLContext/opengl_Utils.cpp                              \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_CombinerInputs.cpp                  \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilder.cpp          \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.cpp             \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactory.cpp   \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_FXAA.cpp                            \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_ShaderStorage.cpp                   \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_SpecialShadersFactory.cpp           \
    $(SRCDIR)/Graphics/OpenGLContext/GLSL/glsl_Utils.cpp                           \
    $(SRCDIR)/uCodes/F3D.c                                                       \
    $(SRCDIR)/uCodes/F3DAM.c                                                     \
    $(SRCDIR)/uCodes/F3DBETA.c                                                   \
    $(SRCDIR)/uCodes/F3DDKR.c                                                    \
    $(SRCDIR)/uCodes/F3DEX.c                                                     \
    $(SRCDIR)/uCodes/F3DEX2.c                                                    \
    $(SRCDIR)/uCodes/F3DEX2ACCLAIM.c                                             \
    $(SRCDIR)/uCodes/F3DEX2CBFD.c                                                \
    $(SRCDIR)/uCodes/F3DZEX2.c                                                   \
    $(SRCDIR)/uCodes/F3DFLX2.c                                                   \
    $(SRCDIR)/uCodes/F3DGOLDEN.c                                                 \
    $(SRCDIR)/uCodes/F3DPD.c                                                     \
    $(SRCDIR)/uCodes/F3DSETA.c                                                   \
    $(SRCDIR)/uCodes/F5Indi_Naboo.c                                              \
    $(SRCDIR)/uCodes/F5Rogue.c                                                    \
    $(SRCDIR)/uCodes/F3DTEXA.c                                                   \
    $(SRCDIR)/uCodes/L3D.c                                                       \
    $(SRCDIR)/uCodes/L3DEX2.c                                                    \
    $(SRCDIR)/uCodes/L3DEX.c                                                     \
    $(SRCDIR)/uCodes/S2DEX2.c                                                    \
    $(SRCDIR)/uCodes/S2DEX_gfx.cpp \
    $(SRCDIR)/uCodes/S2DEX.c                                                     \
    $(SRCDIR)/uCodes/T3DUX.c                                                     \
    $(SRCDIR)/uCodes/Turbo3D.c                                                   \
    $(SRCDIR)/uCodes/ZSort.c                                                     \
    $(SRCDIR)/uCodes/ZSortBOSS.c                                                 \
    $(SRCDIR)/CRC_OPT.c \

MY_LOCAL_CFLAGS :=          \
    $(COMMON_CFLAGS)        \
    -g                      \
    -DTXFILTER_LIB          \
    -DOS_ANDROID            \
    -DUSE_SDL               \
    -DMUPENPLUSAPI          \
    -DEGL                   \
    -DEGL_EGLEXT_PROTOTYPES \
    -fsigned-char           \
    #-DSDL_NO_COMPAT        \

MY_LOCAL_CPPFLAGS := $(COMMON_CPPFLAGS) -std=c++11 -g

MY_LOCAL_LDFLAGS := $(COMMON_LDFLAGS) -Wl,-version-script,$(LOCAL_PATH)/$(SRCDIR)/mupenplus/video_api_export.ver

MY_LOCAL_LDLIBS := -llog -lEGL

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    # Use for ARM7a:
    MY_LOCAL_SRC_FILES += $(SRCDIR)/Neon/3DMathNeon.cpp
    MY_LOCAL_SRC_FILES += $(SRCDIR)/Neon/gSPNeon.cpp
    MY_LOCAL_SRC_FILES += $(SRCDIR)/Neon/RSP_LoadMatrixNeon.cpp
    MY_LOCAL_CFLAGS += -D__NEON_OPT
    MY_LOCAL_CFLAGS += -D__VEC4_OPT -mfpu=neon

else ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    # Use for ARM8a:
    MY_LOCAL_SRC_FILES += $(SRCDIR)/Neon/3DMathNeon.cpp
    MY_LOCAL_SRC_FILES += $(SRCDIR)/Neon/gSPNeon.cpp
    MY_LOCAL_SRC_FILES += $(SRCDIR)/Neon/RSP_LoadMatrixNeon.cpp
    MY_LOCAL_CFLAGS += -D__NEON_OPT
    MY_LOCAL_CFLAGS += -D__VEC4_OPT -mfpu=neon

else ifeq ($(TARGET_ARCH_ABI), x86)
#    MY_LOCAL_CFLAGS += -DX86_ASM
    MY_LOCAL_CFLAGS += -D__VEC4_OPT
    MY_LOCAL_SRC_FILES += $(SRCDIR)/3DMath.c
    MY_LOCAL_SRC_FILES += $(SRCDIR)/RSP_LoadMatrix.c

else ifeq ($(TARGET_ARCH_ABI), x86_64)
#    MY_LOCAL_CFLAGS += -DX86_ASM
    MY_LOCAL_CFLAGS += -D__VEC4_OPT
    MY_LOCAL_SRC_FILES += $(SRCDIR)/3DMath.c
    MY_LOCAL_SRC_FILES += $(SRCDIR)/RSP_LoadMatrix.c
endif

###########
# Build library
###########
include $(CLEAR_VARS)
LOCAL_MODULE            := $(MY_LOCAL_MODULE)
LOCAL_SHARED_LIBRARIES  := $(MY_LOCAL_SHARED_LIBRARIES)
LOCAL_STATIC_LIBRARIES  := $(MY_LOCAL_STATIC_LIBRARIES)
LOCAL_ARM_MODE          := $(MY_LOCAL_ARM_MODE)
LOCAL_C_INCLUDES        := $(MY_LOCAL_C_INCLUDES) $(LOCAL_PATH)/GL/
LOCAL_SRC_FILES         := $(MY_LOCAL_SRC_FILES)
LOCAL_CFLAGS            := $(MY_LOCAL_CFLAGS)
LOCAL_CPPFLAGS          := $(MY_LOCAL_CPPFLAGS)
LOCAL_LDFLAGS           := $(MY_LOCAL_LDFLAGS)
LOCAL_LDLIBS            := $(MY_LOCAL_LDLIBS)

include $(BUILD_SHARED_LIBRARY)
