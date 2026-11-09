#include <string.h>

#include "StereoFrames.h"
#include "gli_framebuffer.h"

/* A frame has a handful of color images; past that the oldest slot goes */
#define STEREO_FRAMES_SLOTS 16

#define STEREO_FRAME_CAPTURED      0x1U  /* the copy holds this buffer's left eye */
#define STEREO_FRAME_CAPTURED_TASK 0x2U  /* made by this task's left pass */
#define STEREO_FRAME_ENTERED_PASS  0x4U  /* entered during this left pass */

struct StereoFrameSlot
{
	struct FrameBuffer * pLeftEye;
	u32 address;
	u32 flags;
};

static struct StereoFrameSlot l_slots[STEREO_FRAMES_SLOTS];
static u32 l_slotCount = 0;
static u32 l_slotNext = 0;

bool g_stereoFramesCapturing = false;

static
struct StereoFrameSlot * _findSlot(u32 _address)
{
	u32 i;
	for (i = 0; i < l_slotCount; ++i) {
		if (l_slots[i].address == _address)
			return &l_slots[i];
	}
	return NULL;
}

static
struct StereoFrameSlot * _getSlot(u32 _address)
{
	struct StereoFrameSlot * pSlot = _findSlot(_address);
	if (pSlot != NULL)
		return pSlot;
	if (l_slotCount < STEREO_FRAMES_SLOTS) {
		pSlot = &l_slots[l_slotCount++];
	} else {
		pSlot = &l_slots[l_slotNext];
		l_slotNext = (l_slotNext + 1) % STEREO_FRAMES_SLOTS;
		gli_fb_stereo_delete(pSlot->pLeftEye);
	}
	pSlot->pLeftEye = NULL;
	pSlot->address = _address;
	pSlot->flags = 0;
	return pSlot;
}

void gli_StereoFrames_beginTask(void)
{
	u32 i;
	for (i = 0; i < l_slotCount; ++i)
		l_slots[i].flags &= ~STEREO_FRAME_CAPTURED_TASK;
}

void gli_StereoFrames_setCapturing(bool _capturing)
{
	u32 i;
	g_stereoFramesCapturing = _capturing;
	for (i = 0; i < l_slotCount; ++i)
		l_slots[i].flags &= ~STEREO_FRAME_ENTERED_PASS;
}

void gli_StereoFrames_noteEntered(u32 _address)
{
	struct StereoFrameSlot * pSlot;
	if (g_stereoFramesCapturing) {
		_getSlot(_address)->flags |= STEREO_FRAME_ENTERED_PASS;
		return;
	}
	pSlot = _findSlot(_address);
	if (pSlot != NULL && (pSlot->flags & STEREO_FRAME_CAPTURED_TASK) == 0)
		pSlot->flags &= ~STEREO_FRAME_CAPTURED;
}

bool gli_StereoFrames_wasEntered(u32 _address)
{
	const struct StereoFrameSlot * pSlot = _findSlot(_address);
	return pSlot != NULL && (pSlot->flags & STEREO_FRAME_ENTERED_PASS) != 0;
}

void gli_StereoFrames_captureLeftEye(struct FrameBuffer * _pBuffer)
{
	struct StereoFrameSlot * pSlot;

	if (_pBuffer == NULL || !gli_fb_has_texture(_pBuffer))
		return;

	pSlot = _getSlot(gli_fb_start_address(_pBuffer));
	if (gli_fb_stereo_copy(_pBuffer, &pSlot->pLeftEye))
		pSlot->flags |= STEREO_FRAME_CAPTURED | STEREO_FRAME_CAPTURED_TASK;
	else
		pSlot->flags &= ~(STEREO_FRAME_CAPTURED | STEREO_FRAME_CAPTURED_TASK);
}

struct FrameBuffer * gli_StereoFrames_leftEye(const struct FrameBuffer * _pBuffer)
{
	const struct StereoFrameSlot * pSlot;
	if (_pBuffer == NULL)
		return NULL;
	pSlot = _findSlot(gli_fb_start_address(_pBuffer));
	return pSlot != NULL && (pSlot->flags & STEREO_FRAME_CAPTURED) != 0 ? pSlot->pLeftEye : NULL;
}

void gli_StereoFrames_forget(const struct FrameBuffer * _pBuffer)
{
	struct StereoFrameSlot * pSlot;
	if (_pBuffer == NULL)
		return;
	pSlot = _findSlot(gli_fb_start_address(_pBuffer));
	if (pSlot != NULL)
		pSlot->flags &= ~(STEREO_FRAME_CAPTURED | STEREO_FRAME_CAPTURED_TASK);
}

void gli_StereoFrames_destroy(void)
{
	u32 i;
	for (i = 0; i < l_slotCount; ++i)
		gli_fb_stereo_delete(l_slots[i].pLeftEye);
	memset(l_slots, 0, sizeof(l_slots));
	l_slotCount = 0;
	l_slotNext = 0;
	g_stereoFramesCapturing = false;
}
