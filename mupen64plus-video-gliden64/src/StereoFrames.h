#ifndef STEREO_FRAMES_H
#define STEREO_FRAMES_H

#include <map>
#include <memory>
#include <set>
#include "Types.h"

struct FrameBuffer;

class StereoFrames
{
public:
	static StereoFrames & get();

	/** Keeps a copy of the buffer. */
	void captureLeftEye(FrameBuffer * _pBuffer);

	/** True during the first stereoscopic frame. */
	void setCapturing(bool _capturing) { m_capturing = _capturing; m_enteredThisPass.clear(); }

	void noteEntered(u32 _address) { if (m_capturing) m_enteredThisPass.insert(_address); }
	bool wasEntered(u32 _address) const { return m_enteredThisPass.count(_address) != 0; }

	bool isCapturing() const { return m_capturing; }

	/** The kept copy belonging to this N64 frame buffer, or null if it was never captured. */
	FrameBuffer * leftEye(const FrameBuffer * _pBuffer) const;


	void destroy();

private:
	StereoFrames() = default;
	StereoFrames(const StereoFrames &) = delete;

	std::unique_ptr<FrameBuffer> _createBuffer(const FrameBuffer * _pMainBuffer);
	std::map<u32, std::unique_ptr<FrameBuffer>> m_leftEyes;
	// Addresses whose copy is valid.
	std::set<u32> m_captured;
	bool m_capturing = false;
	std::set<u32> m_enteredThisPass;
};

#endif // STEREO_FRAMES_H
