#include "N64.h"
#include "Config.h"
#include "gli_framebuffer.h"
#include "PostProcessor.h"

static struct gli_shader * l_gammaCorrectionProgram;
static struct gli_shader * l_FXAAProgram;
static struct FrameBuffer * l_pResultBuffer;
static bool l_doFXAA;

void gli_pp_init(void)
{
	l_gammaCorrectionProgram = gli_gfx_create_gamma_shader();
	l_doFXAA = false;
	if (config.video.fxaa != 0) {
		l_FXAAProgram = gli_gfx_create_fxaa_shader();
		l_doFXAA = true;
	}
}

void gli_pp_destroy(void)
{
	l_doFXAA = false;
	gli_gfx_delete_shader(l_gammaCorrectionProgram);
	l_gammaCorrectionProgram = NULL;
	gli_gfx_delete_shader(l_FXAAProgram);
	l_FXAAProgram = NULL;
	gli_fb_delete(l_pResultBuffer);
	l_pResultBuffer = NULL;
}

static struct FrameBuffer * _doPostProcessing(struct FrameBuffer * _pBuffer, struct gli_shader * _pShader)
{
	if (l_pResultBuffer == NULL || gli_fb_width(l_pResultBuffer) != gli_fb_width(_pBuffer)) {
		struct FrameBuffer * pResult = gli_fb_pp_create(_pBuffer);
		gli_fb_delete(l_pResultBuffer);
		l_pResultBuffer = pResult;
	}
	gli_fb_pp_draw(_pBuffer, l_pResultBuffer, _pShader);
	return l_pResultBuffer;
}

static struct FrameBuffer * _doGammaCorrection(struct FrameBuffer * _pBuffer)
{
	if (_pBuffer == NULL)
		return NULL;

	if (((*REG.VI_STATUS & 8) | config.gammaCorrection.force) == 0)
		return _pBuffer;

	return _doPostProcessing(_pBuffer, l_gammaCorrectionProgram);
}

static struct FrameBuffer * _doFXAA(struct FrameBuffer * _pBuffer)
{
	if (_pBuffer == NULL)
		return NULL;

	if (config.video.fxaa == 0)
		return _pBuffer;

	return _doPostProcessing(_pBuffer, l_FXAAProgram);
}

struct FrameBuffer * gli_pp_run(struct FrameBuffer * _pBuffer)
{
	if (l_gammaCorrectionProgram == NULL)
		return _pBuffer;
	if (l_doFXAA)
		_pBuffer = _doFXAA(_pBuffer);
	return _doGammaCorrection(_pBuffer);
}
