#include <Log.h>
#include <Config.h>
#include "opengl_Utils.h"
#include "opengl_GLInfo.h"
#include <regex>
#ifdef EGL
#include <EGL/egl.h>
#endif

#ifdef OS_ANDROID
#include <Graphics/OpenGLContext/GraphicBuffer/GraphicBufferWrapper.h>
#endif

using namespace opengl;

void GLInfo::init() {
	const char * strVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	isGLESX = strstr(strVersion, "OpenGL ES") != nullptr;
	isGLES2 = strstr(strVersion, "OpenGL ES 2") != nullptr;
	if (isGLES2) {
		majorVersion = 2;
		minorVersion = 0;
	} else {
		glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
		glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
	}
	LOG(LOG_VERBOSE, "%s major version: %d\n", isGLESX ? "OpenGL ES" : "OpenGL", majorVersion);
	LOG(LOG_VERBOSE, "%s minor version: %d\n", isGLESX ? "OpenGL ES" : "OpenGL", minorVersion);


	LOG(LOG_VERBOSE, "OpenGL vendor: %s\n", glGetString(GL_VENDOR));
	const GLubyte * strRenderer = glGetString(GL_RENDERER);
	const GLubyte * strDriverVersion = glGetString(GL_VERSION);

	bool isAnyAdreno    = strstr((const char*)strRenderer, "Adreno") != nullptr;
	bool isFreedreno    = strstr((const char*)strRenderer, "FD") != nullptr;  // freedreno uses "FDxxx" naming
	bool isAdrenoFamily = isAnyAdreno || isFreedreno;

	if (std::regex_match(std::string((const char*)strRenderer), std::regex("Adreno.*530")))
		renderer = Renderer::Adreno530;
	else if (std::regex_match(std::string((const char*)strRenderer), std::regex("Adreno.*540")) ||
		std::regex_match(std::string((const char*)strRenderer), std::regex("Adreno.*6\\d\\d")) ||
		std::regex_match(std::string((const char*)strRenderer), std::regex(".*FD6\\d\\d.*")))  // freedreno 6xx series
		renderer = Renderer::Adreno_no_bugs;
	else if (std::regex_match(std::string((const char*)strRenderer), std::regex(".*FD5\\d\\d.*")))  // freedreno 5xx series
		renderer = Renderer::Adreno;
	else if (isAdrenoFamily)
		renderer = Renderer::Adreno;
	else if (strstr((const char*)strRenderer, "VideoCore IV") != nullptr)
		renderer = Renderer::VideoCore;
	else if (strstr((const char*)strRenderer, "Intel") != nullptr)
		renderer = Renderer::Intel;
	else if (strstr((const char*)strRenderer, "PowerVR") != nullptr)
		renderer = Renderer::PowerVR;
	else if (strstr((const char*)strRenderer, "NVIDIA Tegra") != nullptr)
		renderer = Renderer::Tegra;
	LOG(LOG_VERBOSE, "OpenGL renderer: %s\n", strRenderer);

	/* ANGLE (Microsoft's GLES translator over D3D11 / D3D12 / Vulkan /
	 * Metal) reports its underlying device through GL_RENDERER and is
	 * therefore caught by one of the entries above on most platforms.
	 * The fact that the underlying device is talking through ANGLE
	 * shows up in GL_VERSION instead, where the driver-version string
	 * contains the substring "ANGLE". On Windows/Edge, Apple/Metal,
	 * and Android-with-ANGLE this needs to win over the underlying
	 * device classification because the ANGLE translation layer has
	 * its own quirks - notably the fragment-depth-write paths below.
	 * Apply the override after the chain. */
	if (strstr((const char*)strDriverVersion, "ANGLE") != nullptr)
		renderer = Renderer::Angle;

	/* Renderer-specific config overrides:
	 *
	 *   PowerVR (Apple A4-A10, some embedded ARM SoCs): the GLES driver
	 *   has a long-standing bug where gl_FragDepth writes are silently
	 *   dropped and forceDepthBufferClear=1 is required to keep the
	 *   depth attachment in a sane state across frames.
	 *
	 *   Angle on Android: the D3D / Vulkan translation path does not
	 *   forward fragment depth writes through the GLES emulation layer.
	 *   Disable the gliden64 fragment-depth-write path on the
	 *   combination; the desktop ANGLE flavours handle it fine.
	 */
	if (renderer == Renderer::PowerVR) {
		config.frameBufferEmulation.forceDepthBufferClear = 1;
		config.generalEmulation.enableFragmentDepthWrite = 0;
	}

#ifdef OS_ANDROID
	if (renderer == Renderer::Angle) {
		config.generalEmulation.enableFragmentDepthWrite = 0;
	}
#endif

	int numericVersion = majorVersion * 10 + minorVersion;
	if (isGLES2) {
		imageTextures = false;
		msaa = false;
	} else if (isGLESX) {
		imageTextures = (numericVersion >= 31);
		msaa = numericVersion >= 31;
	} else {
		imageTextures = (numericVersion >= 42) || Utils::isExtensionSupported(*this, "GL_ARB_shader_image_load_store");
		msaa = false;
	}

	//Tegra has a buggy implementation of fragment_shader_interlock that causes graphics lockups on drivers below 390.00
	bool hasBuggyFragmentShaderInterlock = false;

	if (renderer == Renderer::Tegra) {
		std::string strDriverVersionString((const char*)strDriverVersion);
		std::string nvidiaText = "NVIDIA";
		std::size_t versionPosition = strDriverVersionString.find(nvidiaText);

		if (versionPosition == std::string::npos) {
			hasBuggyFragmentShaderInterlock = true;
		} else {
			std::string strDriverVersionNumber = strDriverVersionString.substr(versionPosition + nvidiaText.length() + 1);
			float versionNumber = std::stof(strDriverVersionNumber);
			hasBuggyFragmentShaderInterlock = versionNumber < 390.0;
		}
	}

	fragment_interlock = Utils::isExtensionSupported(*this, "GL_ARB_fragment_shader_interlock") && !hasBuggyFragmentShaderInterlock;
	fragment_interlockNV = Utils::isExtensionSupported(*this, "GL_NV_fragment_shader_interlock") && !fragment_interlock && !hasBuggyFragmentShaderInterlock;
	fragment_ordering = Utils::isExtensionSupported(*this, "GL_INTEL_fragment_shader_ordering") && !fragment_interlock && !fragment_interlockNV;

	imageTextures = imageTextures && (fragment_interlock || fragment_interlockNV || fragment_ordering);

	if (isGLES2)
		config.generalEmulation.enableFragmentDepthWrite = 0;

	bufferStorage = (!isGLESX && (numericVersion >= 44)) || Utils::isExtensionSupported(*this, "GL_ARB_buffer_storage") ||
			Utils::isExtensionSupported(*this, "GL_EXT_buffer_storage");

	texStorage = (isGLESX && (numericVersion >= 30)) || (!isGLESX && numericVersion >= 42) ||
			Utils::isExtensionSupported(*this, "GL_ARB_texture_storage");

	shaderStorage = false;
	if (config.generalEmulation.enableShadersStorage != 0) {
		const char * strGetProgramBinary = isGLESX
			? "GL_OES_get_program_binary"
			: "GL_ARB_get_program_binary";
		if ((isGLESX && numericVersion >= 30) || (!isGLESX && numericVersion >= 41) || Utils::isExtensionSupported(*this, strGetProgramBinary)) {
			GLint numBinaryFormats = 0;
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinaryFormats);
			shaderStorage = numBinaryFormats > 0;
		}
	}

	bool ext_draw_buffers_indexed = isGLESX && (Utils::isExtensionSupported(*this, "GL_EXT_draw_buffers_indexed") || numericVersion >= 32);
