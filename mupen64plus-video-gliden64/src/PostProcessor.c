#include "N64.h"
#include "Config.h"
#include "gli_framebuffer.h"
#include "PostProcessor.h"

static struct gli_shader * l_gammaCorrectionProgram;
static struct gli_shader * l_FXAAProgram;
/* two, so that a pass never reads the buffer it draws into */
static struct FrameBuffer * l_pResultBuffers[2];
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
	gli_fb_delete(l_pResultBuffers[0]);
	gli_fb_delete(l_pResultBuffers[1]);
	l_pResultBuffers[0] = l_pResultBuffers[1] = NULL;
}

static struct FrameBuffer * _doPostProcessing(struct FrameBuffer * _pBuffer, struct gli_shader * _pShader)
{
	struct FrameBuffer ** ppResult = &l_pResultBuffers[_pBuffer == l_pResultBuffers[0] ? 1 : 0];
	if (*ppResult == NULL || gli_fb_width(*ppResult) != gli_fb_width(_pBuffer)) {
		struct FrameBuffer * pResult = gli_fb_pp_create(_pBuffer);
		gli_fb_delete(*ppResult);
		*ppResult = pResult;
	}
	gli_fb_pp_draw(_pBuffer, *ppResult, _pShader);
	return *ppResult;
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
