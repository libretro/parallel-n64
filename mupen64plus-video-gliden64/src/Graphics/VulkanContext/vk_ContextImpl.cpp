#include <string>
#include <string.h>
#include <stdlib.h>

#include <Config.h>
#include <Combiner.h>
#include <gDP.h>
#include <gSP.h>
#include <N64.h>
#include <NoiseTexture.h>
#include <PaletteTexture.h>
#include <FrameBuffer.h>
#include <DisplayWindow.h>
#include <Textures.h>
#include <Graphics/Context.h>
#include <Graphics/ContextImpl.h>
#include <Graphics/Parameters.h>
#include <Graphics/ColorBufferReader.h>
#include <Graphics/PixelBuffer.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/GLSL/glsl_CombinerInputs.h>
#include <GraphicsDrawer.h>
#include <PluginAPI.h>
#include <wchar.h>
#include <osal_files.h>

#include "vk_renderer.h"
#include "vk_device.h"
#include "vk_ContextImpl.h"

using namespace graphics;

namespace {

vkr_renderer * g_r = nullptr;

/* GL's numbers, which GLideN64's parameters carry */
enum {
	GLV_R8 = 0x8229, GLV_R16F = 0x822D, GLV_R32F = 0x822E, GLV_R32UI = 0x8236,
	GLV_RGBA8 = 0x8058, GLV_RGB8 = 0x8051, GLV_RGBA4 = 0x8056, GLV_RGB5_A1 = 0x8057,
	GLV_DEPTH = 0x81A6
};

int toFormat(u32 _internal)
{
	switch (_internal) {
	case GLV_RGBA8: return VKR_FMT_RGBA8;
	case GLV_RGB8: return VKR_FMT_RGB8;
	case GLV_RGBA4: return VKR_FMT_RGBA4;
	case GLV_RGB5_A1: return VKR_FMT_RGB5A1;
	case GLV_R8: return VKR_FMT_R8;
	case GLV_R16F: return VKR_FMT_R16F;
	case GLV_R32F: return VKR_FMT_R32F;
	case GLV_R32UI: return VKR_FMT_R32UI;
	case GLV_DEPTH: return VKR_FMT_DEPTH;
	}
	return VKR_FMT_NONE;
}

u32 formatBytes(int _fmt)
{
	switch (_fmt) {
	case VKR_FMT_RGBA4: case VKR_FMT_RGB5A1: case VKR_FMT_R16F: return 2;
	case VKR_FMT_R8: return 1;
	}
	return 4;
}

int toUnit(TextureUnitParam _unit)
{
	const u32 u(_unit);
	static const int units[8] = { VKR_BIND_TEX0, VKR_BIND_TEX1, VKR_BIND_NOISE, VKR_BIND_DEPTH,
		VKR_BIND_ZLUT, VKR_BIND_TLUT, VKR_BIND_MS0, VKR_BIND_MS1 };
	return u < 8 ? units[u] : VKR_BIND_COUNT;
}

int toBlend(BlendParam _b)
{
	if (_b == blend::ZERO) return VKR_BLEND_ZERO;
	if (_b == blend::ONE) return VKR_BLEND_ONE;
	if (_b == blend::SRC_ALPHA) return VKR_BLEND_SRC_ALPHA;
	if (_b == blend::DST_ALPHA) return VKR_BLEND_DST_ALPHA;
	if (_b == blend::ONE_MINUS_SRC_ALPHA) return VKR_BLEND_INV_SRC_ALPHA;
	if (_b == blend::CONSTANT_ALPHA) return VKR_BLEND_CONST_ALPHA;
	if (_b == blend::ONE_MINUS_CONSTANT_ALPHA) return VKR_BLEND_INV_CONST_ALPHA;
	if (_b == blend::SRC1_COLOR) return VKR_BLEND_SRC1_COLOR;
	if (_b == blend::ONE_MINUS_SRC1_COLOR) return VKR_BLEND_INV_SRC1_COLOR;
	if (_b == blend::SRC1_ALPHA) return VKR_BLEND_SRC1_ALPHA;
	if (_b == blend::ONE_MINUS_SRC1_ALPHA) return VKR_BLEND_INV_SRC1_ALPHA;
	return VKR_BLEND_ONE;
}

int toWrap(TextureParam _w)
{
	if (_w == textureParameters::WRAP_REPEAT) return VKR_WRAP_REPEAT;
	if (_w == textureParameters::WRAP_MIRRORED_REPEAT) return VKR_WRAP_MIRROR;
	return VKR_WRAP_CLAMP;
}

int toTopology(DrawModeParam _m)
{
	if (_m == drawmode::TRIANGLE_STRIP) return VKR_TOPO_STRIP;
	if (_m == drawmode::LINES) return VKR_TOPO_LINES;
	return VKR_TOPO_TRIANGLES;
}

/*---------------Combiner-------------*/

class VkCombinerProgram : public CombinerProgram
{
public:
	VkCombinerProgram(const CombinerKey & _key, vkr_program * _p, glsl::CombinerInputs _inputs, unsigned _groups)
		: m_key(_key), m_p(_p), m_inputs(_inputs), m_groups(_groups), m_values() {}
	~VkCombinerProgram() { vkr_program_delete(g_r, m_p); }

