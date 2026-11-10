#include <string>
#include <stdlib.h>
#include <fstream>
#include <functional>
#include <cstring>
#include <stdio.h>
#include <osal_files.h>

#include "Combiner.h"
#include "CombinerKeyJournal.h"
#include "gDP.h"
#include "Config.h"
#include "PluginAPI.h"
#include "RSP.h"
#include "Graphics/Context.h"

using namespace graphics;

void Combiner_Init() {
	CombinerInfo & cmbInfo = CombinerInfo::get();
	cmbInfo.init();
	gDP.otherMode.cycleType = G_CYC_1CYCLE;
}

void Combiner_Destroy() {
	CombinerInfo::get().destroy();
}

/*---------------CombinerInfo-------------*/

CombinerInfo & CombinerInfo::get()
{
	static CombinerInfo info;
	return info;
}

void CombinerInfo::init()
{
	gfxContext.resetCombinerProgramBuilder();
	m_pCurrent = nullptr;

	m_shadersLoaded = 0;
	if (config.generalEmulation.enableShadersStorage != 0 && !_loadShadersStorage()) {
		for (auto cur = m_combiners.begin(); cur != m_combiners.end(); ++cur)
			delete cur->second;
		m_combiners.clear();
	}

	_buildFixedKeys();

	if (config.generalEmulation.enableShadersStorage != 0)
		_openKeysJournal();

	m_shadowmapProgram.reset(gfxContext.createDepthFogShader());
	m_texrectCopyProgram.reset(gfxContext.createTexrectCopyShader());
}

void CombinerInfo::destroy()
{
	m_shadowmapProgram.reset();
	m_texrectCopyProgram.reset();

	m_pCurrent = nullptr;
	if (config.generalEmulation.enableShadersStorage != 0)
		_closeKeysJournal(_saveShadersStorage());
	m_shadersLoaded = 0;
	for (auto cur = m_combiners.begin(); cur != m_combiners.end(); ++cur)
		delete cur->second;
	m_combiners.clear();
}

graphics::CombinerProgram * Combiner_Compile(CombinerKey key, bool _mayDefer)
{
	Combiner color, alpha;
	gli_combiner_decode(key.getMux(), key.getCycleType(), &color, &alpha);

	if (_mayDefer) {
		// The builder corrects the stages in place: on copies, for what may still compile below.
		Combiner deferredColor = color, deferredAlpha = alpha;
		graphics::CombinerProgram * program = gfxContext.createCombinerProgramAsync(deferredColor, deferredAlpha, key);
		if (program != nullptr)
			return program;
		static const bool strict = getenv("GLIDEN64_GL_STRICT") != nullptr;
		if (strict)
			abort();
	}

	return gfxContext.createCombinerProgram(color, alpha, key);
}

void CombinerInfo::update()
{
	// TODO: find, why gDP.changed & CHANGED_COMBINE not always works (e.g. Mario Tennis).
	// if (gDP.changed & CHANGED_COMBINE)
	{
		setCombine(gli_combiner_mux());
		gDP.changed &= ~CHANGED_COMBINE;
	}
}

void CombinerInfo::setCombine(u64 _mux )
{
	const CombinerKey key(_mux);
	if (m_pCurrent != nullptr && m_pCurrent->getKey() == key) {
		m_bChanged = false;
		return;
	}
	auto iter = m_combiners.find(key);
	if (iter != m_combiners.end()) {
		m_pCurrent = iter->second;
	} else {
		m_pCurrent = Combiner_Compile(key, true);
		m_pCurrent->update(true);
		m_combiners[m_pCurrent->getKey()] = m_pCurrent;
		combiner_journal_push(m_journal, m_pCurrent->getKey().getMux(), gli_GBI_hwlSupported ? 1 : 0);
	}
	m_bChanged = true;
}

void CombinerInfo::updateParameters()
{
	m_pCurrent->update(false);
}

void CombinerInfo::setDepthFogCombiner()
{
	if (m_shadowmapProgram) {
		m_shadowmapProgram->activate();
		m_pCurrent = m_shadowmapProgram.get();
	}
}

