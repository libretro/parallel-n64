#pragma once
#include <Types.h>
#include "CombinerProgram.h"

namespace graphics {

	class ShaderProgram : public CombinerProgram
	{
	public:
		explicit ShaderProgram(const gli_cprog_ops * _ops = nullptr) : CombinerProgram(_ops) {}
		virtual ~ShaderProgram() {}

		void update(bool _force) override {}
		const CombinerKey & getKey() const override { return CombinerKey::getEmpty(); }
		bool usesTexture() const override {return true;}
		virtual bool usesTile(u32 _t) const override {return _t == 0 ? true : false;}
		virtual bool usesShade() const override {return false;}
		virtual bool usesLOD() const override {return false;}
		virtual bool usesHwLighting() const override {return false;}
		virtual bool getBinaryForm(std::vector<char> & _buffer) override {return false;}
	};

	/* its C face sets its texture size and alpha test too */
	class TexrectDrawerShaderProgram : public ShaderProgram
	{
	public:
		TexrectDrawerShaderProgram() : ShaderProgram(&ops()) {}
		virtual void setTextureSize(u32 _width, u32 _height) = 0;
		virtual void setEnableAlphaTest(int _enable) = 0;

	private:
		static TexrectDrawerShaderProgram * I(void * _p)
		{
			return static_cast<TexrectDrawerShaderProgram *>(static_cast<CombinerProgram *>(static_cast<gli_cprog *>(_p)));
		}
		static void textureSize(void * _p, u32 _w, u32 _h) { I(_p)->setTextureSize(_w, _h); }
		static void alphaTest(void * _p, int _enable) { I(_p)->setEnableAlphaTest(_enable); }
		static const gli_cprog_ops & ops()
		{
			static const gli_cprog_ops table = { GLI_VIRTUAL_CPROG_OPS, &textureSize, &alphaTest };
			return table;
		}
	};
}
