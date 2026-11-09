#pragma once
#include "FixedCellPool.h"
#include <functional>
#include <map>
#include <vector>
#include "CombinerKey.h"

namespace graphics {

	class CombinerProgram
	{
	public:
		virtual ~CombinerProgram() {}
		virtual void activate() = 0;
		virtual void update(bool _force) = 0;

		virtual const CombinerKey & getKey() const = 0;

		virtual bool usesTexture() const = 0;
		virtual bool usesTile(u32 _t) const = 0;
		virtual bool usesShade() const = 0;
		virtual bool usesLOD() const = 0;
		virtual bool usesHwLighting() const = 0;

		virtual bool getBinaryForm(std::vector<char> & _buffer) = 0;

		static u32 getShaderCombinerOptionsBits();
	};

	/* map nodes come from a fixed pool: a new combiner mid-game allocates nothing */
	template <class T>
	struct CombinerNodeAllocator
	{
		typedef T value_type;
		CombinerNodeAllocator() {}
		template <class U> CombinerNodeAllocator(const CombinerNodeAllocator<U> &) {}
		T * allocate(std::size_t _n)
		{
			return _n == 1 ? static_cast<T*>(pool().take()) : static_cast<T*>(::operator new(_n * sizeof(T)));
		}
		void deallocate(T * _p, std::size_t _n)
		{
			if (_n == 1)
				pool().give(_p);
			else
				::operator delete(_p);
		}
		template <class U> bool operator==(const CombinerNodeAllocator<U> &) const { return true; }
		template <class U> bool operator!=(const CombinerNodeAllocator<U> &) const { return false; }
	private:
		static FixedCellPool<sizeof(T), 8192> & pool()
		{
			static FixedCellPool<sizeof(T), 8192> p;
			return p;
		}
	};

	typedef std::map<CombinerKey, graphics::CombinerProgram *, std::less<CombinerKey>,
		CombinerNodeAllocator<std::pair<const CombinerKey, graphics::CombinerProgram *> > > Combiners;
}
