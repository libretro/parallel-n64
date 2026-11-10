#ifndef DEPTHBUFFER_H
#define DEPTHBUFFER_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
#include "Textures.h"
#include "Graphics/ObjectHandle.h"
#include "Graphics/Parameter.h"

struct FrameBuffer;

struct DepthBuffer
{
	DepthBuffer();
	~DepthBuffer();
	void initDepthImageTexture(FrameBuffer * _pBuffer);
	void initDepthBufferTexture(FrameBuffer * _pBuffer);
	CachedTexture * resolveDepthBufferTexture(FrameBuffer * _pBuffer);
	CachedTexture * copyDepthBufferTexture(FrameBuffer * _pBuffer);

	void setDepthAttachment(graphics::ObjectHandle _fbo, graphics::BufferTargetParam _target);
	void activateDepthBufferTexture(FrameBuffer * _pBuffer);

	void bindDepthImageTexture(graphics::ObjectHandle _fbo);

	u32 m_address = 0;
	u32 m_width = 0;
	bool m_cleared = false;

	CachedTexture *m_pDepthBufferTexture = nullptr;

	graphics::ObjectHandle m_depthRenderbuffer;
	u32 m_depthRenderbufferWidth = 0;

	CachedTexture *m_pDepthImageZTexture = nullptr;
	CachedTexture *m_pDepthImageDeltaZTexture = nullptr;
	graphics::ObjectHandle m_ZTextureClearFBO;
	graphics::ObjectHandle m_DeltaZTextureClearFBO;

	// multisampling
	CachedTexture *m_pResolveDepthBufferTexture = nullptr;
	bool m_resolved = false;

	// render to depth buffer
	graphics::ObjectHandle m_copyFBO;
	CachedTexture *m_pDepthBufferCopyTexture = nullptr;
	bool m_copied = false;

private:
	void _initDepthImageTexture(FrameBuffer * _pBuffer, CachedTexture& _cachedTexture, graphics::ObjectHandle & _clearFBO);
	void _initDepthBufferTexture(FrameBuffer * _pBuffer, CachedTexture *_pTexture, bool _multisample);
	void _initDepthBufferRenderbuffer(FrameBuffer * _pBuffer);
};

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

struct DepthBuffer;
struct FrameBuffer;

/* DepthBuffer.c: the depth buffers the game has used, newest last */
void gli_depth_list_init(void);
void gli_depth_list_destroy(void);
void gli_depth_save(u32 _address);
void gli_depth_clear(void);
void gli_depth_set_cleared(bool _cleared);
struct DepthBuffer * gli_depth_find(u32 _address);
struct DepthBuffer * gli_depth_current_buffer(void);
const u16 * gli_depth_zlut(void);

/* DepthBufferGpu.cpp */
struct DepthBuffer * gli_depth_new(u32 _address, struct FrameBuffer * _pBuffer);
void gli_depth_delete(struct DepthBuffer * _pBuffer);
u32 gli_depth_address(const struct DepthBuffer * _pBuffer);
u32 gli_depth_width(const struct DepthBuffer * _pBuffer);
void gli_depth_mark_cleared(struct DepthBuffer * _pBuffer, bool _cleared);
void gli_depth_gpu_clear(void);

#ifdef __cplusplus
}
#endif

#endif