	static void * operator new(size_t);
	static void operator delete(void *);

	void activate() override { vkr_use(g_r, m_p); }
	void update(bool) override {
		vkr_use(g_r, m_p);
		updateCombinerValues(&m_values, m_groups);
		vkr_values(g_r, m_p, &m_values);
	}
	const CombinerKey & getKey() const override { return m_key; }
	bool usesTexture() const override { return m_inputs.usesTexture(); }
	bool usesTile(u32 _t) const override { return m_inputs.usesTile(_t); }
	bool usesShade() const override { return m_inputs.usesShade(); }
	bool usesLOD() const override { return m_inputs.usesLOD(); }
	bool usesHwLighting() const override { return m_inputs.usesHwLighting(); }
	bool getBinaryForm(std::vector<char> &) override { return false; }

private:
	CombinerKey m_key;
	vkr_program * m_p;
	glsl::CombinerInputs m_inputs;
	unsigned m_groups;
	CombinerValues m_values;
};

glsl::CombinerInputs addStage(glsl::CombinerInputs _in, const CombinerStage & _stage)
{
	for (int i = 0; i < _stage.numOps; ++i) {
		_in.addInput(_stage.op[i].param1);
		if (_stage.op[i].op == INTER) {
			_in.addInput(_stage.op[i].param2);
			_in.addInput(_stage.op[i].param3);
		}
	}
	return _in;
}

/* as many as the renderer has programs: a miss allocates nothing */
static FixedCellPool<sizeof(VkCombinerProgram), 4096> s_combinerPool;
void * VkCombinerProgram::operator new(size_t) { return s_combinerPool.take(); }
void VkCombinerProgram::operator delete(void * _p) { s_combinerPool.give(_p); }

CombinerProgram * buildCombiner(Combiner & _color, Combiner & _alpha, const CombinerKey & _key)
{
	gDPCombine combine;
	combine.mux = _key.getMux();
	const u32 cycle = _key.getCycleType();
	vkr_combiner_desc desc;
	memset(&desc, 0, sizeof(desc));

	if (cycle != G_CYC_2CYCLE) {
		correctFirstStageParams(_alpha.stage[0]);
		correctFirstStageParams(_color.stage[0]);
	}
	glsl::CombinerInputs inputs = addStage(addStage(glsl::CombinerInputs(), _alpha.stage[0]), _color.stage[0]);

	int flags = 0;
	if (combinedAlphaC(cycle, combine)) flags |= VKR_CF_ALPHA_C;
	else if (combinedAlphaABD(cycle, combine)) flags |= VKR_CF_ALPHA_ABD;
	if (combinedColorC(cycle, combine)) flags |= VKR_CF_COLOR_C;
	else if (combinedColorABD(cycle, combine)) flags |= VKR_CF_COLOR_ABD;

	encodeStage(_alpha.stage[0], &desc.ops[0]);
	encodeStage(_color.stage[0], &desc.ops[4]);
	for (int i = 8; i < 16; ++i)
		desc.ops[i] = -1;

	if (cycle == G_CYC_2CYCLE && _alpha.numStages == 2) {
		correctSecondStageParams(_alpha.stage[1]);
		correctSecondStageParams(_color.stage[1]);
		inputs = addStage(addStage(inputs, _alpha.stage[1]), _color.stage[1]);
		encodeStage(_alpha.stage[1], &desc.ops[8]);
		encodeStage(_color.stage[1], &desc.ops[12]);
		flags |= VKR_CF_TWO_STAGES;
	}

	const bool isRect = _key.isRectKey();
	const bool hwLight = !isRect && gli_isHWLightingAllowed() && inputs.usesShadeColor();
	if (hwLight)
		inputs.addInput(G_GCI_HW_LIGHT);

	if (inputs.usesTexture()) flags |= VKR_CF_TEXTURED;
	if (inputs.usesTile(0)) flags |= VKR_CF_TILE0;
	if (inputs.usesTile(1)) flags |= VKR_CF_TILE1;
	if (inputs.usesLOD()) flags |= VKR_CF_LOD;
	if (hwLight) flags |= VKR_CF_HWLIGHT;
	const u32 bilerp = _key.getBilerp();
	if (bilerp & 2) flags |= VKR_CF_BILERP0;
	if (bilerp & 1) flags |= VKR_CF_BILERP1;
	desc.flags = flags | (int(cycle) << VKR_CF_CYCLE_SHIFT);

	CombinerNeeds needs;
	memset(&needs, 0, sizeof(needs));
	needs.usesTexture = inputs.usesTexture();
	needs.usesLOD = inputs.usesLOD();
	needs.usesNoise = inputs.usesNoise();
	needs.usesTile0 = inputs.usesTile(0);
	needs.usesTile1 = inputs.usesTile(1);
	needs.usesHwLighting = inputs.usesHwLighting();
	needs.depthScale = 1;
	needs.cycleType = int(cycle);
	needs.isRect = isRect;

	vkr_program * p = vkr_combiner_new(g_r, &desc, isRect ? 1 : 0);
	if (p == nullptr)
		return nullptr;
	return new VkCombinerProgram(_key, p, inputs, combinerValueGroups(&needs));
}

/*---------------Special shaders-------------*/

class VkShader : public ShaderProgram
{
public:
	VkShader(int _vs, int _fs) : m_p(vkr_special_new(g_r, _vs, _fs)) {}
	~VkShader() { vkr_program_delete(g_r, m_p); }
	void activate() override {
		vkr_use(g_r, m_p);
		gDP.changed |= CHANGED_COMBINE;
	}
protected:
	vkr_program * m_p;
};

class VkShadowMapShader : public VkShader
{
public:
	VkShadowMapShader() : VkShader(VKR_VS_SPECIAL, VKR_FS_SHADOW_MAP) {}
	void activate() override {
		VkShader::activate();
		vkr_blend(g_r, VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA);
		vkr_special_params(g_r, m_p, gDP.fogColor.r, gDP.fogColor.g, gDP.fogColor.b, gDP.fogColor.a);
		gli_palette_update();
	}
};

class VkGammaShader : public VkShader
{
public:
	VkGammaShader() : VkShader(VKR_VS_SPECIAL, VKR_FS_GAMMA) {
		const f32 level = (config.gammaCorrection.force != 0) ? config.gammaCorrection.level : 2.0f;
		vkr_special_params(g_r, m_p, level, 0.f, 0.f, 0.f);
	}
};

class VkFXAAShader : public VkShader
{
public:
	VkFXAAShader() : VkShader(VKR_VS_FXAA, VKR_FS_FXAA), m_width(0), m_height(0) {}
	void activate() override {
		VkShader::activate();
		FrameBuffer * pBuffer = frameBufferList().findBuffer(*REG.VI_ORIGIN);
		if (pBuffer != nullptr && pBuffer->m_pTexture != nullptr) {
			m_width = pBuffer->m_pTexture->realWidth;
			m_height = pBuffer->m_pTexture->realHeight;
		}
		vkr_special_params(g_r, m_p, f32(m_width), f32(m_height), 0.f, 0.f);
	}
private:
	u16 m_width, m_height;
};

class VkTexrectDrawerDraw : public TexrectDrawerShaderProgram
{
public:
	VkTexrectDrawerDraw() : m_p(vkr_special_new(g_r, VKR_VS_SPECIAL, VKR_FS_TEXRECT_DRAW)), m_alpha(0) {}
	~VkTexrectDrawerDraw() { vkr_program_delete(g_r, m_p); }
	void activate() override {
		vkr_use(g_r, m_p);
		const f32 depth = gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : 0.0f;
		vkr_special_params(g_r, m_p, f32(m_alpha), depth, 0.f, 0.f);
		gDP.changed |= CHANGED_COMBINE;
	}
	void setTextureSize(u32, u32) override {}
	void setEnableAlphaTest(int _enable) override {
		m_alpha = _enable;
		gDP.changed |= CHANGED_COMBINE;
	}
private:
	vkr_program * m_p;
	int m_alpha;
};

/*---------------Readers-------------*/

class VkPixelReadBuffer : public PixelReadBuffer
{
public:
	VkPixelReadBuffer(size_t _bytes) : m_rb(vkr_readback_new(g_r, _bytes)) {}
	~VkPixelReadBuffer() { vkr_readback_delete(g_r, m_rb); }
	void readPixels(s32 _x, s32 _y, u32 _w, u32 _h, Parameter _format, Parameter) override {
		vkr_readback_fbo(g_r, m_rb, _x, _y, _w, _h, _format == Parameter(colorFormat::DEPTH) ? 1 : 0);
	}
	void * getDataRange(u32 _offset, u32) override {
		vkr_readback_ready(g_r, m_rb, 1);
		return const_cast<u8*>(static_cast<const u8*>(vkr_readback_data(m_rb))) + _offset;
	}
	void closeReadBuffer() override {}
	void bind() override {}
	void unbind() override {}
private:
	vkr_readback * m_rb;
};

class VkColorBufferReader : public ColorBufferReader
{
public:
	enum { MaxSlots = 8 };