ShaderProgram * CombinerInfo::getTexrectCopyProgram()
{
	return m_texrectCopyProgram.get();
}

bool CombinerInfo::isShaderCacheSupported() const
{
	return config.generalEmulation.enableShadersStorage != 0 && Context::ShaderProgramBinary;
}

void CombinerInfo::setPolygonMode(DrawingState _drawingState)
{
	switch (_drawingState) {
	case DrawingState::Rect:
	case DrawingState::TexRect:
		m_rectMode = true;
		break;
	default:
		m_rectMode = false;
		break;
	}
}

bool CombinerInfo::_saveShadersStorage() const
{
	if (m_shadersLoaded >= m_combiners.size())
		return true;

	return gfxContext.saveShadersStorage(m_combiners);
}

/* Copy and fill modes use one combine each, so every key they can make is
 * known: build them all here rather than when a game first draws one. */
void CombinerInfo::_buildFixedKeys()
{
	static const u64 muxes[2][2] = {
		{ G_CYC_COPY, EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0) },
		{ G_CYC_FILL, EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE) }
	};
	const bool hwl = gli_GBI_hwlSupported;
	for (u32 mode = 0; mode < 2; ++mode)
		for (u32 bits = 0; bits < 16; ++bits) {
			/* bits: rect, two of bilerp, hardware lighting */
			const u32 flags = (bits & 1) | (static_cast<u32>(muxes[mode][0]) << 1) | ((bits & 6) << 2) | ((bits & 8) << 2);
			gDPCombine combine;
			combine.mux = muxes[mode][1];
			combine.muxs0 |= flags << 24;
			const CombinerKey key(combine.mux, false);
			if (m_combiners.find(key) != m_combiners.end())
				continue;
			gli_GBI_hwlSupported = (bits & 8) != 0;
			graphics::CombinerProgram * pCombiner = Combiner_Compile(key);
			pCombiner->update(true);
			m_combiners[pCombiner->getKey()] = pCombiner;
		}
	gli_GBI_hwlSupported = hwl;
}

void CombinerInfo::_journalKey(void * _ctx, uint64_t _key, unsigned _hwl)
{
	CombinerInfo * info = static_cast<CombinerInfo *>(_ctx);
	const CombinerKey key(_key, false);
	if (info->m_combiners.find(key) != info->m_combiners.end())
		return;
	const bool hwl = gli_GBI_hwlSupported;
	gli_GBI_hwlSupported = _hwl != 0;
	graphics::CombinerProgram * pCombiner = Combiner_Compile(key);
	pCombiner->update(true);
	info->m_combiners[pCombiner->getKey()] = pCombiner;
	gli_GBI_hwlSupported = hwl;
}

/* Keys a session that crashed wrote down are built now, at load, and are
 * in what this session saves: m_shadersLoaded does not count them. */
void CombinerInfo::_openKeysJournal()
{
	u32 version = 0;
	const std::string path = gfxContext.getCombinerJournalPath(version);
	combiner_journal_read(path.c_str(), version, _journalKey, this);
	m_journal = combiner_journal_open(path.c_str(), version);
}

void CombinerInfo::_closeKeysJournal(bool _saved)
{
	u32 version = 0;
	combiner_journal_close(m_journal);
	m_journal = nullptr;
	/* An ubershader-only driver has no program of these keys to save: the
	 * journal is how the next load builds them. */
	if (_saved && !gfxContext.isCombinerUberOnly()) {
		const std::string path = gfxContext.getCombinerJournalPath(version);
		combiner_journal_remove(path.c_str());
	}
}

bool CombinerInfo::_loadShadersStorage()
{
	if (gfxContext.loadShadersStorage(m_combiners)) {
		m_shadersLoaded = static_cast<u32>(m_combiners.size());
		return true;
	}

	return false;
}

bool gli_combiner_is_rect_mode(void)
{
	return CombinerInfo::get().isRectMode();
}
