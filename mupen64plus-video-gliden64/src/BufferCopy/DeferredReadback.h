#ifndef DeferredReadback_H
#define DeferredReadback_H

#include "../Types.h"

/* The order of the asynchronous colour copies to RDRAM. Each read goes
 * into the next of a ring of slots and keeps what it was read for, so it
 * lands where it was meant to go. A read is written to RDRAM at a later
 * copy once the GPU has finished it, oldest first. The copy waits only
 * when the slot it is about to reuse is still in flight, i.e. when the GPU
 * is a whole ring of copies behind. */
template <typename Geometry, u32 MaxSlots = 3>
class DeferredReadback
{
public:
	struct Ops
	{
		virtual bool ready(u32 _slot, bool _wait) = 0;
		virtual void write(u32 _slot, const Geometry & _geometry) = 0;
	protected:
		~Ops() {}
	};

	DeferredReadback() { reset(1); }

	void reset(u32 _slots)
	{
		m_slots = _slots == 0 ? 1 : (_slots > MaxSlots ? MaxSlots : _slots);
		m_next = 0;
		for (u32 i = 0; i < MaxSlots; ++i)
			m_pending[i] = false;
	}

	/* Writes what has finished and hands back the slot to read into. */
	u32 begin(Ops & _ops)
	{
		for (u32 i = 0; i < m_slots; ++i) {
			const u32 slot = (m_next + i) % m_slots;
			if (!m_pending[slot])
				continue;
			if (!_ops.ready(slot, false))
				break;
			_retire(_ops, slot);
		}
		if (m_pending[m_next]) {
			_ops.ready(m_next, true);
			_retire(_ops, m_next);
		}
		return m_next;
	}

	/* The read begin() handed out is on its way. */
	void issued(const Geometry & _geometry)
	{
		m_geometry[m_next] = _geometry;
		m_pending[m_next] = true;
		m_next = (m_next + 1) % m_slots;
	}

	/* Writes every read in flight, waiting for each; the next slot is
	 * free afterwards, for a read that is waited for at once. */
	u32 drain(Ops & _ops)
	{
		for (u32 i = 0; i < m_slots; ++i) {
			const u32 slot = (m_next + i) % m_slots;
			if (m_pending[slot]) {
				_ops.ready(slot, true);
				_retire(_ops, slot);
			}
		}
		return m_next;
	}

	bool pending(u32 _slot) const { return m_pending[_slot]; }

private:
	void _retire(Ops & _ops, u32 _slot)
	{
		m_pending[_slot] = false;
		_ops.write(_slot, m_geometry[_slot]);
	}

	u32 m_slots;
	u32 m_next;
	bool m_pending[MaxSlots];
	Geometry m_geometry[MaxSlots];
};

#endif // DeferredReadback_H
