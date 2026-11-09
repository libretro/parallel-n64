#pragma once
#include <memory>
#include <vector>
#include <Graphics/CombinerProgram.h>
#include <Graphics/CombinerValues.h>
#include <Graphics/ObjectHandle.h>
#include <Graphics/OpenGLContext/GLFunctions.h>
#include "glsl_CombinerInputs.h"

namespace opengl {
	class CachedUseProgram;
}

namespace glsl {

	/* A program's uniforms: this combiner's values, and what of them the
	 * program has, each sent only when it differs from what it last got. */
	class CombinerUniforms
	{
	public:
		CombinerUniforms() : m_groups(0), m_values(), m_uploaded() {}
		void update(bool _force);

		struct Entry {
			GLint loc;
			u16 offset;
			u8 kind;
			u8 bytes;
		};
		struct Sampler {
			GLint loc;
			GLint unit;
		};
		void _add(GLint _loc, int _kind, u32 _offset);

		u32 m_groups;
		std::vector<Entry> m_entries;
		std::vector<Sampler> m_samplers;
		CombinerValues m_values;
		CombinerValues m_uploaded;
	};

	class CombinerProgramImpl : public graphics::CombinerProgram
	{
	public:
		CombinerProgramImpl(const CombinerKey & _key,
			GLuint _program,
			opengl::CachedUseProgram * _useProgram,
			const CombinerInputs & _inputs,
			CombinerUniforms && _uniforms);
		~CombinerProgramImpl();

		void activate() override;
		void update(bool _force) override;
		const CombinerKey & getKey() const override;

		bool usesTexture() const override;
		bool usesTile(u32 _t) const override;
		bool usesShade() const override;
		bool usesLOD() const override;
		bool usesHwLighting() const override;

		bool getBinaryForm(std::vector<char> & _buffer) override;

	private:
		bool m_bNeedUpdate;
		CombinerKey m_key;
		graphics::ObjectHandle m_program;
		opengl::CachedUseProgram * m_useProgram;
		CombinerInputs m_inputs;
		CombinerUniforms m_uniforms;
	};

}
