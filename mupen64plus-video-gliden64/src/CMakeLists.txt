cmake_minimum_required(VERSION 2.6)

if ((${CMAKE_VERSION} VERSION_EQUAL 3.9) OR (${CMAKE_VERSION} VERSION_GREATER 3.9))
  cmake_policy(SET CMP0069 NEW)
endif()

option(EGL "Set to ON if targeting an EGL device" ${EGL})
option(PANDORA "Set to ON if targeting an OpenPandora" ${PANDORA})
option(ODROID "Set to ON if targeting an Odroid" ${ODROID})
option(MUPENPLUSAPI "Set to ON for Mupen64Plus plugin" ${MUPENPLUSAPI})
option(MESA "Set to ON to disable Raspberry Pi autodetection" ${MESA})
option(VERO4K "Set to ON if targeting a Vero4k" ${VERO4K})

project( GLideN64 )

set(GLideN64_SOURCES
  3DMath.c
  Combiner.cpp
  CombinerDecode.c
  TextureDecode.c
  CombinerKey.c
  PluginAPI.c
  Config.c
  convert.c
  CRC32.c
  DepthBuffer.c
  DepthBufferGpu.cpp
  DisplayWindow.c
  DisplayWindowGpu.cpp
  FrameBuffer.cpp
  FrameBufferInfo.c
  GBI.c
  gDP.c
  GLideN64.c
  GraphicsDrawer.cpp
  gSP.c
  N64.c
  NoiseTexture.c
  PaletteTexture.c
  PostProcessor.c
  RDP.c
  RSP.c
  RSP_LoadMatrix.c
  SoftwareRender.c
  TexrectDrawer.c
  TexrectDrawerGpu.cpp
  TextureFilterHandler.c
  Textures.cpp
  VI.c
  ZlutTexture.c
  BufferCopy/ColorBufferToRDRAM.cpp
  BufferCopy/DepthBufferToRDRAM.cpp
  BufferCopy/RDRAMtoColorBuffer.cpp
  DepthBufferRender/ClipPolygon.c
  DepthBufferRender/DepthBufferRender.c
  Graphics/Context.cpp
  Graphics/ColorBufferReader.cpp
  Graphics/CombinerProgram.cpp
  Graphics/CombinerValues.cpp
  Graphics/ObjectHandle.cpp
  Graphics/OpenGLContext/GLFunctions.cpp
  Graphics/OpenGLContext/opengl_Attributes.cpp
  Graphics/OpenGLContext/opengl_BufferedDrawer.cpp
  Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.cpp
  Graphics/OpenGLContext/opengl_CachedFunctions.cpp
  Graphics/OpenGLContext/opengl_ColorBufferReaderWithBufferStorage.cpp
  Graphics/OpenGLContext/opengl_ColorBufferReaderWithPixelBuffer.cpp
  Graphics/OpenGLContext/opengl_ColorBufferReaderWithReadPixels.cpp
  Graphics/OpenGLContext/opengl_ContextImpl.cpp
  Graphics/OpenGLContext/opengl_GLInfo.cpp
  Graphics/OpenGLContext/opengl_Parameters.cpp
  Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.cpp
  Graphics/OpenGLContext/opengl_UnbufferedDrawer.cpp
  Graphics/OpenGLContext/opengl_Utils.cpp
  Graphics/OpenGLContext/GLSL/glsl_CombinerInputs.cpp
  Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilder.cpp
  Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.cpp
  Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactory.cpp
  Graphics/OpenGLContext/GLSL/glsl_FXAA.cpp
  Graphics/OpenGLContext/GLSL/glsl_ShaderStorage.cpp
  Graphics/OpenGLContext/GLSL/glsl_SpecialShadersFactory.cpp
  Graphics/OpenGLContext/GLSL/glsl_Utils.cpp
  uCodes/F3D.c
  uCodes/F3DBETA.c
  uCodes/F3DDKR.c
  uCodes/F3DEX.c
  uCodes/F3DAM.c
  uCodes/F3DEX2.c
  uCodes/F3DEX2ACCLAIM.c
  uCodes/F3DEX2CBFD.c
  uCodes/F3DZEX2.c
  uCodes/F3DFLX2.c
  uCodes/F3DGOLDEN.c
  uCodes/F3DTEXA.c
  uCodes/F3DPD.c
  uCodes/F3DSETA.c
  uCodes/F5Indi_Naboo.c
  uCodes/F5Rogue.c
  uCodes/L3D.c
  uCodes/L3DEX2.c
  uCodes/L3DEX.c
  uCodes/S2DEX2.c
  uCodes/S2DEX.c
    uCodes/S2DEX_gfx.cpp
  uCodes/T3DUX.c
  uCodes/Turbo3D.c
  uCodes/ZSort.c
  uCodes/ZSortBOSS.c
)