#ifdef EGL
	if (isGLESX && bufferStorage)
		g_glBufferStorage = (PFNGLBUFFERSTORAGEPROC) eglGetProcAddress("glBufferStorageEXT");
	if (isGLESX && numericVersion < 32) {
		if (ext_draw_buffers_indexed) {
			g_glEnablei = (PFNGLENABLEIPROC) eglGetProcAddress("glEnableiEXT");
			g_glDisablei = (PFNGLDISABLEIPROC) eglGetProcAddress("glDisableiEXT");
		} else {
			g_glEnablei = nullptr;
			g_glDisablei = nullptr;
		}
	}
	if (isGLES2 && shaderStorage) {
		g_glProgramBinary = (PFNGLPROGRAMBINARYPROC) eglGetProcAddress("glProgramBinaryOES");
		g_glGetProgramBinary = (PFNGLGETPROGRAMBINARYPROC) eglGetProcAddress("glGetProgramBinaryOES");
		g_glProgramParameteri = nullptr;
	}
#endif
#ifndef OS_ANDROID
	if (isGLES2 && config.frameBufferEmulation.copyToRDRAM > Config::ctSync) {
		config.frameBufferEmulation.copyToRDRAM = Config::ctDisable;
		LOG(LOG_WARNING, "Async color buffer copies are not supported on GLES2\n");
	}
