#include <stdint.h>
#include <string.h>

#include "UniformSet.h"
#include "../Config.h"
#include "../Textures.h"

void UniformSet::bindWithShaderCombiner(ShaderCombiner * _pCombiner)
{
	const uint64_t mux   = _pCombiner->getMux();
	const GLuint program = _pCombiner->m_program;

	m_uniforms.emplace(mux, program);

	UniformSetLocation & location = m_uniforms.at(mux);

	/* Texture parameters */
	if (_pCombiner->usesTexture())
   {
		location.uTexScale.loc           = glGetUniformLocation(program, "uTexScale");
		location.uTexOffset[0].loc       = glGetUniformLocation(program, "uTexOffset[0]");
		location.uTexOffset[1].loc       = glGetUniformLocation(program, "uTexOffset[1]");
		location.uCacheScale[0].loc      = glGetUniformLocation(program, "uCacheScale[0]");
		location.uCacheScale[1].loc      = glGetUniformLocation(program, "uCacheScale[1]");
		location.uCacheOffset[0].loc     = glGetUniformLocation(program, "uCacheOffset[0]");
		location.uCacheOffset[1].loc     = glGetUniformLocation(program, "uCacheOffset[1]");
		location.uCacheShiftScale[0].loc = glGetUniformLocation(program, "uCacheShiftScale[0]");
		location.uCacheShiftScale[1].loc = glGetUniformLocation(program, "uCacheShiftScale[1]");
		location.uCacheFrameBuffer.loc   = glGetUniformLocation(program, "uCacheFrameBuffer");
		location.uTextureSize[0].loc     = glGetUniformLocation(program, "uTextureSize[0]");
		location.uTextureSize[1].loc     = glGetUniformLocation(program, "uTextureSize[1]");

		_updateTextureUniforms(location, _pCombiner->usesTile(0), _pCombiner->usesTile(1), true);
	}

	/* Colors */
   location.uFogColor.loc              = glGetUniformLocation(program, "uFogColor");
   location.uCenterColor.loc           = glGetUniformLocation(program, "uCenterColor");
   location.uScaleColor.loc            = glGetUniformLocation(program, "uScaleColor");
   location.uBlendColor.loc            = glGetUniformLocation(program, "uBlendColor");
	location.uEnvColor.loc              = glGetUniformLocation(program, "uEnvColor");
	location.uPrimColor.loc             = glGetUniformLocation(program, "uPrimColor");
	location.uPrimLod.loc               = glGetUniformLocation(program, "uPrimLod");
	location.uK4.loc                    = glGetUniformLocation(program, "uK4");
	location.uK5.loc                    = glGetUniformLocation(program, "uK5");
	_updateColorUniforms(location, true);

	/* Lights */
	if (config.generalEmulation.enableHWLighting != 0 && GBI.isHWLSupported() && _pCombiner->usesShadeColor())
   {
		/* locate lights uniforms */
		char buf[32];
		for (int32_t i = 0; i < 8; ++i)
      {
			sprintf(buf, "uLightDirection[%d]", i);
			location.uLightDirection[i].loc = glGetUniformLocation(program, buf);
			sprintf(buf, "uLightColor[%d]", i);
			location.uLightColor[i].loc     = glGetUniformLocation(program, buf);
		}
		_updateLightUniforms(location, true);
	}
}

void UniformSet::_updateColorUniforms(UniformSetLocation & _location, bool _bForce)
{
	_location.uFogColor.set(&gDP.fogColor.r, _bForce);
	_location.uCenterColor.set(&gDP.key.center.r, _bForce);
	_location.uScaleColor.set(&gDP.key.scale.r, _bForce);
	_location.uBlendColor.set(&gDP.blendColor.r, _bForce);
	_location.uEnvColor.set(&gDP.envColor.r, _bForce);
	_location.uPrimColor.set(&gDP.primColor.r, _bForce);
	_location.uPrimLod.set(gDP.primColor.l, _bForce);
	_location.uK4.set(gDP.convert.k4*0.0039215689f, _bForce);
	_location.uK5.set(gDP.convert.k5*0.0039215689f, _bForce);
}