#check if we're running on Raspberry Pi
if(EXISTS "/opt/vc/include/bcm_host.h" AND NOT MESA)
  message("bcm_host.h found")
  set(BCMHOST ON)
  set(EGL ON)
  set(EGL_LIB -lbrcmEGL)
  add_definitions(
    -DVC
  )
  include_directories(
    "/opt/vc/include"
    "/opt/vc/include/interface/vcos"
    "/opt/vc/include/interface/vmcs_host/linux"
    "/opt/vc/include/interface/vcos/pthreads"
  )
  link_directories(
    "/opt/vc/lib"
    "/opt/vc/lib/GL"
  )
else(EXISTS "/opt/vc/include/bcm_host.h" AND NOT MESA)
  set(EGL_LIB -lEGL)
endif(EXISTS "/opt/vc/include/bcm_host.h" AND NOT MESA)

if(MUPENPLUSAPI)
  add_definitions(
	-DMUPENPLUSAPI
		-DTXFILTER_LIB
  )
  include_directories( . inc )
  set(GLideN64_SOURCES_UNIX
	mupenplus/Config_mupenplus.c
  )

  set(GLideN64_SOURCES_WIN ${GLideN64_SOURCES_UNIX}
  )
  set(GLideN64_DLL_NAME mupen64plus-video-GLideN64)
else(MUPENPLUSAPI)
  if(UNIX)
    message(FATAL_ERROR "UNIX build requires MUPENPLUSAPI!")
  endif(UNIX)
  set(GLideN64_SOURCES_WIN
	common/GLFunctions.cpp
  )
  set(GLideN64_DLL_NAME GLideN64)
endif(MUPENPLUSAPI)

add_subdirectory( osal )
include_directories( osal )

if (NOHQ)
  list(APPEND GLideN64_SOURCES
    TxFilterStub.cpp
  )
else (NOHQ)
  add_subdirectory( GLideNHQ )
  include_directories( GLideNHQ )
endif(NOHQ)

if(UNIX)
  list(APPEND GLideN64_SOURCES ${GLideN64_SOURCES_UNIX})
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_definitions(
	  -DOS_MAC_OS_X
	)
	if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
	  set(CMAKE_MACOSX_RPATH 0)
	endif()
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	add_definitions(
	  -DOS_LINUX
	)
  endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
endif(UNIX)

if(MINGW)
  add_definitions(
    -DMINGW
  )
endif(MINGW)

if(WIN32)
  list(APPEND GLideN64_SOURCES ${GLideN64_SOURCES_WIN})
  add_definitions(
    -DOS_WINDOWS
    -D__WIN32__
    -DWIN32
    -D_WIN32_ASM
    -D_CRT_SECURE_NO_WARNINGS
    -D__MSC__
  )
endif(WIN32)

if(SDL)
  include(FindPkgConfig)
  pkg_check_modules(SDL REQUIRED sdl)
  include_directories(${SDL_INCLUDE_DIRS})
  add_definitions(
	-DUSE_SDL
  )
endif(SDL)

if(PANDORA)
  #Pandora as a SGX530, but it should share the bugs and limitations as SGX540
  add_definitions(
  -DPANDORA
  -DPowerVR_SGX_540
  -DEGL
   )
endif(PANDORA)

if(ODROID)
  add_definitions(
  -DODROID
   )
endif(ODROID)

if(VERO4K)
  add_definitions(
  -DVERO4K
   )
endif(VERO4K)

if(UNIX OR BCMHOST)
  SET( FREETYPE_INCLUDE_DIRS "/usr/include/freetype2/" )
