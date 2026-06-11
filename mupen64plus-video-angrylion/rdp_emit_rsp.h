#ifndef RDP_EMIT_RSP_H
#define RDP_EMIT_RSP_H

#include <stdint.h>

int32_t rsp_rcp32(int32_t in32);
int32_t rsp_rcp16(int32_t in16);
int32_t rsp_rsq32(int32_t in32);
void rsp_light_dir_xfrm_one(const int32_t mv[4][4],
                            const int32_t dir[3], int32_t out[3]);
int32_t rsp_clip_scale_w(int32_t w, int ratio);
void rsp_clip_lerp(const int32_t on_pos[4], const int32_t off_pos[4],
                   const int16_t cr[4],
                   const int16_t on_attr[8], const int16_t off_attr[8],
                   int32_t out_pos[4], int16_t out_attr[8]);
void rsp_texgen(const int32_t n[3], const int32_t l0[3], const int32_t l1[3],
                int linear, int32_t *s_out, int32_t *t_out);
int32_t rsp_light_dirdot(const int32_t n[3], const int32_t d[3]);
void rsp_light_fold1(int32_t lt[3], const int32_t rgb[3], int32_t d);
int32_t rsp_light_point_factor(const int32_t mv[4][4], const int32_t n[3],
                               const int32_t vtx[3], const int32_t pos[3],
                               int32_t kc, int32_t kl, int32_t kq);
void rsp_light_vtx(const int32_t n[3], const int32_t amb[3],
                   const int32_t (*rgb)[3], const int32_t (*dirs)[3],
                   int num, int32_t out[3]);
int32_t rsp_vtx_invw(int32_t w);
int32_t rsp_vtx_fog(int32_t cz, int32_t cw, int32_t pn,
                    int32_t fog_m, int32_t fog_o);
int rsp_vtx_screen(int32_t cx, int32_t cy, int32_t cz, int32_t cw,
                   int32_t pn,
                   int32_t vsx, int32_t vsy, int32_t vsz,
                   int32_t vtx_, int32_t vty, int32_t vtz,
                   int32_t *sx102, int32_t *sy102, int32_t *sz1616,
                   int32_t *invw_out);

typedef struct RspTriVtx
{
    int16_t  x, y;      /* screen position, 10.2 */
    int32_t  z;         /* screen z, 16.16 */
    int32_t  r, g, b, a;/* 8-bit colour values */
    int32_t  s, t;      /* texture coordinates as stored in VTX_TC_VEC */
    int32_t  invw;      /* VTX_INV_W 32-bit value (rsp_vtx_invw) */
} RspTriVtx;

/* dx_scale/idy_scale/frac_mask/vcr_bound are the per-microcode triangle
 * setup constants; see GSPState tri_dx_scale and friends. */
int rsp_tri_write(int32_t *ew,
                  const RspTriVtx *v1c, const RspTriVtx *v2c,
                  const RspTriVtx *v3c,
                  int textured, int z_buffered, int smooth,
                  int tile, int level,
                  int32_t dx_scale, int32_t idy_scale,
                  int32_t frac_mask, int32_t vcr_bound);

#endif
