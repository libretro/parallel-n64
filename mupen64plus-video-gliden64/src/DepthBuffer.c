#include "Config.h"
#include "VI.h"
#include "gDP.h"
#include "gli_framebuffer.h"
#include "DepthBuffer.h"

/* More than a game has at once: past that the oldest goes */
#define MAX_DEPTH_BUFFERS GLI_DEPTH_MAX
#define ZLUT_SIZE 0x40000

static struct DepthBuffer * l_list[MAX_DEPTH_BUFFERS];
static u32 l_count;
static struct DepthBuffer * l_pCurrent;
static u16 l_zLUT[ZLUT_SIZE];
static bool l_zLUTMade;

const u16 * gli_depth_zlut(void)
{
	u32 i;
	if (l_zLUTMade)
		return l_zLUT;
	for (i = 0; i < ZLUT_SIZE; i++) {
		u32 exponent = 0;
		u32 testbit = 1 << 17;
		u32 mantissa;
		while ((i & testbit) && (exponent < 7)) {
			exponent++;
			testbit = 1 << (17 - exponent);
		}

		mantissa = (i >> (6 - (6 < exponent ? 6 : exponent))) & 0x7ff;
		l_zLUT[i] = (u16)(((exponent << 11) | mantissa) << 2);
	}
	l_zLUTMade = true;
	return l_zLUT;
}

void gli_depth_list_init(void)
{
	gli_depth_zlut();
	l_pCurrent = NULL;
}

void gli_depth_list_destroy(void)
{
	u32 i;
	l_pCurrent = NULL;
	for (i = 0; i < l_count; i++)
		gli_depth_delete(l_list[i]);
	l_count = 0;
}

void gli_depth_set_cleared(bool _cleared)
{
	u32 i;
	for (i = 0; i < l_count; i++)
		gli_depth_mark_cleared(l_list[i], _cleared);
}

struct DepthBuffer * gli_depth_find(u32 _address)
{
	u32 i = l_count;
	while (i > 0) {
		--i;
		if (gli_depth_address(l_list[i]) == _address)
			return l_list[i];
	}
	return NULL;
}

struct DepthBuffer * gli_depth_current_buffer(void)
{
	return l_pCurrent;
}

static void _removeAt(u32 _i)
{
	u32 i;
	gli_fb_clear_depth_buffer(l_list[_i]);
	gli_depth_delete(l_list[_i]);
	for (i = _i + 1; i < l_count; i++)
		l_list[i - 1] = l_list[i];
	--l_count;
}

static void _removeBuffer(u32 _address)
{
	u32 i = l_count;
	while (i > 0) {
		--i;
		if (gli_depth_address(l_list[i]) == _address) {
			_removeAt(i);
			return;
		}
	}
}

static struct DepthBuffer * _addBuffer(u32 _address, struct FrameBuffer * _pBuffer)
{
	struct DepthBuffer * pBuffer;
	if (l_count == MAX_DEPTH_BUFFERS) {
		if (l_list[0] == l_pCurrent)
			l_pCurrent = NULL;
		_removeAt(0);
	}
	pBuffer = gli_depth_new(_address, _pBuffer);
	l_list[l_count++] = pBuffer;
	return pBuffer;
}

static void _createScreenSizeBuffer(u32 _address)
{
	struct FrameBuffer * pFrameBuffer = gli_fb_find(VI.width * 2);
	if (pFrameBuffer == NULL)
		return;

	l_pCurrent = _addBuffer(_address, pFrameBuffer);
	gli_fb_attach_depth_buffer();
	l_pCurrent = NULL;
}

void gli_depth_save(u32 _address)
{
	struct FrameBuffer * pFrameBuffer;
	struct DepthBuffer * pDepthBuffer = NULL;
	struct DepthBuffer * pCurrent;

	if (config.frameBufferEmulation.enable == 0) {
		if (l_count == 0)
			_createScreenSizeBuffer(_address);
		return;
	}

	pFrameBuffer = gli_fb_find(_address);
	if (pFrameBuffer != NULL)
		gli_fb_set_is_depth(pFrameBuffer);

	if (pFrameBuffer != NULL && gli_fb_start_address(pFrameBuffer) != _address)
		pDepthBuffer = gli_depth_find(gli_fb_start_address(pFrameBuffer));
	else
		pDepthBuffer = gli_depth_find(_address);

	if (pDepthBuffer != NULL && pFrameBuffer != NULL && gli_depth_width(pDepthBuffer) != gli_fb_width(pFrameBuffer)) {
		_removeBuffer(_address);
		pDepthBuffer = NULL;
	}

	if (pDepthBuffer == NULL)
		pDepthBuffer = _addBuffer(_address, pFrameBuffer);

	pCurrent = l_pCurrent;
	l_pCurrent = pDepthBuffer;
	gli_fb_attach_depth_buffer();
	if (pFrameBuffer == NULL && (config.generalEmulation.hacks & hack_clearAloneDepthBuffer) != 0)
		gli_depth_clear();
	if (gli_depth_address(pDepthBuffer) != gDP.depthImageAddress)
		l_pCurrent = pCurrent;
}

void gli_depth_clear(void)
{
	if (l_pCurrent != NULL)
		gli_depth_mark_cleared(l_pCurrent, true);
	gli_depth_gpu_clear();
}

void gli_depth_set_image(u32 _address)
{
	gli_depth_save(_address);
}

bool gli_depthbuffer_software_target(const u16 ** _zLUT, u32 * _width)
{
	/* Current depth buffer can be null if we are loading from a save state */
	if (l_pCurrent == NULL || config.frameBufferEmulation.copyDepthToRDRAM != cdSoftwareRender)
		return false;
	*_zLUT = gli_depth_zlut();
	*_width = gli_depth_width(l_pCurrent);
	return true;
}