else(UNIX OR BCMHOST)
  SET( ENV{FREETYPE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../freetype )
endif(UNIX OR BCMHOST)
FIND_PACKAGE( Freetype REQUIRED )
include_directories( ${FREETYPE_INCLUDE_DIRS} )

if(VEC4_OPT)
  add_definitions(
    -D__VEC4_OPT
  )
endif(VEC4_OPT)

if(CRC_OPT)
  list(REMOVE_ITEM GLideN64_SOURCES
        CRC32.c
  )
  list(APPEND GLideN64_SOURCES
        CRC_OPT.c
  )
endif(CRC_OPT)

if(NEON_OPT)
  add_definitions(
    -D__NEON_OPT
  )
  list(APPEND GLideN64_SOURCES
    Neon/3DMathNeon.cpp
    Neon/gSPNeon.cpp
    Neon/RSP_LoadMatrixNeon.cpp
  )
  list(REMOVE_ITEM GLideN64_SOURCES
    3DMath.c
    RSP_LoadMatrix.c
  )
endif(NEON_OPT)

if(X86_OPT)
  list(APPEND GLideN64_SOURCES
    RSP_LoadMatrixX86.cpp
  )
  list(REMOVE_ITEM GLideN64_SOURCES
    RSP_LoadMatrix.c
  )
endif(X86_OPT)

# Build type

if( NOT CMAKE_BUILD_TYPE)
	set( CMAKE_BUILD_TYPE Release)
endif( NOT CMAKE_BUILD_TYPE)

if( CMAKE_BUILD_TYPE STREQUAL "Debug")
	set( CMAKE_BUILD_TYPE Debug)
	set( DEBUG_BUILD TRUE)
	add_definitions(
	-DGL_DEBUG
	)
endif( CMAKE_BUILD_TYPE STREQUAL "Debug")

if(EGL)
  add_definitions(
    -DEGL
  )
  SET(OPENGL_LIBRARIES ${EGL_LIB})
else(EGL)
  find_package(OpenGL REQUIRED)
  include_directories(${OpenGL_INCLUDE_DIRS})
  link_directories(${OpenGL_LIBRARY_DIRS})
  add_definitions(${OpenGL_DEFINITIONS})
  if(NOT OPENGL_FOUND)
  	message(ERROR " OPENGL not found!")
  endif(NOT OPENGL_FOUND)
endif(EGL)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  #check for compiler version
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE G++_VERSION)
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND G++_VERSION VERSION_LESS 4.8)
    message("Found compiler version " ${G++_VERSION})
    message(SEND_ERROR "You need at least G++ 4.8 to compile GLideN64!")
  elseif(G++_VERSION VERSION_LESS 3.3)
    message("Found compiler version " ${G++_VERSION})
    message(SEND_ERROR "You need at least Clang 3.3 to compile GLideN64!")
  endif()
  SET(GCC_CPP11_COMPILE_FLAGS "-std=c++11")
  SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_CPP11_COMPILE_FLAGS}" )
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    SET(GCC_STATIC_LINK_FLAGS "-static-libgcc")
  endif()
  SET( CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS} ${GCC_STATIC_LINK_FLAGS}" )
endif()

add_library( ${GLideN64_DLL_NAME} SHARED ${GLideN64_SOURCES})

if( CMAKE_BUILD_TYPE STREQUAL "Debug")
  SET_TARGET_PROPERTIES(
	${GLideN64_DLL_NAME}
	PROPERTIES
	LINKER_LANGUAGE CXX # Or else we get an error message, because cmake can't figure out from the ".o"-suffix that it is a C-linker we need.
	PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugin/Debug
  )

  if(SDL)
        if (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} PRIVATE ${OPENGL_LIBRARIES} ${SDL_LIBRARIES} ${FREETYPE_LIBRARIES} osald )
	else (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} PRIVATE ${OPENGL_LIBRARIES} ${SDL_LIBRARIES} ${FREETYPE_LIBRARIES} osald GLideNHQd )
	endif (NOHQ)
  else(SDL)
	if (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} PRIVATE ${OPENGL_LIBRARIES} ${FREETYPE_LIBRARIES} osald )
	else (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} PRIVATE ${OPENGL_LIBRARIES} ${FREETYPE_LIBRARIES} osald GLideNHQd )
	endif (NOHQ)
  endif(SDL)
endif( CMAKE_BUILD_TYPE STREQUAL "Debug")

if( CMAKE_BUILD_TYPE STREQUAL "Release")
  if ((${CMAKE_VERSION} VERSION_EQUAL 3.9) OR (${CMAKE_VERSION} VERSION_GREATER 3.9))
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result)
    if(result)
      message("Interprocedural optimizations enabled")
      set_property(TARGET ${GLideN64_DLL_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
  endif()

  SET_TARGET_PROPERTIES(
	${GLideN64_DLL_NAME}
	PROPERTIES
	LINKER_LANGUAGE CXX # Or else we get an error message, because cmake can't figure out from the ".o"-suffix that it is a C-linker we need.
	PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugin/Release
  )

  if(SDL)
	if (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} ${OPENGL_LIBRARIES} ${SDL_LIBRARIES} ${FREETYPE_LIBRARIES} osal )
	else (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} ${OPENGL_LIBRARIES} ${SDL_LIBRARIES} ${FREETYPE_LIBRARIES} osal GLideNHQ )
	endif (NOHQ)
  else(SDL)
	if (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} ${OPENGL_LIBRARIES} ${FREETYPE_LIBRARIES} osal )
	else (NOHQ)
	  target_link_libraries(${GLideN64_DLL_NAME} ${OPENGL_LIBRARIES} ${FREETYPE_LIBRARIES} osal GLideNHQ )
	endif (NOHQ)
  endif(SDL)
endif( CMAKE_BUILD_TYPE STREQUAL "Release")
