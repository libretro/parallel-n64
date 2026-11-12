#ifndef RDRAMtoColorBuffer_H
#define RDRAMtoColorBuffer_H

#include <Graphics/ObjectHandle.h>
#include <FrameBuffer.h>
#include <gli_rdram_to_color.h>

struct CachedTexture;
struct FrameBuffer;

class RDRAMtoColorBuffer
{
public:
	void init();
	void destroy();

	void addAddress(u32 _address, u32 _size);

	void copyFromRDRAM(u32 _address, bool _bCFB);
	void copyFromRDRAM(FrameBuffer * _pBuffer);

	static RDRAMtoColorBuffer & get();

private:
	RDRAMtoColorBuffer();
	RDRAMtoColorBuffer(const RDRAMtoColorBuffer &) = delete;

	void _copyFromRDRAM(u32 _height, bool _fullAlpha);
	void reset();

	class Cleaner
	{
	public:
		Cleaner(RDRAMtoColorBuffer * _p) : m_p(_p), m_pCurrentBuffer(frameBufferList().getCurrent()) {}
		~Cleaner()
		{
			m_p->reset();
			frameBufferList().setCurrent(m_pCurrentBuffer);
		}
	private:
		RDRAMtoColorBuffer * m_p;
		FrameBuffer * m_pCurrentBuffer;
	};

	FrameBuffer * m_pCurBuffer;
	CachedTexture * m_pTexture;
	/* the pixels the CPU wrote, when it wrote some rather than all */
	gli_r2c_marks m_marks;
	u8* m_pbuf;
};

#endif // RDRAMtoColorBuffer_H