void UniformSet::_updateTextureUniforms(UniformSetLocation & _location, bool _bUsesT0, bool _bUsesT1, bool _bForce)
{
   uint32_t t;
	int nFB[2] = { 0, 0 };
	const bool bUsesTile[2] = { _bUsesT0, _bUsesT1 };
	TextureCache & cache = textureCache();

	for (t = 0; t < 2; ++t)
   {
      if (!bUsesTile[t])
         continue;

      if (gSP.textureTile[t] != NULL)
      {
         if (     gSP.textureTile[t]->textureMode == TEXTUREMODE_BGIMAGE 
               || gSP.textureTile[t]->textureMode == TEXTUREMODE_FRAMEBUFFER_BG)
            _location.uTexOffset[t].set(0.0f, 0.0f, _bForce);
         else
         {
            float fuls = gSP.textureTile[t]->fuls;
            float fult = gSP.textureTile[t]->fult;
            FrameBuffer * pBuffer = gSP.textureTile[t]->frameBuffer;
            if (pBuffer != NULL)
            {
               if (gSP.textureTile[t]->masks > 0 && gSP.textureTile[t]->clamps == 0)
                  fuls = float(gSP.textureTile[t]->uls % (1 << gSP.textureTile[t]->masks));
               if (gSP.textureTile[t]->maskt > 0 && gSP.textureTile[t]->clampt == 0)
                  fult = float(gSP.textureTile[t]->ult % (1 << gSP.textureTile[t]->maskt));
            }
            _location.uTexOffset[t].set(fuls, fult, _bForce);
         }
      }

      if (cache.current[t] != NULL)
      {
         float shiftScaleS = 1.0f;
         float shiftScaleT = 1.0f;
         getTextureShiftScale(t, cache, shiftScaleS, shiftScaleT);
         _location.uCacheShiftScale[t].set(shiftScaleS, shiftScaleT, _bForce);
         _location.uCacheScale[t].set(cache.current[t]->scaleS, cache.current[t]->scaleT, _bForce);
         _location.uCacheOffset[t].set(cache.current[t]->offsetS, cache.current[t]->offsetT, _bForce);
         nFB[t] = cache.current[t]->frameBufferTexture;
      }
   }

	_location.uCacheFrameBuffer.set(nFB[0], nFB[1], _bForce);
	_location.uTexScale.set(gSP.texture.scales, gSP.texture.scalet, _bForce);
}

void UniformSet::_updateTextureSize(UniformSetLocation & _location, bool _bUsesT0, bool _bUsesT1, bool _bForce)
{
	TextureCache & cache = textureCache();
	if (_bUsesT0 && cache.current[0] != NULL)
		_location.uTextureSize[0].set((float)cache.current[0]->realWidth, (float)cache.current[0]->realHeight, _bForce);
	if (_bUsesT1 && cache.current[1] != NULL)
		_location.uTextureSize[1].set((float)cache.current[1]->realWidth, (float)cache.current[1]->realHeight, _bForce);
}

void UniformSet::_updateLightUniforms(UniformSetLocation & _location, bool _bForce)
{
   uint32_t i;
	for (i = 0; i <= gSP.numLights; ++i)
   {
		_location.uLightDirection[i].set(&gSP.lights[i].x, _bForce);
		_location.uLightColor[i].set(&gSP.lights[i].r, _bForce);
	}
}

void UniformSet::updateUniforms(ShaderCombiner * _pCombiner, OGLRender::RENDER_STATE _renderState)
{
	UniformSetLocation & location = m_uniforms.at(_pCombiner->getMux());

	_updateColorUniforms(location, false);

	if ((
            _renderState == OGLRender::rsTriangle || 
            _renderState == OGLRender::rsLine)
         && _pCombiner->usesTexture())
		_updateTextureUniforms(location, _pCombiner->usesTile(0), _pCombiner->usesTile(1), false);

	if (_pCombiner->usesTexture())
		_updateTextureSize(location, _pCombiner->usesTile(0), _pCombiner->usesTile(1), false);

	if (config.generalEmulation.enableHWLighting != 0 && GBI.isHWLSupported() && _pCombiner->usesShadeColor())
		_updateLightUniforms(location, false);
}

UniformCollection * createUniformCollection()
{
	return new UniformSet();
}