	VkColorBufferReader(CachedTexture * _pTexture) : ColorBufferReader(_pTexture), m_numSlots(1) {
		m_numSlots = config.frameBufferEmulation.copyToRDRAM > 1 ? config.frameBufferEmulation.copyToRDRAM : 1;
		if (m_numSlots > MaxSlots)
			m_numSlots = MaxSlots;
		for (u32 i = 0; i < m_numSlots; ++i)
			m_rb[i] = vkr_readback_new(g_r, m_pTexture->textureBytes);
	}
	~VkColorBufferReader() {
		for (u32 i = 0; i < m_numSlots; ++i)
			vkr_readback_delete(g_r, m_rb[i]);
	}

	u32 numSlots() const override { return m_numSlots; }

	void readToSlot(u32 _slot, s32 _x0, s32 _y0, u32 _height, u32) override {
		vkr_readback_tex(g_r, m_rb[_slot], u32(m_pTexture->name), _x0, _y0, m_pTexture->width, _height);
	}

	bool slotReady(u32 _slot, bool _wait) override {
		return vkr_readback_ready(g_r, m_rb[_slot], _wait ? 1 : 0) != 0;
	}

	void cleanUp() override {}

private:
	const u8 * _readPixels(const ReadColorBufferParams & _params, u32 & _heightOffset, u32 & _stride) override {
		vkr_readback_tex(g_r, m_rb[0], u32(m_pTexture->name), _params.x0, _params.y0, m_pTexture->width, _params.height);
		vkr_readback_ready(g_r, m_rb[0], 1);
		_heightOffset = 0;
		_stride = m_pTexture->width;
		return static_cast<const u8*>(vkr_readback_data(m_rb[0]));
	}
	const u8 * _mapSlot(u32 _slot, u32, u32, u32 & _stride) override {
		_stride = m_pTexture->width;
		return static_cast<const u8*>(vkr_readback_data(m_rb[_slot]));
	}

