#ifndef RDP_EMIT_RSP_H
#define RDP_EMIT_RSP_H

#include <stdint.h>

int32_t rsp_rcp32(int32_t in32);
int32_t rsp_rcp32_dp(int32_t in32);
int32_t rsp_rcp16(int32_t in16);
int32_t rsp_rsq32(int32_t in32);
void rsp_light_dir_xfrm_one(const int32_t mv[4][4],
                            const int32_t dir[3], int32_t out[3]);
void rsp_zsort_light_xfrm(const uint16_t mi[3][4], const uint16_t mf[3][4],
                          const int32_t dir[3], unsigned char out[4]);
int32_t rsp_clip_scale_w(int32_t w, int ratio);
void rsp_set_clip_lerp_204h(int on);
void rsp_set_vtx_invw_2rd(int on);
void rsp_set_tri_attr_rs(int on);
void rsp_set_vtx_invw_raw(int on);
void rsp_set_vtx_y_round(int on);
void rsp_set_vtx_x_round(int on);
void rsp_set_vtx_z_quant(int on);
void rsp_set_keep_degenerate(int on);
void rsp_set_affine_tex(int on);
void rsp_set_persp_skip(int on);
void rsp_set_attr_lowp(int on);
void rsp_set_clip_lerp_wo64(int on);
void rsp_set_clip_lerp_l3dex(int on);
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
int32_t rsp_vtx_fog_dkr(int32_t cz, int32_t cw,
                        int32_t fog_m, int32_t fog_o);
int32_t rsp_vtx_last_ndc2z(void);
int32_t rsp_vtx_last_pw(void);
int32_t rsp_vtx_last_outcode(void);
void rsp_clip_weights_rs(const int32_t in4[4], const int32_t out4[4],
                         const int16_t P[4], int32_t *wc, int32_t *wt);
int32_t rsp_clip_blend32_rs(int32_t a, int32_t b, int32_t wc, int32_t wt);
int32_t rsp_clip_blend16_rs(int32_t a, int32_t b, int32_t wc, int32_t wt);
int32_t rsp_tri_key_rs(int32_t z1, int32_t z2, int32_t z3);
int32_t rsp_interior_blend_rs(int32_t a, int32_t b, int32_t cur,
                              int32_t f2, int32_t g);
int32_t rsp_geomorph_blend_rs(int32_t a, int32_t b, int32_t cur,
                              int32_t wa, int32_t wb,
                              int32_t f, int32_t g);
int32_t rsp_fog_rs(int32_t sz1616,
                   int32_t m_i, int32_t m_f,
                   int32_t o_i, int32_t o_f, int32_t k);
int rsp_vtx_screen_rs(int32_t cx, int32_t cy, int32_t cz, int32_t cw,
                      int32_t pn,
                      const int32_t *vs, const int32_t *vt,
                      int32_t *sx102, int32_t *sy102, int32_t *sz1616,
                      int32_t *invw_out);
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
    int32_t  pw;        /* perspNorm'd w (the divide input; Rogue
                           Squadron's texture normalizer folds these) */
    int      flat2d;    /* raw attribute path: skip the perspective
                           normalizer and colour rounding (Fighting
                           Force 64's 2D overlay microcode) */
} RspTriVtx;

/* dx_scale/idy_scale/frac_mask/vcr_bound are the per-microcode triangle
 * setup constants; see GSPState tri_dx_scale and friends. */
int rsp_line_write(int32_t *cmd, const RspTriVtx *e0, const RspTriVtx *e1,
                   int width_q, int32_t dx_scale, int32_t idy_scale,
                   int32_t slope_mask, int32_t *xl_dmem, int zbuf);
void rsp_tri_set_rs_sort(int on);
void rsp_tri_set_d64_sort(int on);
int rsp_tri_write(int32_t *ew,
                  const RspTriVtx *v1c, const RspTriVtx *v2c,
                  const RspTriVtx *v3c,
                  int textured, int z_buffered, int shaded, int smooth,
                  int tile, int level,
                  int32_t dx_scale, int32_t idy_scale,
                  int32_t frac_mask, int32_t vcr_bound);

#endif
