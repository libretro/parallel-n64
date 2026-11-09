#ifndef STEREO_FRAMES_H
#define STEREO_FRAMES_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FrameBuffer;

/* The left eye copies of the N64 color images a task drew for both eyes.
 * The right eye is the buffer itself. */

/* True during the left eye pass */
extern bool g_stereoFramesCapturing;

/* A task drawn for both eyes starts: no left eye of it is kept yet */
void gli_StereoFrames_beginTask(void);

void gli_StereoFrames_setCapturing(bool _capturing);

/* A buffer drawn into again without its left eye kept by this task loses the old one */
void gli_StereoFrames_noteEntered(u32 _address);

/* Entered during this left eye pass */
bool gli_StereoFrames_wasEntered(u32 _address);

/* Keeps a copy of the buffer as its left eye */
void gli_StereoFrames_captureLeftEye(struct FrameBuffer * _pBuffer);

/* The kept copy belonging to this N64 frame buffer, or NULL */
struct FrameBuffer * gli_StereoFrames_leftEye(const struct FrameBuffer * _pBuffer);

/* Drops the kept copy, so the buffer is shown flat */
void gli_StereoFrames_forget(const struct FrameBuffer * _pBuffer);

void gli_StereoFrames_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* STEREO_FRAMES_H */
