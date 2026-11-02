#pragma once
#include <Types.h>

namespace opengl {

/* Where the draw data goes in a persistently mapped ring: in Segments
 * parts, each fenced when the writes move on from it and waited for
 * before they come back to it. A wait holds only for draws a whole lap
 * behind, and the ring cannot overwrite what the GPU has yet to read. */
template <u32 Segments = 3>
class RingSegments
{
public:
	void init(u32 _size)
	{
		m_size = _size;
		m_segSize = _size / Segments;
		m_cur = 0;
		for (u32 i = 0; i < Segments; ++i)
			m_fenced[i] = false;
	}

	/* The offset for _bytes (at most a segment) at _offset, moved on to
	 * the next segment when they would straddle two, so each draw reads
	 * from one: ops.fence(seg) marks the draws so far when the writes
	 * leave a segment, ops.wait(seg) waits for that mark when they come
	 * back to it. */
	template <typename Ops>
	u32 place(u32 _offset, u32 _bytes, Ops & _ops)
	{
		u32 start = _offset;
		if (_seg(start) != _seg(start + _bytes - 1))
			start = (_seg(start) + 1) * m_segSize;
		if (start + _bytes > m_size || _seg(start) != _seg(start + _bytes - 1))
			start = 0;
		const u32 target = _seg(start);
		/* going round to an earlier segment, or to this one from its end */
		bool round = start < _offset && target == m_cur;
		while (m_cur != target || round) {
			_ops.fence(m_cur);
			m_fenced[m_cur] = true;
			m_cur = (m_cur + 1) % Segments;
			if (m_fenced[m_cur]) {
				_ops.wait(m_cur);
				m_fenced[m_cur] = false;
			}
			round = false;
		}
		return start;
	}

	u32 segment() const { return m_cur; }

private:
	u32 _seg(u32 _at) const
	{
		const u32 seg = _at / m_segSize;
		return seg < Segments ? seg : Segments - 1;
	}

	u32 m_size = 0;
	u32 m_segSize = 0;
	u32 m_cur = 0;
	bool m_fenced[Segments];
};

}
