#ifndef COMBINER_H
#define COMBINER_H

#include <map>
#include <memory>

#include "GLideN64.h"
#include "GraphicsDrawer.h"
#include "gDP.h"
#include "CombinerKey.h"
#include "Graphics/CombinerProgram.h"
#include "Graphics/ShaderProgram.h"

#include "CombinerMux.h"

struct CombinerOp
{
	int op = LOAD;
	int param1 = -1;
	int param2 = -1;
	int param3 = -1;
};

struct CombinerStage
{
	int numOps;
	CombinerOp op[6];
};

struct Combiner
{
	int numStages;
	CombinerStage stage[2];
};

struct CombineCycle
{
	int sa, sb, m, a;
};

class CombinerInfo
{
public:
	void init();
	void destroy();
	void update();
	void setCombine(u64 _mux);
	void updateParameters();

	void setDepthFogCombiner();
	graphics::ShaderProgram * getTexrectCopyProgram();

	graphics::CombinerProgram * getCurrent() const { return m_pCurrent; }
	bool isChanged() const {return m_bChanged;}
	size_t getCombinersNumber() const { return m_combiners.size();  }
	bool isShaderCacheSupported() const;

	static CombinerInfo & get();

	void setPolygonMode(DrawingState _drawingState);
	bool isRectMode() const { return m_rectMode; }

private:
	CombinerInfo()
		: m_bChanged(false)
		, m_rectMode(true)
		, m_shadersLoaded(0)
		, m_configOptionsBitSet(0)
		, m_pCurrent(nullptr)
		, m_journal(nullptr) {}
	CombinerInfo(const CombinerInfo &) = delete;

	void _buildFixedKeys();
	bool _saveShadersStorage() const;
	bool _loadShadersStorage();
	void _openKeysJournal();
	void _closeKeysJournal(bool _saved);
	static void _journalKey(void * _ctx, uint64_t _key, unsigned _hwl);

	bool m_bChanged;
	bool m_rectMode;
	u32 m_shadersLoaded;
	u32 m_configOptionsBitSet;

	graphics::CombinerProgram * m_pCurrent;
	graphics::Combiners m_combiners;
	struct combiner_journal * m_journal;

	std::unique_ptr<graphics::ShaderProgram> m_shadowmapProgram;
	std::unique_ptr<graphics::ShaderProgram> m_texrectCopyProgram;
};

inline
graphics::CombinerProgram * currentCombiner() {
	return CombinerInfo::get().getCurrent();
}

void Combiner_Init();
void Combiner_Destroy();
graphics::CombinerProgram * Combiner_Compile(CombinerKey key, bool _mayDefer = false);

#endif

