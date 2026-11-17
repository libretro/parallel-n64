#pragma once
#include <Graphics/OpenGLContext/opengl_GLInfo.h>

namespace opengl {
	class CachedUseProgram;
}

namespace glsl {

	class ShaderStorage
	{
	public:
		ShaderStorage(const opengl::GLInfo & _glinfo, opengl::CachedUseProgram * _useProgram);

		bool saveShadersStorage(const gli_combiner_table * _combiners) const;

		bool loadShadersStorage(gli_combiner_table * _combiners);

		std::string getKeysJournalPath(u32 & _version) const;

	private:
		bool _saveCombinerKeys(const gli_combiner_table * _combiners) const;
		bool _loadFromCombinerKeys(gli_combiner_table * _combiners);

		const u32 m_formatVersion = 0x3EU;
		const u32 m_keysFormatVersion = 0x05;
		const opengl::GLInfo & m_glinfo;
		opengl::CachedUseProgram * m_useProgram;
	};

}