#endif
	if (isGLES2 && config.generalEmulation.enableLOD) {
		if (!Utils::isExtensionSupported(*this, "GL_EXT_shader_texture_lod") || !Utils::isExtensionSupported(*this, "GL_OES_standard_derivatives")) {
			config.generalEmulation.enableLOD = 0;
			LOG(LOG_WARNING, "LOD emulation not possible on this device\n");
		}
	}

	depthTexture = !isGLES2 || Utils::isExtensionSupported(*this, "GL_OES_depth_texture");
	noPerspective = Utils::isExtensionSupported(*this, "GL_NV_shader_noperspective_interpolation");

	fetch_depth = Utils::isExtensionSupported(*this, "GL_ARM_shader_framebuffer_fetch_depth_stencil");
	texture_barrier = !isGLESX && (numericVersion >= 45 || Utils::isExtensionSupported(*this, "GL_ARB_texture_barrier"));
	texture_barrierNV = Utils::isExtensionSupported(*this, "GL_NV_texture_barrier");

	ext_fetch = Utils::isExtensionSupported(*this, "GL_EXT_shader_framebuffer_fetch") && !isGLES2 && (!isGLESX || ext_draw_buffers_indexed) && !imageTextures;
	eglImage = (Utils::isEGLExtensionSupported("EGL_KHR_image_base") || Utils::isEGLExtensionSupported("EGL_KHR_image"));

	/* Mesa freedreno running on the desktop OpenGL profile (not GLES)
	 * advertises GL_EXT_shader_framebuffer_fetch but the implementation
	 * miscompiles fragment shaders that read from gl_LastFragData,
	 * which produces rendering issues whenever the N64 depth compare
	 * path is engaged. The GLES-profile freedreno path is fine -
	 * exposure to the bug only happens through the desktop GL entry
	 * point. Force the framebuffer-fetch path off for that specific
	 * combination so the renderer falls back to imageTextures (or to
	 * disabling N64DepthCompare entirely on devices without either).
	 * isFreedreno is declared at the top of this function and stays
	 * in scope here.
	 */
	if (isFreedreno && !isGLESX)
		ext_fetch = false;

#ifdef OS_ANDROID
	eglImage = eglImage &&
	        ( (isGLES2 && GraphicBufferWrapper::isSupportAvailable()) || (isGLESX && GraphicBufferWrapper::isPublicSupportAvailable()) ) &&
		    (renderer != Renderer::PowerVR);
#endif

	if (config.frameBufferEmulation.N64DepthCompare != 0) {
		if (!imageTextures && !ext_fetch) {
			config.frameBufferEmulation.N64DepthCompare = 0;
			LOG(LOG_WARNING, "Your GPU does not support the extensions needed for N64 Depth Compare.\n");
		}
	}
}