	u32 m_numSlots;
	vkr_readback * m_rb[MaxSlots];
};

/*---------------Formats-------------*/

struct VkFramebufferFormats : public FramebufferTextureFormats
{
	VkFramebufferFormats() { init(); }
protected:
	void init() override {
		enum { GLV_RGBA = 0x1908, GLV_RED = 0x1903, GLV_RED_INT = 0x8D94, GLV_DEPTHC = 0x1902,
			GLV_UBYTE = 0x1401, GLV_UINT = 0x1405, GLV_FLOAT = 0x1406 };
		colorInternalFormat = GLV_RGBA8; colorFormat = GLV_RGBA; colorType = GLV_UBYTE; colorFormatBytes = 4;
		monochromeInternalFormat = GLV_R8; monochromeFormat = GLV_RED; monochromeType = GLV_UBYTE; monochromeFormatBytes = 1;
		depthInternalFormat = GLV_DEPTH; depthFormat = GLV_DEPTHC; depthType = GLV_FLOAT; depthFormatBytes = 4;
		depthImageInternalFormat = GLV_R32F; depthImageFormat = GLV_RED; depthImageType = GLV_FLOAT; depthImageFormatBytes = 4;
		lutInternalFormat = GLV_R32UI; lutFormat = GLV_RED_INT; lutType = GLV_UINT; lutFormatBytes = 4;
		noiseInternalFormat = GLV_R8; noiseFormat = GLV_RED; noiseType = GLV_UBYTE; noiseFormatBytes = 1;
	}
};

/*---------------Context-------------*/

class VkContextImpl : public ContextImpl
{
public:
	VkContextImpl() : m_clampMode(ClampMode::ClippingEnabled), m_unpack(1) {}

