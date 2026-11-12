#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <list>
#include <vector>

#include "Types.h"
#include "gli_vi_update.h"
#include "gli_fb_rdram.h"
#include "gli_fb_list.h"
#include "Textures.h"
#include "Graphics/ObjectHandle.h"

struct gDPTile;
struct DepthBuffer;


struct FrameBuffer : gli_fb
{
	FrameBuffer();
	~FrameBuffer();
	/* owns its RDRAM copy */
	FrameBuffer(const FrameBuffer &) = delete;
	FrameBuffer & operator=(const FrameBuffer &) = delete;
	void init(u32 _address, u16 _format, u16 _size, u16 _width, bool _cfb);
	void updateEndAddress();
	void resolveMultisampledTexture(bool _bForce = false);
	CachedTexture * getTexture(u32 _t);
	CachedTexture * getTextureBG(u32 _t);
	void setBufferClearParams(u32 _fillcolor, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
	void copyRdram();
	void setDirty();
	bool isValid(bool _forceCheck) const;
	bool isAuxiliary() const;

	graphics::ObjectHandle m_FBO;
	CachedTexture *m_pTexture;
	DepthBuffer *m_pDepthBuffer;

	// multisampling
	graphics::ObjectHandle m_resolveFBO;
	CachedTexture *m_pResolveTexture;

	// subtexture
	graphics::ObjectHandle m_SubFBO;
	CachedTexture *m_pSubTexture;

	// copy FBO
	graphics::ObjectHandle m_copyFBO;
	CachedTexture * m_pFrameBufferCopyTexture;


private:
	void _initTexture(u16 _width, u16 _height, u16 _format, u16 _size, CachedTexture *_pTexture);
	void _setAndAttachTexture(graphics::ObjectHandle _fbo, CachedTexture *_pTexture, u32 _t, bool _multisampling);
	bool _initSubTexture(u32 _t);
	void _initCopyTexture();
	CachedTexture * _copyFrameBufferTexture();
	CachedTexture * _getSubTexture(u32 _t);

};

class FrameBufferList
{
public:
	void init();
	void destroy();
	/* lends a buffer in the list RDRAM copy storage for @_capacity bytes */
	void lendRdramCopy(FrameBuffer * _pBuffer, u32 _capacity) { gli_fb_list_lend_copy(&m_fbs, _pBuffer, _capacity); }
	void saveBuffer(u32 _address, u16 _format, u16 _size, u16 _width, bool _cfb);
	void removeAux();
	void copyAux();
	void removeBuffer(u32 _address);
	void removeBuffers(u32 _width);
	void attachDepthBuffer();
	void clearDepthBuffer(DepthBuffer * _pDepthBuffer);
	FrameBuffer * findBuffer(u32 _startAddress);
	FrameBuffer * getBuffer(u32 _startAddress);
	FrameBuffer * findTmpBuffer(u32 _address);
	FrameBuffer * getCurrent() const {return m_pCurrent;}
	void setCurrent(FrameBuffer * _pCurrent) { m_pCurrent = _pCurrent; }
	void updateCurrentBufferEndAddress();
	void renderBuffer();
	void setBufferChanged(f32 _maxY);
	void clearBuffersChanged();
	void setCurrentDrawBuffer() const;
	void fillRDRAM(s32 ulx, s32 uly, s32 lrx, s32 lry);

	FrameBuffer * getCopyBuffer() const { return m_pCopy; }
	void setCopyBuffer(FrameBuffer * _pBuffer) { m_pCopy = _pBuffer; }
	void depthBufferCopyRdram();

	void fillBufferInfo(void * _pinfo, u32 _size);

	static FrameBufferList & get();

private:
	FrameBufferList() : m_pCurrent(nullptr), m_pCopy(nullptr), m_prevColorImageHeight(0) { gli_fb_list_init(&m_fbs); }
	FrameBufferList(const FrameBufferList &) = delete;

	void removeIntersections();

	void _createScreenSizeBuffer();
	void _renderScreenSizeBuffer();

	class OverscanBuffer
	{
	public:
		void init();
		void destroy();

		void setInputBuffer(const FrameBuffer *  _pBuffer);
		void activate();
		/** Draws into the window at _x, of _windowWidth, clearing it first when _clear */
		void draw(u32 _fullHeight, bool _PAL, s32 _x, u32 _windowWidth, bool _clear);

		s32 getHOffset() const;
		s32 getVOffset() const;
		f32 getScaleX() const;
		f32 getScaleY(u32 _fullHeight) const;
		u32 getDrawingWidth() const { return m_drawingWidth; }
		u32 getBufferWidth() const { return m_bufferWidth; }
		u32 getBufferHeight() const { return m_bufferHeight; }

	private:
		s32 m_hOffset = 0;
		s32 m_vOffset = 0;
		f32 m_scale = 1.0f;
		u32 m_drawingWidth = 0U;
		u32 m_bufferWidth = 0U;
		u32 m_bufferHeight = 0U;
		bool m_enabled = false;

		graphics::ObjectHandle m_FBO;
		CachedTexture *m_pTexture = nullptr;
	};

	/* the buffers live in fixed slots, built in place; the C list keeps
	 * their order */
	gli_fb_list m_fbs;
	alignas(FrameBuffer) unsigned char m_storage[GLI_FB_MAX][sizeof(FrameBuffer)];
	FrameBuffer * _slot(u32 _slot) { return reinterpret_cast<FrameBuffer *>(m_storage[_slot]); }
	FrameBuffer * _at(u32 _pos) { return _slot(gli_fb_list_at(&m_fbs, _pos)); }
	FrameBuffer & _addFront();
	/* destroys the buffers taken out of the list, forgetting the current one */
	void _destroyRemoved(const u8 * _removed, u32 _count);
	FrameBuffer * m_pCurrent;
	FrameBuffer * m_pCopy;
	u32 m_prevColorImageHeight;
	OverscanBuffer m_overscan;

	typedef gli_vi_result RdpUpdateResult;
	gli_vi_state m_rdpUpdate;
};

inline
FrameBufferList & frameBufferList()
{
	return FrameBufferList::get();
}

void calcCoordsScales(const FrameBuffer * _pBuffer, f32 & _scaleX, f32 & _scaleY);

void FrameBuffer_Init();
void FrameBuffer_Destroy();
void FrameBuffer_CopyToRDRAM( u32 _address , bool _sync );
void FrameBuffer_CopyChunkToRDRAM(u32 _address);
void FrameBuffer_CopyFromRDRAM(u32 address, bool bUseAlpha);
void FrameBuffer_AddAddress(u32 address, u32 _size);
bool FrameBuffer_CopyDepthBuffer(u32 address);
bool FrameBuffer_CopyDepthBufferChunk(u32 address);
void FrameBuffer_ActivateBufferTexture(u32 t, u32 _frameBufferAddress);
void FrameBuffer_ActivateBufferTextureBG(u32 t, u32 _frameBufferAddress);

#endif
