#pragma once
#include <memory>
#include <array>
#include <vector>
#include <list>
#include <chrono>
#include <string>
#include "gSP.h"
#include "gli_drawer.h"
#include "TexrectDrawer.h"
#include "Graphics/ObjectHandle.h"
#include "Graphics/Parameters.h"

namespace graphics {
	class CombinerProgram;
}

struct CachedTexture;
struct FrameBuffer;

#define VERTBUFF_SIZE 256U
#define ELEMBUFF_SIZE 1024U

enum class DrawingState
{
	Non = 0,
	Line = 1,
	Triangle = 2,
	Rect = 3,
	TexRect = 4,
};

typedef std::chrono::milliseconds Milliseconds;

class GraphicsDrawer
{
public:

	void drawTriangles();

	void drawScreenSpaceTriangle(u32 _numVtx, graphics::DrawModeParam _mode = graphics::drawmode::TRIANGLE_STRIP);

	void drawDMATriangles(u32 _numVtx);

	void drawLine(u32 _v0, u32 _v1, float _width, u32 _flag);

	void drawRect(int _ulx, int _uly, int _lrx, int _lry);

	struct TexturedRectParams : gli_texrect_params
	{
		const FrameBuffer * pBuffer;
		TexturedRectParams(float _ulx, float _uly, float _lrx, float _lry,
			float _dsdx, float _dtdy,
			s16 _s, s16 _t,
			bool _flip, bool _forceAjustScale, bool _texrectCmd,
			const FrameBuffer * _pBuffer
			) :
			pBuffer(_pBuffer)
		{
			ulx = _ulx; uly = _uly; lrx = _lrx; lry = _lry;
			dsdx = _dsdx; dtdy = _dtdy;
			s = _s; t = _t;
			flip = _flip; forceAjustScale = _forceAjustScale; texrectCmd = _texrectCmd;
		}
	};

	void correctTexturedRectParams(TexturedRectParams & _params);

	void drawTexturedRect(const TexturedRectParams & _params);

	struct CopyRectParams : gli_copy_rect
	{
		CopyRectParams()
		{
			srcX0 = srcY0 = 0;
			dstX0 = dstY0 = 0;
			invertX = invertY = 0;
		}
		typedef std::array<CachedTexture *, 2> Textures;
		Textures tex = Textures{ { nullptr, nullptr } };
		graphics::CombinerProgram * combiner = nullptr;
		graphics::TextureParam filter;
	};

	void copyTexturedRect(const CopyRectParams & _params);

	struct BlitOrCopyRectParams : public CopyRectParams
	{
		graphics::ObjectHandle readBuffer;
		graphics::ObjectHandle drawBuffer;
		graphics::BlitMaskParam mask;
	};

	void blitOrCopyTexturedRect(const BlitOrCopyRectParams & _params);

	void clearDepthBuffer();

	void clearColorBuffer(float * _pColor);

	int getTrianglesCount() const { return triangles.num; }

	bool isClipped(s32 _v0, s32 _v1, s32 _v2) const
	{
		return (triangles.vertices[_v0].clip & triangles.vertices[_v1].clip & triangles.vertices[_v2].clip) != 0;
	}


	SPVertex & getVertex(u32 _v) { return triangles.vertices[_v]; }

	SPVertex * getVertexPtr(u32 _v) { return triangles.vertices.data() + _v; }

	SPVertex * getDMAVerticesData() { return m_dmaVertices.data(); }

	SPVertex & getCurrentDMAVertex();
	u32 getDMAVerticesCount() const { return m_dmaVerticesNum; }
	u32 getDMAVerticesCapacity() const { return (u32)m_dmaVertices.size(); }

	void updateScissor(FrameBuffer * _pBuffer) const;

	DrawingState getDrawingState() const { return m_drawingState; }

	void dropRenderState() { m_drawingState = DrawingState::Non; }

	void flush() { gli_texrect_draw(); }

	bool isTexrectDrawerMode() const { return !gli_texrect_is_empty(); }

	void setBackgroundDrawingMode(bool _mode) { m_bBGMode = _mode; }

private:
	friend class DisplayWindow;
	friend struct WindowGpu;
	friend struct TexrectGpu;

	GraphicsDrawer();
	~GraphicsDrawer();

	GraphicsDrawer(const GraphicsDrawer &) = delete;

	void _initStates();
	void _initData();
	void _destroyData();

	void _setSpecialTexrect() const;

	void _setBlendMode() const;
	void _updateCullFace() const;
	void _updateViewport() const;
	void _updateScreenCoordsViewport(const FrameBuffer * _pBuffer = nullptr) const;
	void _updateDepthUpdate() const;
	void _updateDepthCompare() const;
	void _updateTextures() const;
	void _updateStates(DrawingState _drawingState) const;
	void _prepareDrawTriangle();
	bool _canDraw() const;

	DrawingState m_drawingState;
	f32 m_texrectRect[4]; /* the last texrect, ulx, uly, lrx, lry */

	struct {
		std::array<SPVertex, VERTBUFF_SIZE> vertices;
		std::array<u16, ELEMBUFF_SIZE> elements;
		u32 num = 0;
		int maxElement = 0;
	} triangles;

	std::vector<SPVertex> m_dmaVertices;
	u32 m_dmaVerticesNum;

	RectVertex m_rect[4];

	u32 m_modifyVertices;
	f32 m_maxLineWidth;
	bool m_bFlatColors;
	bool m_bBGMode;
};
