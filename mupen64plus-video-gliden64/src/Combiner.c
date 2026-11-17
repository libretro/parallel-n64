/* GLideN64's combiners (gli_combiner.h). C89.
 *
 * The C++ kept the programs in a std::map, found again through the
 * CombinerInfo singleton on every draw, and asked the current program
 * for its key through a virtual call. Here they are in a hash table by
 * key, in one global, and the current program's key is read from it. */
#include <stdlib.h>
#include <retro_miscellaneous.h>

#include "gli_combiner.h"
#include "CombinerKey.h"
#include "CombinerMux.h"
#include "CombinerKeyJournal.h"
#include "Config.h"
#include "GBI.h"
#include "gDP.h"
#include "Graphics/gli_context.h"

gli_combiner_info gli_cmb = { NULL, { NULL, 0, 0 }, NULL, NULL, NULL, 0, 0, 1 };

static void program_free(gli_cprog **_program)
{
	if (*_program != NULL)
		(*_program)->ops->free_program(*_program);
	*_program = NULL;
}

static void program_keep(gli_cprog *_program)
{
	_program->ops->update(_program, 1);
	gli_combiners_put(&gli_cmb.table, _program);
}

gli_cprog *gli_combiner_compile(u64 _key, bool _mayDefer)
{
	Combiner color, alpha;
	gli_combiner_decode(_key, gli_combiner_key_cycle_type(_key), &color, &alpha);
	if (_mayDefer) {
		/* The builder corrects the stages in place: on copies, for what may still compile below. */
		static int strict = -1;
		Combiner deferredColor = color, deferredAlpha = alpha;
		gli_cprog *program = gli_ctx.ops->createCombinerProgramAsync(gli_ctx.impl, &deferredColor, &deferredAlpha, _key);
		if (program != NULL)
			return program;
		if (strict < 0)
			strict = getenv("GLIDEN64_GL_STRICT") != NULL;
		if (strict)
			abort();
	}
	return gli_ctx.ops->createCombinerProgram(gli_ctx.impl, &color, &alpha, _key);
}

/* Copy and fill modes use one combine each, so every key they can make is
 * known: build them all here rather than when a game first draws one. */
static void build_fixed_keys(void)
{
	static const u64 muxes[2][2] = {
		{ G_CYC_COPY, EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0) },
		{ G_CYC_FILL, EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE) }
	};
	const bool hwl = gli_GBI_hwlSupported;
	u32 mode, bits;
	for (mode = 0; mode < 2; ++mode)
		for (bits = 0; bits < 16; ++bits) {
			/* bits: rect, two of bilerp, hardware lighting */
			const u32 flags = (bits & 1) | ((u32)muxes[mode][0] << 1) | ((bits & 6) << 2) | ((bits & 8) << 2);
			gDPCombine combine;
			combine.mux = muxes[mode][1];
			combine.muxs0 |= flags << 24;
			if (gli_combiners_find(&gli_cmb.table, combine.mux) != NULL)
				continue;
			gli_GBI_hwlSupported = (bits & 8) != 0;
			program_keep(gli_combiner_compile(combine.mux, 0));
		}
	gli_GBI_hwlSupported = hwl;
}

static void journal_key(void *_ctx, uint64_t _key, unsigned _hwl)
{
	bool hwl;
	(void)_ctx;
	if (gli_combiners_find(&gli_cmb.table, _key) != NULL)
		return;
	hwl = gli_GBI_hwlSupported;
	gli_GBI_hwlSupported = _hwl != 0;
	program_keep(gli_combiner_compile(_key, 0));
	gli_GBI_hwlSupported = hwl;
}

/* Keys a session that crashed wrote down are built now, at load, and are
 * in what this session saves: shadersLoaded does not count them. */
static void open_keys_journal(void)
{
	char path[PATH_MAX_LENGTH];
	u32 version = 0;
	gli_ctx.ops->getCombinerJournalPath(gli_ctx.impl, path, sizeof(path), &version);
	combiner_journal_read(path, version, journal_key, NULL);
	gli_cmb.journal = combiner_journal_open(path, version);
}

