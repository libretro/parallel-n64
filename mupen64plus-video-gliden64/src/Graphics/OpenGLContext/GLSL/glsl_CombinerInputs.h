#pragma once
#include <Types.h>
#include <Graphics/CombinerStages.h>

namespace glsl {

	class CombinerInputs
	{
	public:
		CombinerInputs() : m_inputs(0) {}
		explicit CombinerInputs(int _inputs) : m_inputs(_inputs) {}
		CombinerInputs(const CombinerInputs & _other) : m_inputs(_other.m_inputs) {}

		explicit operator int() { return m_inputs; }

		/* CombinerStages.c's tests of its bits */
		bool usesTile(u32 _t) const { return gli_inputs_tile(u32(m_inputs), _t); }

		bool usesTexture() const { return gli_inputs_texture(u32(m_inputs)); }

		bool usesLOD() const { return gli_inputs_lod(u32(m_inputs)); }

		bool usesNoise() const { return gli_inputs_noise(u32(m_inputs)); }

		bool usesShade() const { return gli_inputs_shade(u32(m_inputs)); }

		bool usesShadeColor() const { return gli_inputs_shade_color(u32(m_inputs)); }

		bool usesHwLighting() const { return gli_inputs_hw_lighting(u32(m_inputs)); }

		void addInput(u32 _input) { m_inputs |= 1 << _input; }

		void operator+=(const CombinerInputs & _other) { m_inputs |= _other.m_inputs; }

	private:
		int m_inputs;
	};

}
