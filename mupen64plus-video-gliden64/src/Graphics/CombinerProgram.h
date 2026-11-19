#pragma once
#include "CombinerKey.h"
#include "gli_combiner.h"

namespace graphics {

	template <class T> struct CombinerProgramOps;

	/* A program, and its C face (gli_cprog, gli_combiner.h): its class's
	 * table and its key. A class names its table and key at construction;
	 * one that does not has its virtual functions called. */
	class CombinerProgram : public gli_cprog
	{
	public:
		explicit CombinerProgram(const gli_cprog_ops * _ops = nullptr, u64 _key = 0);
		virtual ~CombinerProgram() {}

		virtual void activate() = 0;
		virtual void update(bool _force) = 0;
		virtual const CombinerKey & getKey() const = 0;
		virtual bool usesTexture() const = 0;
		virtual bool usesTile(u32 _t) const = 0;
		virtual bool usesShade() const = 0;
		virtual bool usesLOD() const = 0;
		virtual bool usesHwLighting() const = 0;
	};

	/* a program from its C face */
	inline CombinerProgram * combinerProgram(gli_cprog * _p) { return static_cast<CombinerProgram *>(_p); }

	/* @T's table: its own functions called by name, no virtual call left */
	template <class T>
	struct CombinerProgramOps
	{
		static T * I(void * _p) { return static_cast<T *>(static_cast<CombinerProgram *>(static_cast<gli_cprog *>(_p))); }
		static const T * C(const void * _p)
		{
			return static_cast<const T *>(static_cast<const CombinerProgram *>(static_cast<const gli_cprog *>(_p)));
		}
		static void activate(void * _p) { I(_p)->T::activate(); }
		static void update(void * _p, bool _force) { I(_p)->T::update(_force); }
		static bool usesLOD(const void * _p) { return C(_p)->T::usesLOD(); }
		static void free_program(void * _p) { delete I(_p); }
		static bool usesTexture(const void * _p) { return C(_p)->T::usesTexture(); }
		static bool usesTile(const void * _p, u32 _t) { return C(_p)->T::usesTile(_t); }
		static bool usesShade(const void * _p) { return C(_p)->T::usesShade(); }
		static bool usesHwLighting(const void * _p) { return C(_p)->T::usesHwLighting(); }
		static const gli_cprog_ops table;
	};

	template <class T>
	const gli_cprog_ops CombinerProgramOps<T>::table = { &activate, &update, &usesLOD, &free_program,
		&usesTexture, &usesTile, &usesShade, &usesHwLighting, nullptr, nullptr };

	/* for a class without a table of its own: its virtual functions */
	struct VirtualCombinerProgramOps
	{
		static CombinerProgram * I(void * _p) { return static_cast<CombinerProgram *>(static_cast<gli_cprog *>(_p)); }
		static const CombinerProgram * C(const void * _p) { return static_cast<const CombinerProgram *>(static_cast<const gli_cprog *>(_p)); }
		static void activate(void * _p) { I(_p)->activate(); }
		static void update(void * _p, bool _force) { I(_p)->update(_force); }
		static bool usesLOD(const void * _p) { return C(_p)->usesLOD(); }
		static void free_program(void * _p) { delete I(_p); }
		static bool usesTexture(const void * _p) { return C(_p)->usesTexture(); }
		static bool usesTile(const void * _p, u32 _t) { return C(_p)->usesTile(_t); }
		static bool usesShade(const void * _p) { return C(_p)->usesShade(); }
		static bool usesHwLighting(const void * _p) { return C(_p)->usesHwLighting(); }
	};
#define GLI_VIRTUAL_CPROG_OPS &VirtualCombinerProgramOps::activate, &VirtualCombinerProgramOps::update, \
	&VirtualCombinerProgramOps::usesLOD, &VirtualCombinerProgramOps::free_program, \
	&VirtualCombinerProgramOps::usesTexture, &VirtualCombinerProgramOps::usesTile, \
	&VirtualCombinerProgramOps::usesShade, &VirtualCombinerProgramOps::usesHwLighting

	inline CombinerProgram::CombinerProgram(const gli_cprog_ops * _ops, u64 _key)
	{
		static const gli_cprog_ops virtualOps = { GLI_VIRTUAL_CPROG_OPS, nullptr, nullptr };
		ops = _ops != nullptr ? _ops : &virtualOps;
		key = _key;
	}
}
