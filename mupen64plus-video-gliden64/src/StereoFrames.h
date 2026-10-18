#ifndef STEREO_FRAMES_H
#define STEREO_FRAMES_H

#include "Types.h"

struct FrameBuffer;

/* The left eye copies of the N64 color images a task drew for both eyes.
 * The right eye is the buffer itself. */

/* True during the left eye pass */
extern bool g_stereoFramesCapturing;

/* A task drawn for both eyes starts: no left eye of it is kept yet */
void StereoFrames_beginTask();

void StereoFrames_setCapturing(bool _capturing);

/* A buffer drawn into again without its left eye kept by this task loses the old one */
void StereoFrames_noteEntered(u32 _address);

/* Entered during this left eye pass */
bool StereoFrames_wasEntered(u32 _address);

/* Keeps a copy of the buffer as its left eye */
void StereoFrames_captureLeftEye(FrameBuffer * _pBuffer);

/* The kept copy belonging to this N64 frame buffer, or NULL */
FrameBuffer * StereoFrames_leftEye(const FrameBuffer * _pBuffer);

/* Drops the kept copy, so the buffer is shown flat */
void StereoFrames_forget(const FrameBuffer * _pBuffer);

void StereoFrames_destroy();

#endif /* STEREO_FRAMES_H */