	void init() override { _create(); }
	void destroy() override {
		vkr_destroy(g_r);
		g_r = nullptr;
	}

	void setClampMode(ClampMode _mode) override {
		m_clampMode = _mode;
		switch (_mode) {
		case ClampMode::ClippingEnabled: vkr_set_clamp(g_r, 0, 0); break;
		case ClampMode::NoNearPlaneClipping: vkr_set_clamp(g_r, 1, 1); break;
		case ClampMode::NoClipping: vkr_set_clamp(g_r, 1, 0); break;
		}
	}
	ClampMode getClampMode() override { return m_clampMode; }

	void enable(EnableParam _p, bool _on) override {
		if (_p == enable::BLEND) vkr_enable(g_r, VKR_CAP_BLEND, _on);
		else if (_p == enable::CULL_FACE) vkr_enable(g_r, VKR_CAP_CULL, _on);
		else if (_p == enable::DEPTH_TEST) vkr_enable(g_r, VKR_CAP_DEPTH_TEST, _on);
		else if (_p == enable::POLYGON_OFFSET_FILL) vkr_enable(g_r, VKR_CAP_POLY_OFFSET, _on);
		else if (_p == enable::SCISSOR_TEST) vkr_enable(g_r, VKR_CAP_SCISSOR, _on);
	}
	u32 isEnabled(EnableParam _p) override {
		if (_p == enable::BLEND) return vkr_is_enabled(g_r, VKR_CAP_BLEND);
		if (_p == enable::CULL_FACE) return vkr_is_enabled(g_r, VKR_CAP_CULL);
		if (_p == enable::DEPTH_TEST) return vkr_is_enabled(g_r, VKR_CAP_DEPTH_TEST);
		if (_p == enable::POLYGON_OFFSET_FILL) return vkr_is_enabled(g_r, VKR_CAP_POLY_OFFSET);
		if (_p == enable::SCISSOR_TEST) return vkr_is_enabled(g_r, VKR_CAP_SCISSOR);
		return 0;
	}
	void cullFace(CullModeParam _m) override {
		vkr_cull(g_r, _m == cullMode::FRONT ? VKR_CULL_FRONT : (_m == cullMode::BACK ? VKR_CULL_BACK : VKR_CULL_BOTH));
	}
	void enableDepthWrite(bool _on) override { vkr_depth_write(g_r, _on); }
	void setDepthCompare(CompareParam _m) override {
		vkr_depth_compare(g_r, _m == compare::LESS ? VKR_CMP_LESS : (_m == compare::ALWAYS ? VKR_CMP_ALWAYS : VKR_CMP_LEQUAL));
	}
	void setViewport(s32 _x, s32 _y, s32 _w, s32 _h) override { vkr_viewport(g_r, _x, _y, _w, _h); }
	void setScissor(s32 _x, s32 _y, s32 _w, s32 _h) override { vkr_scissor(g_r, _x, _y, _w, _h); }
	void setBlending(BlendParam _s, BlendParam _d) override {
		int s = toBlend(_s), d = toBlend(_d);
		vkr_blend(g_r, s, d, s, d);
	}
	void setBlendingSeparate(BlendParam _s, BlendParam _d, BlendParam _sa, BlendParam _da) override {
		vkr_blend(g_r, toBlend(_s), toBlend(_d), toBlend(_sa), toBlend(_da));
	}
	void setBlendColor(f32 _r, f32 _g, f32 _b, f32 _a) override { vkr_blend_color(g_r, _r, _g, _b, _a); }
	void clearColorBuffer(f32 _r, f32 _g, f32 _b, f32 _a) override { vkr_clear_color(g_r, _r, _g, _b, _a); }
	void clearDepthBuffer() override { vkr_clear_depth(g_r); }
	void setPolygonOffset(f32 _f, f32 _u) override { vkr_polygon_offset(g_r, _f, _u); }

