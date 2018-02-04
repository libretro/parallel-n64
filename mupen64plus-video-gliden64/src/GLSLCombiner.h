#ifndef GLSL_COMBINER_H
#define GLSL_COMBINER_H

#include <vector>
#include <iostream>
#include "gDP.h"
#include "Combiner.h"

class ShaderCombiner {
public:
	ShaderCombiner();
	ShaderCombiner(Combiner & _color, Combiner & _alpha, const gDPCombine & _combine);
	~ShaderCombiner();

	void update(bool _bForce);
	void updateFogMode(bool _bForce = false);
	void updateDitherMode(bool _bForce = false);
	void updateLOD(bool _bForce = false);
	void updateFBInfo(bool _bForce = false);
	void updateDepthInfo(bool _bForce = false);
	void updateAlphaTestInfo(bool _bForce = false);
	void updateTextureInfo(bool _bForce = false);
	void updateRenderState(bool _bForce = false);

	uint64_t getMux() const {return m_combine.mux;}

	bool usesTile(uint32_t _t) const {
		if (_t == 0)
			return (m_nInputs & ((1<<TEXEL0)|(1<<TEXEL0_ALPHA))) != 0;
		return (m_nInputs & ((1 << TEXEL1) | (1 << TEXEL1_ALPHA))) != 0;
	}
	bool usesTexture() const { return (m_nInputs & ((1 << TEXEL1)|(1 << TEXEL1_ALPHA)|(1 << TEXEL0)|(1 << TEXEL0_ALPHA))) != 0; }
	bool usesLOD() const { return (m_nInputs & (1 << LOD_FRACTION)) != 0; }
	bool usesShade() const { return (m_nInputs & ((1 << SHADE) | (1 << SHADE_ALPHA))) != 0; }
	bool usesShadeColor() const { return (m_nInputs & (1 << SHADE)) != 0; }

	friend std::ostream & operator<< (std::ostream & _os, const ShaderCombiner & _combiner);
	friend std::istream & operator>> (std::istream & _os, ShaderCombiner & _combiner);

	static void getShaderCombinerOptionsSet(std::vector<uint32_t> & _vecOptions);

private:
	friend class UniformBlock;
	friend class UniformSet;

	struct iUniform	{
		GLint loc;
		int val;
		void set(int _val, bool _force) {
			if (loc >= 0 && (_force || val != _val)) {
				val = _val;
				glUniform1i(loc, _val);
			}
		}
	};

	struct fUniform {
		GLint loc;
		float val;
		void set(float _val, bool _force) {
			if (loc >= 0 && (_force || val != _val)) {
				val = _val;
				glUniform1f(loc, _val);
			}
		}
	};

	struct fv2Uniform {
		GLint loc;
		float val[2];
		void set(float _val1, float _val2, bool _force) {
			if (loc >= 0 && (_force || val[0] != _val1 || val[1] != _val2)) {
				val[0] = _val1;
				val[1] = _val2;
				glUniform2f(loc, _val1, _val2);
			}
		}
	};

	struct iv2Uniform {
		GLint loc;
		int val[2];
		void set(int _val1, int _val2, bool _force) {
			if (loc >= 0 && (_force || val[0] != _val1 || val[1] != _val2)) {
				val[0] = _val1;
				val[1] = _val2;
				glUniform2i(loc, _val1, _val2);
			}
		}
	};

	struct UniformLocation
	{
		iUniform uTex0, uTex1, uMSTex0, uMSTex1, uTexNoise, uTlutImage, uZlutImage, uDepthImage,
			uFogMode, uFogUsage, uEnableLod, uEnableAlphaTest,
			uEnableDepth, uEnableDepthCompare, uEnableDepthUpdate,
			uDepthMode, uDepthSource, uRenderState, uSpecialBlendMode,
			uMaxTile, uTextureDetail, uTexturePersp, uTextureFilterMode, uMSAASamples,
			uAlphaCompareMode, uAlphaDitherMode, uColorDitherMode;

		fUniform uFogAlpha, uMinLod, uDeltaZ, uAlphaTestValue, uMSAAScale;

		fv2Uniform uScreenScale, uDepthScale, uFogScale;

		iv2Uniform uMSTexEnabled, uFbMonochrome, uFbFixedAlpha;
	};

	void _locate_attributes() const;
	void _locateUniforms();

	gDPCombine m_combine;
	UniformLocation m_uniforms;
	GLuint m_program;
	int m_nInputs;
	bool m_bNeedUpdate;
};

void InitShaderCombiner();
void DestroyShaderCombiner();

#ifdef GL_IMAGE_TEXTURES_SUPPORT
void SetDepthFogCombiner();
void SetMonochromeCombiner();
#endif // GL_IMAGE_TEXTURES_SUPPORT

//#define USE_TOONIFY

#endif //GLSL_COMBINER_H
