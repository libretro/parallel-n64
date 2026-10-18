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

	/** A task drawn for both eyes starts: no left eye of it is kept yet. */
	void beginTask() { m_capturedThisTask.clear(); }

	/** True during the left eye pass. */
	void setCapturing(bool _capturing) { m_capturing = _capturing; m_enteredThisPass.clear(); }

	/** A buffer drawn into again without its left eye kept by this task loses the old one. */
	void noteEntered(u32 _address)
	{
		if (m_capturing)
			m_enteredThisPass.insert(_address);
		else if (!m_captured.empty() && m_capturedThisTask.count(_address) == 0)
			m_captured.erase(_address);
	}
	bool wasEntered(u32 _address) const { return m_enteredThisPass.count(_address) != 0; }

	/** Drops the kept copy, so the buffer is shown flat. */
	void forget(const FrameBuffer * _pBuffer);

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
	// Addresses whose copy this task's left pass made.
	std::set<u32> m_capturedThisTask;
	std::set<u32> m_enteredThisPass;
	bool m_capturing = false;
};

#endif // STEREO_FRAMES_H