	ObjectHandle createTexture(Parameter) override { return ObjectHandle(vkr_texture_new(g_r)); }
	void deleteTexture(ObjectHandle _h) override { vkr_texture_delete(g_r, u32(_h)); }
	void init2DTexture(const Context::InitTextureParams & _p) override {
		const int fmt = toFormat(u32(_p.internalFormat));
		const u32 samples = _p.msaaLevel > 0 ? _p.msaaLevel : 1;
		vkr_texture_init(g_r, u32(_p.handle), fmt, samples, _p.width, _p.height, _p.mipMapLevel,
			_p.mipMapLevels, _p.data, _p.data ? size_t(_p.width) * _p.height * formatBytes(fmt) : 0);
		if (_p.textureUnitIndex.isValid())
			bindUnit(_p.textureUnitIndex, _p.handle);
	}
	void update2DTexture(const Context::UpdateTextureDataParams & _p) override {
		const int fmt = toFormat(u32(_p.internalFormat));
		vkr_texture_update(g_r, u32(_p.handle), _p.mipMapLevel, _p.x, _p.y, _p.width, _p.height, _p.data,
			size_t(_p.width) * _p.height * formatBytes(fmt));
	}
	void * lendTextureData(size_t _bytes) override { return vkr_texture_lend(g_r, _bytes); }
	void setTextureParameters(const Context::TexParameters & _p) override {
		const int mag = _p.magFilter.isValid() ? (_p.magFilter == textureParameters::FILTER_LINEAR ? 1 : 0) : VKR_KEEP;
		int min = VKR_KEEP;
		if (_p.minFilter.isValid())
			min = (_p.minFilter == textureParameters::FILTER_LINEAR || _p.minFilter == textureParameters::FILTER_LINEAR_MIPMAP_NEAREST) ? 1 : 0;
		const int wrapS = _p.wrapS.isValid() ? toWrap(_p.wrapS) : VKR_KEEP;
		const int wrapT = _p.wrapT.isValid() ? toWrap(_p.wrapT) : VKR_KEEP;
		const f32 maxLod = _p.maxMipmapLevel.isValid() ? f32(s32(_p.maxMipmapLevel)) : -1.f;
		const f32 aniso = _p.maxAnisotropy.isValid() ? f32(_p.maxAnisotropy) : -1.f;
		vkr_texture_params(g_r, u32(_p.handle), mag, min, VKR_KEEP, wrapS, wrapT, maxLod, aniso);
		if (_p.textureUnitIndex.isValid())
			bindUnit(_p.textureUnitIndex, _p.handle);
	}
	void bindTexture(const Context::BindTextureParameters & _p) override { bindUnit(_p.textureUnitIndex, _p.texture); }
	void setTextureUnpackAlignment(s32 _a) override { m_unpack = _a; }
	s32 getTextureUnpackAlignment() const override { return m_unpack; }
	s32 getMaxTextureSize() const override { return vkr_get_caps(g_r)->max_texture_size; }
	f32 getMaxAnisotropy() const override { return vkr_get_caps(g_r)->max_anisotropy; }
	void bindImageTexture(const Context::BindImageTextureParameters & _p) override {
		vkr_bind_image(g_r, _p.imageUnit == textureImageUnits::DepthZ ? VKR_BIND_DEPTH_Z : VKR_BIND_DEPTH_DZ, u32(_p.texture));
	}
	u32 convertInternalTextureFormat(u32 _f) const override { return _f; }
	void textureBarrier() override {}
	FramebufferTextureFormats * getFramebufferTextureFormats() override { return new VkFramebufferFormats; }

	ObjectHandle createFramebuffer() override { return ObjectHandle(vkr_fbo_new(g_r)); }
	void deleteFramebuffer(ObjectHandle _h) override { vkr_fbo_delete(g_r, u32(_h)); }
	void bindFramebuffer(BufferTargetParam _t, ObjectHandle _h) override {
		if (u32(_h) == 0)
			_windowSize();
		if (_t == bufferTarget::READ_FRAMEBUFFER) vkr_fbo_bind(g_r, 1, 0, u32(_h));
		else if (_t == bufferTarget::DRAW_FRAMEBUFFER) vkr_fbo_bind(g_r, 0, 1, u32(_h));
		else vkr_fbo_bind(g_r, 1, 1, u32(_h));
	}
	void addFrameBufferRenderTarget(const Context::FrameBufferRenderTarget & _p) override {
		/* attaching binds, as it does in GL: a new buffer is drawn to without another bind */
		bindFramebuffer(_p.bufferTarget, _p.bufferHandle);
		if (_p.attachment == bufferAttachment::COLOR_ATTACHMENT0)
			vkr_fbo_color(g_r, u32(_p.bufferHandle), u32(_p.textureHandle));
		else if (_p.attachment == bufferAttachment::DEPTH_ATTACHMENT)
			vkr_fbo_depth(g_r, u32(_p.bufferHandle), u32(_p.textureHandle));
	}
	ObjectHandle createRenderbuffer() override { return ObjectHandle(vkr_texture_new(g_r)); }
	void initRenderbuffer(const Context::InitRenderbufferParams & _p) override {
		vkr_texture_init(g_r, u32(_p.handle), toFormat(u32(_p.format)), 1, _p.width, _p.height, 0, 1, nullptr, 0);
	}
	bool blitFramebuffers(const Context::BlitFramebuffersParams & _p) override {
		_windowSize();
		const u32 mask(_p.mask);
		const int color = (mask & u32(blitMask::COLOR_BUFFER)) != 0;
		const int depth = (mask & u32(blitMask::DEPTH_BUFFER)) != 0;
		return vkr_blit(g_r, u32(_p.readBuffer), u32(_p.drawBuffer), _p.srcX0, _p.srcY0, _p.srcX1, _p.srcY1,
			_p.dstX0, _p.dstY0, _p.dstX1, _p.dstY1, color, depth,
			_p.filter == Parameter(textureParameters::FILTER_LINEAR) ? 1 : 0) != 0;
	}
	void setDrawBuffers(u32) override {}
	PixelReadBuffer * createPixelReadBuffer(size_t _bytes) override { return new VkPixelReadBuffer(_bytes); }
	ColorBufferReader * createColorBufferReader(CachedTexture * _t) override { return new VkColorBufferReader(_t); }

