#ifndef FIXED_CELL_POOL_H
#define FIXED_CELL_POOL_H

#include <cstddef>
#include <new>

/* Up to Count blocks of at most Size bytes without touching the heap, for
 * what would otherwise allocate in the middle of a game. Render thread only.
 * Zero-initialised storage: declare it at namespace or static scope. */
template <std::size_t Size, std::size_t Count>
class FixedCellPool
{
public:
	void * take()
	{
		if (m_free != nullptr) {
			Cell * c = m_free;
			m_free = c->next;
			return c;
		}
		if (m_used < Count)
			return &m_cells[m_used++];
		return ::operator new(Size);
	}

	void give(void * _p)
	{
		Cell * c = static_cast<Cell*>(_p);
		if (_p == nullptr)
			return;
		if (c >= m_cells && c < m_cells + Count) {
			c->next = m_free;
			m_free = c;
		} else
			::operator delete(_p);
	}

private:
	union Cell
	{
		Cell * next;
		unsigned char bytes[Size];
		long double align;
	};

	Cell m_cells[Count];
	Cell * m_free;
	std::size_t m_used;
};

#endif