static void close_keys_journal(bool _saved)
{
	u32 version = 0;
	combiner_journal_close(gli_cmb.journal);
	gli_cmb.journal = NULL;
	/* An ubershader-only driver has no program of these keys to save: the
	 * journal is how the next load builds them. */
	if (_saved && !gli_ctx.ops->isCombinerUberOnly(gli_ctx.impl)) {
		char path[PATH_MAX_LENGTH];
		gli_ctx.ops->getCombinerJournalPath(gli_ctx.impl, path, sizeof(path), &version);
		combiner_journal_remove(path);
	}
}

void gli_combiner_init(void)
{
	gli_ctx.ops->resetCombinerProgramBuilder(gli_ctx.impl);
	gli_cmb.current = NULL;
	gli_cmb.shadersLoaded = 0;
	if (gli_cmb.table.slots == NULL)
		gli_combiners_make(&gli_cmb.table, GLI_COMBINER_SLOTS);

	if (config.generalEmulation.enableShadersStorage != 0) {
		if (gli_ctx.ops->loadShadersStorage(gli_ctx.impl, &gli_cmb.table))
			gli_cmb.shadersLoaded = gli_cmb.table.count;
		else
			gli_combiners_clear(&gli_cmb.table);
	}

	build_fixed_keys();

	if (config.generalEmulation.enableShadersStorage != 0)
		open_keys_journal();

	program_free(&gli_cmb.shadowmap);
	gli_cmb.shadowmap = gli_ctx.ops->createDepthFogShader(gli_ctx.impl);
	program_free(&gli_cmb.texrectCopy);
	gli_cmb.texrectCopy = gli_ctx.ops->createTexrectCopyShader(gli_ctx.impl);

	gDP.otherMode.cycleType = G_CYC_1CYCLE;
}

void gli_combiner_destroy(void)
{
	program_free(&gli_cmb.shadowmap);
	program_free(&gli_cmb.texrectCopy);
	gli_cmb.current = NULL;
	if (config.generalEmulation.enableShadersStorage != 0) {
		const bool saved = gli_cmb.shadersLoaded >= gli_cmb.table.count ||
			gli_ctx.ops->saveShadersStorage(gli_ctx.impl, &gli_cmb.table);
		close_keys_journal(saved);
	}
	gli_cmb.shadersLoaded = 0;
	gli_combiners_clear(&gli_cmb.table);
}

void gli_combiner_update(void)
{
	/* TODO: find, why gDP.changed & CHANGED_COMBINE not always works (e.g. Mario Tennis). */
	gli_combiner_set_combine(gli_combiner_mux());
	gDP.changed &= ~CHANGED_COMBINE;
}

void gli_combiner_set_combine(u64 _mux)
{
	const u64 key = gli_combiner_key(_mux);
	gli_cprog *program;
	if (gli_cmb.current != NULL && gli_cmb.current->key == key) {
		gli_cmb.changed = 0;
		return;
	}
	program = gli_combiners_find(&gli_cmb.table, key);
	if (program == NULL) {
		program = gli_combiner_compile(key, 1);
		program_keep(program);
		combiner_journal_push(gli_cmb.journal, program->key, gli_GBI_hwlSupported ? 1 : 0);
	}
	gli_cmb.current = program;
	gli_cmb.changed = 1;
}

void gli_combiner_update_parameters(void)
{
	gli_cmb.current->ops->update(gli_cmb.current, 0);
}

void gli_combiner_set_depth_fog(void)
{
	if (gli_cmb.shadowmap != NULL) {
		gli_cmb.shadowmap->ops->activate(gli_cmb.shadowmap);
		gli_cmb.current = gli_cmb.shadowmap;
	}
}

void gli_combiner_set_rect_mode(bool _rect)
{
	gli_cmb.rectMode = _rect;
}

bool gli_combiner_shader_cache_supported(void)
{
	return config.generalEmulation.enableShadersStorage != 0 && gli_ctx.shaderProgramBinary;
}

bool gli_combiner_is_rect_mode(void)
{
	return gli_cmb.rectMode;
}

bool gli_combiner_uses_lod(void)
{
	return gli_cmb.current->ops->usesLOD(gli_cmb.current);
}