	bool isCombinerProgramBuilderObsolete() override { return m_optionsBits != CombinerProgram::getShaderCombinerOptionsBits(); }
	void resetCombinerProgramBuilder() override {
		if (!isCombinerProgramBuilderObsolete())
			return;
		_recreate();
	}
	CombinerProgram * createCombinerProgram(Combiner & _c, Combiner & _a, const CombinerKey & _k) override { return buildCombiner(_c, _a, _k); }
	CombinerProgram * createCombinerProgramAsync(Combiner & _c, Combiner & _a, const CombinerKey & _k) override { return buildCombiner(_c, _a, _k); }
	bool saveShadersStorage(const Combiners &) override { return false; }
	bool loadShadersStorage(Combiners &) override { return false; }
	std::string getCombinerJournalPath(u32 & _version) override {
		_version = 1;
		wchar_t path[PLUGIN_PATH_SIZE];
		api().GetUserCachePath(path);
		char mb[PLUGIN_PATH_SIZE * 4];
		wcstombs(mb, path, sizeof(mb));
		return std::string(mb) + "/GLideN64.vulkan.keys.journal";
	}
	ShaderProgram * createDepthFogShader() override { return new VkShadowMapShader; }
	TexrectDrawerShaderProgram * createTexrectDrawerDrawShader() override { return new VkTexrectDrawerDraw; }
	ShaderProgram * createTexrectDrawerClearShader() override { return new VkShader(VKR_VS_SPECIAL, VKR_FS_TEXRECT_CLEAR); }
	ShaderProgram * createTexrectCopyShader() override { return new VkShader(VKR_VS_SPECIAL, VKR_FS_TEXRECT_COPY); }
	ShaderProgram * createGammaCorrectionShader() override { return new VkGammaShader; }
	ShaderProgram * createFXAAShader() override { return new VkFXAAShader; }
	void resetShaderProgram() override { vkr_use(g_r, nullptr); }

