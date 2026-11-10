#ifndef COMBINERKEY_H
#define COMBINERKEY_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The combiner key: the mux with the mode flags in muxs0's high byte */
u64 gli_combiner_key(u64 _mux);
u32 gli_combiner_key_cycle_type(u64 _key);
u32 gli_combiner_key_bilerp(u64 _key);
bool gli_combiner_key_is_rect(u64 _key);
bool gli_combiner_key_hwl(u64 _key);

/* Combiner.cpp */
bool gli_combiner_is_rect_mode(void);

#ifdef __cplusplus
}

#include <istream>

class CombinerKey {
public:
	CombinerKey() : m_mux(0) {}
	explicit CombinerKey(u64 _mux, bool _setModeBits = true)
		: m_mux(_setModeBits ? gli_combiner_key(_mux) : _mux) {}
	CombinerKey(const CombinerKey & _other) : m_mux(_other.m_mux) {}

	void operator=(u64 _mux) { m_mux = _mux; }
	void operator=(const CombinerKey & _other) { m_mux = _other.m_mux; }

	bool operator==(const CombinerKey & _other) const { return m_mux == _other.m_mux; }
	bool operator<(const CombinerKey & _other) const { return m_mux < _other.m_mux; }

	bool isRectKey() const { return gli_combiner_key_is_rect(m_mux); }
	bool isHWLSupported() const { return gli_combiner_key_hwl(m_mux); }
	u32 getCycleType() const { return gli_combiner_key_cycle_type(m_mux); }
	u32 getBilerp() const { return gli_combiner_key_bilerp(m_mux); }
	u64 getMux() const { return m_mux; }

	void read(std::istream & _is) { _is.read(reinterpret_cast<char*>(&m_mux), sizeof(m_mux)); }

	static const CombinerKey & getEmpty()
	{
		static CombinerKey emptyKey;
		return emptyKey;
	}

private:
	u64 m_mux;
};
#endif

#endif /* COMBINERKEY_H */
