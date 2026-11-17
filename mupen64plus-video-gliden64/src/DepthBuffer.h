#ifndef DEPTHBUFFER_H
#define DEPTHBUFFER_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gli_tex;
struct gli_fb;
struct FrameBuffer;

/* A depth buffer: the texture or renderbuffer a frame buffer's depth is
 * drawn into, the N64 depth images (Z and delta Z) and their clear frame
 * buffers, the resolve of a multisampled one and the copy read while it is
 * drawn to */
typedef struct DepthBuffer
{
	u32 m_address;
	u32 m_width;
	bool m_cleared;

	struct gli_tex *m_pDepthBufferTexture;

	u32 m_depthRenderbuffer;
	u32 m_depthRenderbufferWidth;

	struct gli_tex *m_pDepthImageZTexture;
	struct gli_tex *m_pDepthImageDeltaZTexture;
	u32 m_ZTextureClearFBO;
	u32 m_DeltaZTextureClearFBO;

	/* multisampling */
	struct gli_tex *m_pResolveDepthBufferTexture;
	bool m_resolved;

	/* render to depth buffer */
	u32 m_copyFBO;
	struct gli_tex *m_pDepthBufferCopyTexture;
	bool m_copied;
} DepthBuffer;

/* the depth image textures, for N64 depth compare, once */
void gli_depth_init_image_texture(DepthBuffer *_pDepth, const struct gli_fb *_pBuffer);
/* the texture or renderbuffer for @_pBuffer, NULL for the screen */
void gli_depth_init_texture(DepthBuffer *_pDepth, const struct gli_fb *_pBuffer);
/* the depth texture single-sampled, resolved into once per draw to it */
struct gli_tex *gli_depth_resolve_texture(DepthBuffer *_pDepth, const struct gli_fb *_pBuffer);
/* a copy of the depth texture to read while it is drawn to */
struct gli_tex *gli_depth_copy_texture(DepthBuffer *_pDepth, const struct gli_fb *_pBuffer);
/* attached as the depth of frame buffer @_fbo, bound to @_target */
void gli_depth_set_attachment(DepthBuffer *_pDepth, u32 _fbo, u32 _target);
void gli_depth_activate_texture(DepthBuffer *_pDepth, const struct gli_fb *_pBuffer);
/* the depth images bound for @_fbo's draws: image units, or attachments */
void gli_depth_bind_image_texture(DepthBuffer *_pDepth, u32 _fbo);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct DepthBuffer;
struct FrameBuffer;

/* The most depth buffers alive at once; the oldest makes way past it */
#define GLI_DEPTH_MAX 64

/* DepthBuffer.c: the depth buffers the game has used, newest last */
void gli_depth_list_init(void);
void gli_depth_list_destroy(void);
void gli_depth_save(u32 _address);
void gli_depth_clear(void);
void gli_depth_set_cleared(bool _cleared);
struct DepthBuffer * gli_depth_find(u32 _address);
struct DepthBuffer * gli_depth_current_buffer(void);
const u16 * gli_depth_zlut(void);

/* a depth buffer of @_address for @_pBuffer, NULL for the screen */
struct DepthBuffer * gli_depth_new(u32 _address, const struct gli_fb * _pBuffer);
void gli_depth_delete(struct DepthBuffer * _pBuffer);
u32 gli_depth_address(const struct DepthBuffer * _pBuffer);
u32 gli_depth_width(const struct DepthBuffer * _pBuffer);
void gli_depth_mark_cleared(struct DepthBuffer * _pBuffer, bool _cleared);
void gli_depth_gpu_clear(void);

#ifdef __cplusplus
}
#endif

#endif