	void drawTriangles(const Context::DrawTriangleParameters & _p) override {
		/* converted straight into the ring the GPU reads */
		vkr_tri_vertex * dst = vkr_tri_reserve(g_r, _p.verticesCount);
		const f32 lights = gli_isHWLightingAllowed() ? f32(_p.vertices[0].HWLight) : 0.f;
		for (u32 i = 0; i < _p.verticesCount; ++i) {
			const SPVertex & src = _p.vertices[i];
			vkr_tri_vertex & d = dst[i];
			d.x = src.x; d.y = src.y; d.z = src.z; d.w = src.w;
			if (_p.flatColors) {
				d.r = src.flat_r; d.g = src.flat_g; d.b = src.flat_b; d.a = src.flat_a;
			} else {
				d.r = src.r; d.g = src.g; d.b = src.b; d.a = src.a;
			}
			d.s = src.s; d.t = src.t;
			d.modify = src.modify;
			d.num_lights = lights;
		}
		if (_p.elements != nullptr) {
			uint16_t * idx = vkr_index_reserve(g_r, _p.elementsCount);
			memcpy(idx, _p.elements, _p.elementsCount * sizeof(uint16_t));
		}
		vkr_draw_tris(g_r, toTopology(_p.mode), _p.verticesCount, _p.elements != nullptr ? _p.elementsCount : 0);
	}
	void drawRects(const Context::DrawRectParameters & _p) override {
		vkr_rect_vertex * dst = vkr_rect_reserve(g_r, _p.verticesCount);
		memcpy(dst, _p.vertices, _p.verticesCount * sizeof(RectVertex));
		if (!_p.texrect) {
			/* GL reads the texture coordinates of such a rect as 0 */
			for (u32 i = 0; i < _p.verticesCount; ++i)
				dst[i].s0 = dst[i].t0 = dst[i].s1 = dst[i].t1 = 0.f;
		}
		vkr_draw_rects(g_r, toTopology(_p.mode), _p.verticesCount);
	}
	void drawLine(f32 _width, SPVertex * _v) override {
		vkr_tri_vertex * dst = vkr_tri_reserve(g_r, 2);
		for (u32 i = 0; i < 2; ++i) {
			dst[i].x = _v[i].x; dst[i].y = _v[i].y; dst[i].z = _v[i].z; dst[i].w = _v[i].w;
			dst[i].r = _v[i].r; dst[i].g = _v[i].g; dst[i].b = _v[i].b; dst[i].a = _v[i].a;
			dst[i].s = _v[i].s; dst[i].t = _v[i].t;
			dst[i].modify = _v[i].modify;
			dst[i].num_lights = 0.f;
		}
		vkr_draw_line(g_r, 2, _width);
	}
	f32 getMaxLineWidth() override { return vkr_get_caps(g_r)->max_line_width; }
	s32 getMaxMSAALevel() override { return vkr_get_caps(g_r)->max_samples; }
	bool isSupported(SpecialFeatures _f) const override {
		switch (_f) {
		case SpecialFeatures::Multisampling:
		case SpecialFeatures::BlitFramebuffer:
		case SpecialFeatures::DepthFramebufferTextures:
		case SpecialFeatures::IntegerTextures:
		case SpecialFeatures::ClipControl:
			return true;
		case SpecialFeatures::ImageTextures:
			return vkr_get_caps(g_r)->storage_images != 0;
		default:
			return false;
		}
	}
	bool isError() const override { return false; }
	bool isFramebufferError() const override { return false; }

private:
	void bindUnit(TextureUnitParam _unit, ObjectHandle _h) {
		const int u = toUnit(_unit);
		if (u < VKR_BIND_COUNT)
			vkr_bind(g_r, u, u32(_h));
	}

	void _windowSize() { vkr_set_window(g_r, dwnd().getScreenWidth(), dwnd().getScreenHeight()); }

	void _create() {
		m_optionsBits = CombinerProgram::getShaderCombinerOptionsBits();
		vkr_shader_config cfg;
		memset(&cfg, 0, sizeof(cfg));
		cfg.legacy_blending = config.generalEmulation.enableLegacyBlending != 0;
		cfg.noise = config.generalEmulation.enableNoise != 0;
		cfg.fragment_depth_write = config.generalEmulation.enableFragmentDepthWrite != 0;
		cfg.n64_depth_compare = config.frameBufferEmulation.N64DepthCompare != 0;
		cfg.enable_lod = config.generalEmulation.enableLOD != 0;
		cfg.filter_mode = int(config.texture.bilinearMode + config.texture.enableHalosRemoval * 2);
		cfg.three_point = config.texture.bilinearMode == BILINEAR_3POINT;
		cfg.msaa = int(config.video.multisampling);
		cfg.noise_width = NOISE_TEX_WIDTH;
		cfg.noise_height = NOISE_TEX_HEIGHT;
		cfg.interlock = cfg.n64_depth_compare && gliden64_vulkan_has_interlock();
		u32 version = 0;
		const std::string cache = getCombinerJournalPath(version).append(".pipelines");
		g_r = vkr_create(gliden64_vulkan_interface(), &cfg, cache.c_str());
		if (g_r != nullptr)
			_windowSize();
	}
	void _recreate() {
		vkr_destroy(g_r);
		_create();
	}

	ClampMode m_clampMode;
	s32 m_unpack;
	u32 m_optionsBits = 0;
};

}

namespace vulkan {

ContextImpl * createContextImpl() { return new VkContextImpl; }

}

/* What framebuffer 0 holds goes to the frontend, as this frame's image. */
extern "C" void gliden64_vulkan_present(void)
{
	if (g_r != nullptr)
		vkr_present(g_r);
}
