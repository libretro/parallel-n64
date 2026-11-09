#pragma once
#include <memory>
#include <Combiner.h>
#include <Graphics/OpenGLContext/GLFunctions.h>
#include "glsl_CombinerInputs.h"
#include <Graphics/OpenGLContext/opengl_GLInfo.h>

namespace graphics {
	class CombinerProgram;
}

namespace opengl {
	class CachedUseProgram;
}

namespace glsl {

	class ShaderPart;
	class CombinerInputs;
	class CombinerProgramUniformFactory;
	class CombinerProgramImpl;
	class CombinerProgramAsync;

	class CombinerProgramBuilder
	{
	public:
		CombinerProgramBuilder(const opengl::GLInfo & _glinfo, opengl::CachedUseProgram * _useProgram);
		~CombinerProgramBuilder();

		graphics::CombinerProgram * buildCombinerProgram(Combiner & _color, Combiner & _alpha, const CombinerKey & _key);

		const ShaderPart * getVertexShaderHeader() const;

		const ShaderPart * getFragmentShaderHeader() const;

		const ShaderPart * getFragmentShaderEnd() const;

		/* Draws with the ubershader, built at load, and starts the key's own
		 * program where the driver compiles on its own threads; nullptr when
		 * the ubershader cannot draw this key. */
		graphics::CombinerProgram * buildCombinerProgramAsync(Combiner & _color, Combiner & _alpha, const CombinerKey & _key);

		bool isObsolete() const;

		bool isUberOnly() const { return m_uberOnly && !m_uberOff && !m_isGLES2; }

		struct UberVariant
		{
			GLuint handle = 0;
			bool failed = false;
			CombinerProgramImpl * program = nullptr;
			CombinerProgramAsync * owner = nullptr;
			GLint opsLoc[16];
			GLint flagsLoc = -1;
			CombinerInputs inputs;
			CombinerKey key;
		};

	private:
		CombinerInputs compileCombiner(const CombinerKey & _key, Combiner & _color, Combiner & _alpha, std::string & _strShader);
		std::string _fragmentSource(const CombinerInputs & combinerInputs, const std::string & strCombiner,
			bool bUseLod, bool bUseTextures, bool bUseHWLight, const char * _uberDeclarations);
		GLuint _startProgram(const std::string & _fragment, bool bIsRect, bool bUseTextures, bool _check);
		std::string _uberCombiner(bool _textured, bool _lod);
		UberVariant * _readyUber(const CombinerKey & _key, bool _textured, bool _lod, bool _hwl);
		void _startUber(UberVariant & _variant, const CombinerKey & _key, bool _textured, bool _lod, bool _hwl);
		void _finishUber(UberVariant & _variant);
		void _buildUbers();

		// [rect][cycle: 1, 2][bilerp][textured][lod][hardware lighting]
		UberVariant m_uber[2][2][4][2][2][2];
		bool m_isGLES2 = false;
		bool m_uberOff = false;
		bool m_uberOnly = false;
		int m_uberDelay = 0;

		typedef std::unique_ptr<ShaderPart> ShaderPartPtr;
		ShaderPartPtr m_blender1;
		ShaderPartPtr m_blender2;
		ShaderPartPtr m_legacyBlender;
		ShaderPartPtr m_clamp;
		ShaderPartPtr m_signExtendColorC;
		ShaderPartPtr m_signExtendAlphaC;
		ShaderPartPtr m_signExtendColorABD;
		ShaderPartPtr m_signExtendAlphaABD;
		ShaderPartPtr m_alphaTest;
		ShaderPartPtr m_callDither;

		ShaderPartPtr m_vertexHeader;
		ShaderPartPtr m_vertexEnd;
		ShaderPartPtr m_vertexRect;
		ShaderPartPtr m_vertexTexturedRect;
		ShaderPartPtr m_vertexTriangle;
		ShaderPartPtr m_vertexTexturedTriangle;

		ShaderPartPtr m_fragmentHeader;
		ShaderPartPtr m_fragmentGlobalVariablesTex;
		ShaderPartPtr m_fragmentGlobalVariablesNotex;
		ShaderPartPtr m_fragmentHeaderNoise;
		ShaderPartPtr m_fragmentHeaderWriteDepth;
		ShaderPartPtr m_fragmentHeaderCalcLight;
		ShaderPartPtr m_fragmentHeaderMipMap;
		ShaderPartPtr m_fragmentHeaderReadMSTex;
		ShaderPartPtr m_fragmentHeaderDither;
		ShaderPartPtr m_fragmentHeaderDepthCompare;
		ShaderPartPtr m_fragmentHeaderReadTex;
		ShaderPartPtr m_fragmentHeaderReadTexCopyMode;
		ShaderPartPtr m_fragmentMain;
		ShaderPartPtr m_fragmentMain2Cycle;
		ShaderPartPtr m_fragmentBlendMux;
		ShaderPartPtr m_fragmentReadTex0;
		ShaderPartPtr m_fragmentReadTex1;
		ShaderPartPtr m_fragmentReadTexCopyMode;
		ShaderPartPtr m_fragmentReadTexMipmap;
		ShaderPartPtr m_fragmentCallN64Depth;
		ShaderPartPtr m_fragmentRenderTarget;
		ShaderPartPtr m_shaderFragmentMainEnd;

		ShaderPartPtr m_shaderNoise;
		ShaderPartPtr m_shaderDither;
		ShaderPartPtr m_shaderWriteDepth;
		ShaderPartPtr m_shaderMipmap;
		ShaderPartPtr m_shaderCalcLight;
		ShaderPartPtr m_shaderReadtex;
		ShaderPartPtr m_shaderReadtexCopyMode;
		ShaderPartPtr m_shaderN64DepthCompare;
		ShaderPartPtr m_shaderN64DepthRender;

		std::unique_ptr<CombinerProgramUniformFactory> m_uniformFactory;

		GLuint  m_vertexShaderRect;
		GLuint  m_vertexShaderTriangle;
		GLuint  m_vertexShaderTexturedRect;
		GLuint  m_vertexShaderTexturedTriangle;
		opengl::CachedUseProgram * m_useProgram;
		u32 m_combinerOptionsBits;
	};

}
