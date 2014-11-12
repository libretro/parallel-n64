#define T0_INTER_T1_USING_T1() \
  if (!cmb.combine_ext) { \
  T0_INTER_T1_USING_FACTOR(0x7F); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define T0_INTER_T1_USING_PRIM() \
  if (!cmb.combine_ext) { \
  T0_INTER_T1_USING_FACTOR ((rdp.prim_color&0xFF)); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_TMU_CCOLOR, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_ccolor = rdp.prim_color, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define T1_INTER_T0_USING_PRIM() /* inverse of above */\
  if (!cmb.combine_ext) { \
  T1_INTER_T0_USING_FACTOR ((rdp.prim_color&0xFF)); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_TMU_CCOLOR, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_ccolor = rdp.prim_color, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define A_T0_INTER_T1_USING_SHADEA() \
  if (!cmb.combine_ext) { \
  A_T0_INTER_T1_USING_FACTOR (0x7F); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1a_ext_a = GR_CMBX_LOCAL_TEXTURE_ALPHA, \
  cmb.t1a_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1a_ext_b = GR_CMBX_LOCAL_TEXTURE_ALPHA, \
  cmb.t1a_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1a_ext_c = GR_CMBX_ZERO, \
  cmb.t1a_ext_c_invert = 0, \
  cmb.t1a_ext_d= GR_CMBX_B, \
  cmb.t1a_ext_d_invert = 0, \
  cmb.t0a_ext_a = GR_CMBX_OTHER_TEXTURE_ALPHA, \
  cmb.t0a_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0a_ext_b = GR_CMBX_LOCAL_TEXTURE_ALPHA, \
  cmb.t0a_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0a_ext_c = GR_CMBX_ITALPHA, \
  cmb.t0a_ext_c_invert = 0, \
  cmb.t0a_ext_d= GR_CMBX_B, \
  cmb.t0a_ext_d_invert = 0, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_ALPHA; \
}

#define T0_INTER_T1_USING_ENV() \
  if (!cmb.combine_ext) { \
  T0_INTER_T1_USING_FACTOR ((rdp.env_color&0xFF)); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_TMU_CCOLOR, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_ccolor = rdp.env_color, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define T1_INTER_T0_USING_ENV() /* inverse of above */\
  if (!cmb.combine_ext) { \
  T1_INTER_T0_USING_FACTOR ((rdp.env_color&0xFF)); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_TMU_CCOLOR, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_ccolor = rdp.env_color, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define T0_INTER_T1_USING_SHADEA() \
  if (!cmb.combine_ext) { \
  T0_INTER_T1_USING_FACTOR (0x7F); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_ITALPHA, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define T1_INTER_T0_USING_SHADEA() \
  if (!cmb.combine_ext) { \
  T0_INTER_T1_USING_FACTOR (0x7F); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 3, \
  cmb.t1c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_a_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_b = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t1c_ext_b_mode = GR_FUNC_MODE_ZERO, \
  cmb.t1c_ext_c = GR_CMBX_ZERO, \
  cmb.t1c_ext_c_invert = 0, \
  cmb.t1c_ext_d= GR_CMBX_B, \
  cmb.t1c_ext_d_invert = 0, \
  cmb.t0c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_OTHER_TEXTURE_RGB, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_ITALPHA, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

#define T1_SUB_PRIM_MUL_PRIMLOD_ADD_T0() \
  if (cmb.combine_ext) \
{ \
  T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X, \
  GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X, \
  GR_CMBX_DETAIL_FACTOR, 0, \
  GR_CMBX_ZERO, 0); \
  T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X, \
  GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X, \
  GR_CMBX_ZERO, 1, \
  GR_CMBX_ZERO, 0); \
  cmb.tex_ccolor = rdp.prim_color; \
  cmb.tex |= 3; \
  percent = (float)(lod_frac) / 255.0f; \
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent; \
} \
  else \
{  \
  T0_ADD_T1 (); \
  MOD_1 (TMOD_TEX_SUB_COL_MUL_FAC); \
  MOD_1_COL (rdp.prim_color & 0xFFFFFF00); \
  MOD_1_FAC (lod_frac & 0xFF); \
}

#define PRIM_INTER_T0_USING_SHADEA() \
  if (!cmb.combine_ext) { \
  USE_T0 (); \
  }\
  else {\
  rdp.best_tex = 0; \
  cmb.tex |= 1, \
  cmb.t0c_ext_a = GR_CMBX_LOCAL_TEXTURE_RGB, \
  cmb.t0c_ext_a_mode = GR_FUNC_MODE_X, \
  cmb.t0c_ext_b = GR_CMBX_TMU_CCOLOR, \
  cmb.t0c_ext_b_mode = GR_FUNC_MODE_NEGATIVE_X, \
  cmb.t0c_ext_c = GR_CMBX_ITALPHA, \
  cmb.t0c_ext_c_invert = 0, \
  cmb.t0c_ext_d= GR_CMBX_B, \
  cmb.t0c_ext_d_invert = 0, \
  cmb.tex_ccolor = rdp.prim_color, \
  cmb.tex_cmb_ext_use |= TEX_COMBINE_EXT_COLOR; \
}

//forward decls
static void cc_t0_mul_env_mul_shade(void);
static void cc_t0_mul_shade(void);
static void cc_env (void);
static void cc_t0_mul_prim (void);
static void cc_t0(void);

static void cc__t0_inter_one_using_primlod__mul_prim (void)
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIM ();
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (0xFFFFFF00);
    MOD_0_FAC (lod_frac);
    USE_T0 ();
  }
}

static void cc__t1_inter_one_using_env__mul_prim ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIM ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 1,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 2;
    cmb.tex_ccolor = rdp.env_color;
  }
  else
  {
    USE_T1 ();
  }
}

static void cc__t1_inter_one_using_enva__mul_t0 ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = 0xFFFFFF00 | (rdp.env_color&0xFF);
  }
  else
  {
    if ((rdp.env_color&0xFF) == 0xFF)
    {
      USE_T0 ();
    }
    else
    {
      T0_MUL_T1 ();
    }
  }
}

static void cc__t0_mul_shade__add__t1_mul_shade ()
{
  //combiner is used in Spiderman. It seems that t0 is used instead of t1
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_B, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    USE_T0 ();
  }
}

static void ac__t0_inter_t1_using_primlod__sub_env_mul_shade_add_shade ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ALOCAL, 0);
    CA_ENV ();
    A_T0_INTER_T1_USING_FACTOR (lod_frac);
  }
  else
    ac__t0_inter_t1_using_primlod__mul_shade ();
}

static void cc__t0_mul_prim__inter_env_using_enva ()
{
  uint32_t enva  = rdp.env_color&0xFF;
  if (enva == 0xFF)
    cc_env ();
  else if (enva == 0)
    cc_t0_mul_prim ();
  else if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    SETSHADE_ENV();
    CC_ENVA();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    SETSHADE_PRIM();
    INTERSHADE_2 (rdp.env_color & 0xFFFFFF00, rdp.env_color & 0xFF);
    USE_T0 ();
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.env_color & 0xFF);
  }
}

static void cc__t1_inter_t0_using_t1__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_B, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
  }
  else
  {
    T0_INTER_T1_USING_FACTOR (0x7F);
  }
}

//Added by Gonetz
static void cc__t1_inter_t0_using_shadea__mul_shade ()
{
  if (!cmb.combine_ext) {
    cc_t0_mul_shade ();
    return;
  }
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
  T1_INTER_T0_USING_SHADEA ();
}

//Added by Gonetz
static void cc__t0_inter_one_using_prim__mul_shade ()
{
  // (1-t0)*prim+t0, (cmb-0)*shade+0
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
  }
  else
  {
    USE_T0 ();
    MOD_0 (TMOD_TEX_INTER_COL_USING_COL1);
    MOD_0_COL (0xFFFFFF00);
    MOD_0_COL1 (rdp.prim_color & 0xFFFFFF00);
  }
}

static void cc__t0_inter_one_using_primlod__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (0xFFFFFF00);
    MOD_0_FAC (lod_frac);
    USE_T0 ();
  }
}

//Added by Gonetz
static void cc__t0_inter_env_using_enva__mul_shade ()
{
  // (env-t0)*env_a+t0, (cmb-0)*shade+0
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.env_color;
  }
  else
  {
    USE_T0 ();
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.env_color&0xFF);
  }
}

//Added by Gonetz
static void cc__t0_inter_env_using_shadea__mul_shade ()
{
  // (env-t0)*shade_a+t0, (cmb-0)*shade+0
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.env_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    cc_t0_mul_shade ();
  }
}

static void cc__t0_mul_prim_add_env__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_TEX_SCALE_COL_ADD_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_COL1 (rdp.env_color & 0xFFFFFF00);
	USE_T0 ();
  }
}

static void cc__t1_sub_t0_mul_primlod_add_prim__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    T0_INTER_T1_USING_FACTOR (lod_frac);
  }
}

static void cc__t1_sub_prim_mul_t0__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 3;
  }
  else
  {
    T0_MUL_T1 ();
  }
}

static void cc__t1_sub_t0_mul_t0_add_shade__mul_shade (void) //Aded by Gonetz
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_ITRGB, 0);
    cmb.tex |= 3;
  }
  else
  {
    T1_SUB_T0_MUL_T0 ();
  }
}

static void cc__one_sub_shade_mul_t0_add_shade__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
	  GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE_MINUS_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    USE_T0 ();
  }
}

static void cc__t0_sub_prim_mul_t1_add_t1__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (rdp.prim_color & 0xFFFFFF00)
  {
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
  }
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
  }
  else
  {
    T0_MUL_T1 ();
  }
}

static void cc__t1_sub_env_mul_t0_add_t0__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 3;
  }
  else
  {
    MOD_1 (TMOD_TEX_SUB_COL);
    MOD_1_COL (rdp.env_color & 0xFFFFFF00);
    T0_MUL_T1_ADD_T0 ();
  }
}

static void cc__t0_mul_prima_add_prim_mul__shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
  }
  else
  {
    MOD_0 (TMOD_TEX_SCALE_FAC_ADD_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.prim_color & 0xFF);
    USE_T0 ();
  }
}

static void cc__t0_inter_prim_using_prima__inter_env_using_enva ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    CC_ENVA ();
    SETSHADE_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CC_1SUBENVA ();
    SETSHADE_ENV ();
    SETSHADE_ENVA ();
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.prim_color & 0xFF);
    USE_T0 ();
  }
}

static void cc_prim_inter_t1_mul_shade_using_texa ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
  }
  else
  {
    cc_t1_mul_shade ();
  }
}

static void cc__prim_inter_t0_using_t0a__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
  }
  else
  {
    MOD_0 (TMOD_COL_INTER_TEX_USING_TEXA);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__prim_inter_t0_using_t0a__inter_env_using_enva ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    CC_ENVA ();
    SETSHADE_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CC_1SUBENVA ();
    SETSHADE_ENV ();
    SETSHADE_ENVA ();
    MOD_0 (TMOD_COL_INTER_TEX_USING_TEXA);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__shade_inter_t0_using_shadea__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    cc_t0_sub_shade_mul_shadea_add_shade ();
  }
}

static void cc__t0_mul_t1__add_env_mul__t0_mul_t1__add_env ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
    cc__t0_mul_t1__add_env();
}

static void cc_t0_add_prim_mul_one_sub_t0_add_t0 () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 1,
      GR_CMBX_B, 0);
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
  }
  else
  {
    cc_t0_add_prim ();
  }
}

static void cc__one_sub_prim_mul_t0_add_prim__mul_prima_add__one_sub_prim_mul_t0_add_prim () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 1,
      GR_CMBX_B, 0);
    CCMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    CC_PRIMA();
    cmb.tex |= 3; //hw frame buffer allocated as tile1, but not used in combiner
  }
  else
  {
    cc_one_sub_prim_mul_t0_add_prim();
    //    cc_t0 ();
  }
}

// ** A-B*C **
static void cc_env_sub__t0_sub_t1_mul_primlod__mul_prim () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    SETSHADE_PRIM ();
    SETSHADE_PRIMLOD ();
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    SETSHADE_PRIM ();
    CC_ENV ();
    T1_INTER_T0_USING_FACTOR (lod_frac);
  }
}

static void cc_env_sub__t0_mul_scale_add_env__mul_prim ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.SCALE;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    SETSHADE_ENV ();
    CC_PRIM ();
  }
  else
    cc_t0_add_env ();
}

static void cc_one_sub__one_sub_t0_mul_enva_add_prim__mul_prim () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    percent = (float)(rdp.env_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    CCMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 1);
    CC_PRIM ();
  }
  else
  {
    cc_one ();
  }
}

static void cc__t0_sub_env_mul_enva__add_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
    percent = (float)(rdp.env_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;

    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
  }
  else {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
	  GR_COMBINE_FACTOR_ONE,
	  GR_COMBINE_LOCAL_ITERATED,
	  GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_PRIM ();
	  MOD_0 (TMOD_TEX_SUB_COL_MUL_FAC);
	  MOD_0_COL (rdp.env_color & 0xFFFFFF00);
	  MOD_0_FAC (rdp.env_color & 0xFF);
	USE_T0 ();
  }
}

// ** A*B+C **
//Added by Gonetz
static void cc_t0_mul_prim_add_t1 ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = rdp.prim_color;
  }
  else
  {
    MOD_0 (TMOD_TEX_MUL_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    T0_ADD_T1 ();
  }
}

static void cc_shirt ()
{
  // (t1-0)*prim+0, (1-t0)*t1+cmb
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    /*
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_TMU_CCOLOR, 0,
    GR_CMBX_ZERO, 0);
    //*/
    //*
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    //*/
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 1,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = rdp.prim_color;
  }
  else
  {
    MOD_1 (TMOD_TEX_MUL_COL);
    MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    T0_ADD_T1 ();
  }
}

static void cc__t0_add_primlod__mul_prim_add_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIMLOD ();
    cmb.tex_ccolor = cmb.ccolor;
    CC_ENV ();
    SETSHADE_PRIM ();
    cmb.tex |= 1;
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    CC_PRIMLOD ();
    MOD_0 (TMOD_TEX_ADD_COL);
    MOD_0_COL (cmb.ccolor & 0xFFFFFF00);
    SETSHADE_PRIM ();
    CC_ENV ();
    USE_T0 ();
  }
}

static void cc__t1_sub_prim_mul_enva_add_t0__mul_prim_add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_CONSTANT);
  CC_PRIM ();
  SETSHADE_ENV ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 3;
    percent = (float)(rdp.env_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    MOD_1 (TMOD_TEX_SUB_COL_MUL_FAC);
    MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_1_FAC (rdp.env_color & 0xFF);
    T0_ADD_T1 ();
  }
}

//Aded by Gonetz
static void cc__t0_mul_t1__sub_prim_mul_env_add_shade ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_TEXTURE_RGB, 0);
    CC_PRIMMULENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CC_ENV ();
    T0_MUL_T1 ();
  }
}

static void cc__t0_sub_prim_mul_t1_add_t1__mul_env_add_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_CONSTANT);
  CC_ENV ();
  if (rdp.prim_color & 0xFFFFFF00)
  {
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
  }
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
  }
  else
  {
    T0_MUL_T1 ();
  }
}

static void cc__t0_mul_shadea_add_env__mul_shade_add_prim ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    MULSHADE_SHADEA ();
    CC_PRIM ();
    USE_T0 ();
  }
}

//Added by Gonetz
static void cc__t1_sub_t0_mul_primlod_add_prim__mul_shade_add_shade ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ITRGB, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    T0_INTER_T1_USING_FACTOR (lod_frac);
  }
}

static void cc__t0_mul_enva_add_t1__mul_shade_add_prim ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_PRIM ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    percent = (float)(rdp.env_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    T0_ADD_T1 ();
  }
}

static void cc__t0_add_prim__mul_shade_add_t0 ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    //    MOD_0 (TMOD_TEX_ADD_COL);
    //    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
  }
  USE_T0 ();
}

static void cc__t0_add_prim__mul_shade_add_t1 ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_PRIM ();
    T0_ADD_T1 ();
  }
}

static void cc__t0_add_primlod__mul_shade_add_env ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIMLOD ();
    cmb.tex_ccolor = cmb.ccolor;
    CC_ENV ();
    cmb.tex |= 1;
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    uint32_t color = (lod_frac<<24) | (lod_frac<<16) | (lod_frac<<8);
    MOD_0 (TMOD_TEX_ADD_COL);
    MOD_0_COL (color & 0xFFFFFF00);
    CC_ENV ();
    USE_T0 ();
  }
}

static void cc__t0_mul_prima_add_prim_mul__shade_add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
  }
  else
  {
    MOD_0 (TMOD_TEX_SCALE_FAC_ADD_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.prim_color & 0xFF);
    USE_T0 ();
  }
}

static void cc__t1_mul_t1_add_t0__mul_prim ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIM ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_OTHER_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
  }
  else
  {
    T0_ADD_T1 ();
  }
}

// (A*B+C)*D
static void cc__t0_mul_prim_add_shade__mul_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_ENV ();
    MOD_0 (TMOD_TEX_MUL_COL);
    CC_PRIMMULENV ();
    MOD_0_COL (cmb.ccolor & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__prim_mul_shade_add_env__mul_shade () //Aded by Gonetz
{
  if (!cmb.combine_ext)
  {
    cc_prim_mul_shade_add_env ();
    return;
  }
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
  cmb.tex |= 1;
  cmb.tex_ccolor = rdp.prim_color;
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
  CC_ENV ();
}

// ** A*B*C+D*E **
//Added by Gonetz
static void cc__t0_sub_t1__mul_prim_mul_shade_add_prim_mul_env ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
  }
  else
  {
    USE_T0 ();
  }

  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_PRIMMULENV ();
  MULSHADE_PRIM ();
}

// ** (A+B)*C **
static void cc_t0_mul_scale_add_prim__mul_shade () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.SCALE;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_TEX_ADD_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__t0_mul_t1_add_prim__mul_shade () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_PRIM ();
  }
  T0_MUL_T1 ();
}

// ** (A-B)*C **
static void cc__t0_mul_prim_add_shade__sub_env_mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    cc_t0_mul_prim_mul_shade ();
  }
}

static void cc__t0_sub_env_mul_shade__sub_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ITRGB, 0);
    CC_PRIM ();
  }
  else
  {
    cc_t0_mul_shade ();
  }
}

static void cc_t0_sub_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    if (rdp.prim_color & 0xFFFFFF00)
    {
      MOD_0 (TMOD_TEX_SUB_COL);
      MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    }
  }
  USE_T0 ();
}

static void cc__t0_mul_t1__sub_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
  }
  T0_MUL_T1 ();
}

static void cc_t0_sub_env_mul_shade ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    if (rdp.env_color & 0xFFFFFF00)
    {
      MOD_0 (TMOD_TEX_SUB_COL);
      MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    }
  }
  USE_T0 ();
}

static void cc__t0_mul_prima_add_t0__sub_center_mul_scale ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_B, 0);
    uint32_t prima = rdp.prim_color&0xFF;
    cmb.tex_ccolor = (prima<<24)|(prima<<16)|(prima<<8)|prima;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC(rdp.CENTER);
    SETSHADE(rdp.SCALE);
  }
  else
  {
    cc_t0_mul_prima();
  }
}

static void cc__t1_inter_t0_using_primlod__sub_shade_mul_prim ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_PRIM ();
  }
  T1_INTER_T0_USING_FACTOR (lod_frac);
}

static void cc__t0_inter_t1_using_enva__sub_shade_mul_prim ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_PRIM ();
  }
  uint8_t factor = (uint8_t)(rdp.env_color&0xFF);
  T0_INTER_T1_USING_FACTOR (factor);
}

static void cc_one_sub__t0_mul_shadea__mul_shade () //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;

    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    USE_T0 ();
    cmb.tmu0_invert = TRUE;
  }
}

// ** (A-B)*C+D **
static void cc_t0_sub_t1_mul_prim_mul_shade_add_t1 ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    MULSHADE_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CC_PRIM ();
    T0_ADD_T1 ();
  }
}

static void cc_t0_sub_prim_mul_t1_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_B, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    T0_MUL_T1 ();
  }
}

static void cc_t0_sub_prim_mul_primlod_add_prim ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    SETSHADE_PRIM ();
    CC_PRIMLOD ();
  }
  else
  {
    // * not guaranteed to work if another iterated alpha is set
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    SETSHADE_PRIM ();
    SETSHADE_1MPRIMLOD ();
    CC_PRIMLOD ();
  }
  USE_T0 ();
}

static void cc_t0_sub_prim_mul_env_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    CC_ENV ();
  }
  else
  {
    cc_t0_mul_env_add_shade ();
  }
}

static void cc__t0_inter_t1_using_shadea__sub_prim_mul_env_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    //have to pass shade alpha to combiner
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
  }
  CC_ENV ();
  SUBSHADE_PRIMMULENV ();
  T0_INTER_T1_USING_SHADEA ();
}

static void cc_t0_sub_prim_mul_enva_add_prim ()  //Aded by Gonetz41
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    SETSHADE_PRIM ();
    CC_ENVA ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CC_PRIM ();
    MOD_0 (TMOD_TEX_SUB_COL_MUL_FAC);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.env_color & 0xFF);
  }
  USE_T0 ();
}

static void cc_t0_sub_prim_mul_shade_add_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    CC_ENV ();
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc_t1_sub_prim_mul_shade_add_env ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 2;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    CC_ENV ();
    MOD_1 (TMOD_TEX_SUB_COL);
    MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T1 ();
  }
}

static void cc_t1_sub_k4_mul_prima_add_t0 ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    CC_BYTE (rdp.K4);
    cmb.tex_ccolor = cmb.ccolor;
    percent = (float)(rdp.prim_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    CCMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
    T0_ADD_T1 ();
  }
}

static void cc__t0_sub_prim_mul_shade_add_env__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    CC_ENV ();
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__t0_sub_prim_mul_shade_add_env__mul_shadea ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    MULSHADE_SHADEA();
    CC_ENV ();
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc_t0_sub_k4_mul_k5_add_t0 ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    uint32_t temp = rdp.prim_lodfrac;
    rdp.prim_lodfrac = rdp.K4;
    SETSHADE_PRIMLOD ();
    rdp.prim_lodfrac = temp;
    CC_K5 ();
    USE_T0 ();
  }
  else
  {
    cc_t0 ();
  }
}

static void cc__t0_inter_t1_using_t0__sub_shade_mul_prima_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    CC_PRIMA();
    T0_INTER_T1_USING_T0 ();
  }
  else
  {
    // * not guaranteed to work if another iterated alpha is set
    CCMB (GR_COMBINE_FUNCTION_BLEND,
      GR_COMBINE_FACTOR_LOCAL_ALPHA,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    SETSHADE_A_PRIM ();
    T1_INTER_T0_USING_T0 ();  //strange, but this one looks better
  }
}

static void cc_t0_sub_env_mul_prima_add_env ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    percent = (rdp.prim_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    MOD_0 (TMOD_COL_INTER_TEX_USING_COL1);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    uint32_t prima = rdp.prim_color & 0xFF;
    MOD_0_COL1 ((prima<<24)|(prima|16)|(prima<<8));
    USE_T0 ();
  }
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
}

static void cc_t0_sub_env_mul_shade_add_prim ()  //Aded by Gonetz
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_PRIM ();
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.env_color;
  }
  else
  {
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__t0_sub_t1_mul_enva_add_shade__sub_env_mul_prim ()
// (t0-t1)*env_a+shade, (cmb-env)*prim+0
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ITRGB, 0);
    cmb.tex |= 3;
    CC_COLMULBYTE(rdp.prim_color, (rdp.env_color&0xFF));
    cmb.tex_ccolor = cmb.ccolor;
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_TEXTURE_RGB, 0);
    MULSHADE_PRIM ();
    CC_PRIMMULENV ();
  }
  else
  {
    cc_t0_sub_env_mul_prim_add_shade();
  }
}

static void cc__t0_inter_t1_using_primlod__mul_shade_add_prim ();

static void cc__t0_inter_t1_using_primlod__sub_env_mul_shade_add_prim ()  //Aded by Gonetz
{
  if (!(rdp.env_color&0xFFFFFF00))
  {
    cc__t0_inter_t1_using_primlod__mul_shade_add_prim ();
    return;
  }
  if (!(rdp.prim_color&0xFFFFFF00))
  {
    if (!cmb.combine_ext)
    {
      cc_t0_sub_env_mul_shade ();
      return;
    }
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
    T0_INTER_T1_USING_FACTOR (lod_frac);
    return;
  }
  cc__t0_inter_t1_using_primlod__mul_shade_add_prim ();
}

static void cc__t0_sub_env_mul_shade_add_prim__mul_shade ()  //Aded by Gonetz
{
  if (!cmb.combine_ext)
  {
    cc_t0_sub_env_mul_shade_add_prim ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
  cmb.tex_ccolor = rdp.env_color;
  cmb.tex |= 1;
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
  CC_PRIM ();
}

static void cc__t0_sub_env_mul_shade_add_prim__mul_shadea ()  //Aded by Gonetz
{
  if (!cmb.combine_ext)
  {
    cc_t0_sub_env_mul_shade_add_prim ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
  MOD_0 (TMOD_TEX_SUB_COL);
  MOD_0_COL (rdp.env_color & 0xFFFFFF00);

  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITALPHA, 0,
    GR_CMBX_ZERO, 0);
}

static void cc__t0_inter_t1_using_primlod__sub_env_mul_shade_add_env ()
{
  // (t1-t0)*primlod+t0, (cmb-env)*shade+env
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
  }
  CC_ENV ();
  T0_INTER_T1_USING_FACTOR (lod_frac);
}

static void cc_one_sub_prim_mul_t0a_add_prim ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
  } else {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_COL_INTER_COL1_USING_TEXA);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_COL1 (0xFFFFFF00);
  }
  USE_T0 ();
}

static void cc_t0_sub_prim_mul_shade_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ITRGB, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    if (rdp.prim_color & 0xFFFFFF00)
    {
      MOD_0 (TMOD_TEX_SUB_COL);
      MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    }
  }
  USE_T0 ();
}

static void cc__t0_mul_t0__sub_prim_mul_shade_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ITRGB, 0);
    CC_PRIM ();
  }
  else
    cc_t0_sub_prim_mul_shade_add_shade();
}

static void cc__t0_mul_t1__sub_prim_mul_shade_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ITRGB, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
  }
  T0_MUL_T1 ();
}

static void cc__t0_mul_t1__sub_env_mul_shade_add_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ITRGB, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
  }
  T0_MUL_T1 ();
}

static void cc_t0_inter_env_using_enva ()
{
  //(env-t0)*env_a+t0
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
  }
  else
  {
    USE_T0 ();
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (rdp.env_color & 0xFFFFFFFF);
    MOD_0_FAC (rdp.env_color & 0xFF);
  }
}

static void cc_one_sub_shade_mul__t0_mul_shadea__add_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE_MINUS_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
  }
  else
  {
    USE_T0 ();
  }
}

static void cc__t0_mul_t1_add_env__mul_shadea_add_shade ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 3;
  }
  else
  {
    T0_MUL_T1 ();
  }
  // * not guaranteed to work if another iterated alpha is set
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_LOCAL_ALPHA,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
}

static void cc_prim_sub_t0_mul_t1_add_t0 ()  //Aded by Gonetz
{
  if (!cmb.combine_ext)
  {
    cc_t0_mul_t1 ();
    return;
  }
  T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 0,
    GR_CMBX_B, 0);
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_OTHER_TEXTURE_RGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 3;
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 0,
    GR_CMBX_B, 0);
}

static void cc_env_sub_t0_mul_shade_add_t0 ()  //Aded by Gonetz
{
  if (!cmb.combine_ext)
  {
    cc_t0_mul_shade ();
    return;
  }
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  CC_ENV ();
  USE_T0 ();
}

static void cc__prim_sub_env_mul_t0_add_env__add_primlod ()
{
  if (!cmb.combine_ext)
  {
    cc_prim_sub_env_mul_t0_add_env ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.env_color;
  cmb.tex |= 1;
  SETSHADE_PRIMSUBENV ();
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 1,
    GR_CMBX_TEXTURE_RGB, 0);
  CC_PRIMLOD ();
}

static void cc__prim_sub_env_mul_t0_add_env__add_shadea ()
{
  if (!cmb.combine_ext)
  {
    cc_prim_sub_env_mul_t0_add_env ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.env_color;
  cmb.tex |= 1;
  SETSHADE_PRIMSUBENV ();
  CCMBEXT(GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 1,
    GR_CMBX_TEXTURE_RGB, 0);
}

static void cc_prim_sub_env_mul__t0_mul_prim__add_env ()
{
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
         GR_COMBINE_FACTOR_TEXTURE_RGB,
         GR_COMBINE_LOCAL_ITERATED,
         GR_COMBINE_OTHER_CONSTANT);
   CC_PRIM ();
   SETSHADE_ENV ();
   T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
         GR_CMBX_TMU_CCOLOR, 0,
         GR_CMBX_ZERO, 0);
   cmb.tex_ccolor = rdp.prim_color;
   cmb.tex |= 1;
}

static void cc_prim_sub_env_mul_t0_mul_shade_add_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIMSUBENV ();
    cmb.tex_ccolor = cmb.ccolor;
    cmb.tex |= 1;
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    CC_ENV ();
  }
  else
  {
    cc_t0_mul_prim_mul_shade ();
  }
}

static void cc_prim_sub_env_mul__one_sub_t0_mul_primlod_add_prim__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    cmb.dc0_detailmax = cmb.dc1_detailmax = (float)lod_frac / 255.0f;
  }
  else
  {
    USE_T0 ();
  }
}

static void cc_prim_sub_env_mul__t1_sub_prim_mul_enva_add_t0__add_env ()
{
  //(t1-prim)*env_a+t0, (prim-env)*cmb+env
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  if (cmb.combine_ext)
  {
    if (rdp.tiles[rdp.cur_tile].format > 2)
    {
      T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_ZERO, 0,
        GR_CMBX_B, 0);
      T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    }
    else
    {
      T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_ZERO, 0);
      T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_ZERO, 1,
        GR_CMBX_ZERO, 0);
    }
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 3;
    cmb.dc0_detailmax = cmb.dc1_detailmax = (float)(rdp.env_color&0xFF) / 255.0f;
  }
  else
  {
    MOD_1 (TMOD_TEX_SUB_COL_MUL_FAC);
    MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_1_FAC (rdp.env_color & 0xFF);
    T0_ADD_T1 ();
  }
}

static void cc_prim_sub_env_mul__t1_sub_prim_mul_prima_add_t0__add_env ()
{
  // (t1-prim)*prim_a+t0, (prim-env)*cmb+env
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 3;
    cmb.dc0_detailmax = cmb.dc1_detailmax = (float)(rdp.prim_color&0xFF) / 255.0f;
  }
  else
  {
    MOD_1 (TMOD_TEX_SUB_COL_MUL_FAC);
    MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_1_FAC (rdp.prim_color & 0xFF);
    T0_ADD_T1 ();
  }
}

static void cc_prim_sub_env_mul_t0a_add_t0 ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIMSUBENV ();
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = cmb.ccolor;
    cmb.tex |= 1;
  }
  else
  {
    MOD_0 (TMOD_COL_MUL_TEXA_ADD_TEX);
    MOD_0_COL (cmb.ccolor & 0xFFFFFF00);
    USE_T0 ();
  }
}

//Added by Gonetz
static void cc_prim_sub_env_mul__prim_inter_t0_using_shadea__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  if (cmb.combine_ext)
  {
    SETSHADE_PRIM ();
    PRIM_INTER_T0_USING_SHADEA ();
  }
  else
  {
    SETSHADE_PRIMSUBENV ();
    MULSHADE_SHADEA ();
    USE_T0 ();
  }
}

//Added by Gonetz
static void cc_prim_sub_env_mul__t0_sub_prim_mul_primlod_add_t0__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_CONSTANT);
  CC_PRIM ();
  SETSHADE_ENV ();
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    percent = (float)(lod_frac) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    USE_T0 ();
    MOD_0 (TMOD_TEX_SUB_COL_MUL_FAC_ADD_TEX);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_FAC (lod_frac & 0xFF);
  }
}

static void cc_prim_sub_env_mul__t0_sub_prim_mul_primlod_add_shade__add_env ()
{
  if (!cmb.combine_ext)
  {
    cc_prim_sub_env_mul_t0_add_env ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_DETAIL_FACTOR, 0,
    GR_CMBX_ITRGB, 0);
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_TEXTURE_RGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  CC_PRIM ();
  SETSHADE_ENV ();
  cmb.tex |= 1;
  percent = (float)(lod_frac) / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

static void cc_prim_sub_env_mul__t0_sub_shade_mul_primlod_add_shade__add_env ()
{
  if (!cmb.combine_ext)
  {
    cc_prim_sub_env_mul_t0_add_env ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_DETAIL_FACTOR, 0,
    GR_CMBX_B, 0);
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_TEXTURE_RGB, 0,
    GR_CMBX_B, 0);
  CC_PRIM ();
  SETSHADE_ENV ();
  cmb.tex |= 1;
  percent = (float)(lod_frac) / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

//Added by Gonetz
static void cc_lavatex_sub_prim_mul_shade_add_lavatex ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
    T0_SUB_PRIM_MUL_PRIMLOD_ADD_T1 ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
    T0_ADD_T1 ();
  }
}

//Added by Gonetz
static void cc__env_inter_prim_using_t0__sub_shade_mul_t0a_add_shade ()
{
  if (!cmb.combine_ext)
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_ALPHA,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_COL_INTER_COL1_USING_TEX);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    MOD_0_COL1 (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
    A_USE_T0 ();
  }
  else
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
    uint32_t pse = (rdp.prim_color>>24) - (rdp.env_color>>24);
    percent = (float)(pse) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
}

//Added by Gonetz
static void cc_prim_sub_env_mul_prima_add_t0 ()
{
  if (rdp.prim_color != 0x000000ff)
  {
    if (cmb.combine_ext)
    {
      CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
        GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_CONSTANT_ALPHA, 0,
        GR_CMBX_TEXTURE_RGB, 0);
      CC_PRIM ();
      SETSHADE_ENV ();
    }
    else
    {
      CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
        GR_COMBINE_FACTOR_ONE,
        GR_COMBINE_LOCAL_ITERATED,
        GR_COMBINE_OTHER_TEXTURE);
      SETSHADE_PRIMSUBENV ();
      SETSHADE_PRIMA ();
    }
  }
  else if ((rdp.prim_color&0xFFFFFF00) - (rdp.env_color&0xFFFFFF00) == 0)
  {
    cc_t0 ();
    return;
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CC_ENV ();
  }
  USE_T0 ();
}

static void cc_prim_sub_env_mul__t0_mul_enva_add_t1__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    percent = (float)(rdp.env_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    T0_ADD_T1 ();
  }
}

static void cc_prim_sub_env_mul__t1_sub_prim_mul_t0_add_t0__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 3;
  }
  else
  {
    MOD_1 (TMOD_TEX_SUB_COL);
    MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    T0_MUL_T1_ADD_T0 ();
  }
}

//Added by Gonetz
static void cc__prim_sub_env_mul_prim_add_t0__mul_prim ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    SETSHADE_PRIMSUBENV ();
    SETSHADE_PRIM ();
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM() ;
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    SETSHADE_PRIMSUBENV ();
    SETSHADE_PRIM ();
    USE_T0 ();
  }
}

//Added by Gonetz
static void cc_prim_sub_env_mul_prim_add_env ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_B, 0);
    SETSHADE_ENV();
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    SETSHADE_PRIMSUBENV ();
    SETSHADE_PRIM ();
    CC_ENV ();
  }
}

static void cc_prim_sub_env_mul_primlod_add_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CC_PRIMLOD ();
    cmb.tex_ccolor = cmb.ccolor;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    SETSHADE_PRIM();
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    SETSHADE_PRIMSUBENV ();
    SETSHADE_PRIMLOD ();
    CC_ENV ();
  }
}

static void cc_prim_sub_env_mul_enva_add_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CC_ENVA ();
    cmb.tex_ccolor = cmb.ccolor;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    SETSHADE_PRIM();
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    SETSHADE_PRIMSUBENV ();
    SETSHADE_ENVA ();
    CC_ENV ();
  }
}

//Added by Gonetz
static void cc_prim_sub_shade_mul__t0_inter_t1_using_shadea__add_shade ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
  }
  CC_PRIM ();
  T0_INTER_T1_USING_SHADEA ();
}

static void cc__t0_sub_env_mul_shade__sub_prim_mul_shade_add_prim ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    CC_PRIM ();
  }
  else
  {
    cc_t0_mul_shade ();
  }
}

static void cc__env_sub_shade_mul_t0_add_shade__mul_prim ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = rdp.prim_color;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM() ;
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CC_ENV ();
    MULSHADE_PRIM ();
    USE_T0 ();
  }
}

//Added by Gonetz
static void cc_env_sub_shade_mul__t0_inter_t1_using_shadea__add_shade ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
  }
  CC_ENV ();
  T0_INTER_T1_USING_SHADEA ();
}

static void cc__t0_mul_shade_mul_shadea__add__t1_mul_one_sub_shadea ()
{
  // (t0-0)*shade+0, (cmb-t0)*shadea+t0
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, 1,
      GR_CMBX_ZERO, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    MULSHADE_SHADEA ();
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_TEXTURE_RGB, 0);
  }
  else
  {
    cc_t0_mul_shade ();
  }
}

//Added by Gonetz
static void cc_shade_sub_env_mul__t0_mul_t1__add__t0_mul_t1 ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_TEXTURE_RGB, 0);
    CC_ENV ();
    T0_MUL_T1 ();
  }
  else
  {
    cc_t0_mul_t1 ();
  }
}

//Added by Gonetz
static void cc_shade_sub_env_mul__t0_mul_t1__add__t0_mul_t1 ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_RGB, 0,
      GR_CMBX_TEXTURE_RGB, 0);
    CC_ENV ();
    T0_MUL_T1 ();
  }
  else
  {
    cc_t0_mul_t1 ();
  }
}

static void cc__t0_add_prim_mul_shade__mul_shade_add_env ()
{
  if (!cmb.combine_ext)
  {
    cc_shade_sub_env_mul_prim_add_t0 ();
    return;
  }
  T1CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  CC_ENV ();
  cmb.tex |= 1;
}

static void cc__t0_add_prim_mul_shade__mul_shade ()
{
  if (!cmb.combine_ext)
  {
    cc_shade_sub_env_mul_prim_add_t0 ();
    return;
  }
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
}

static void cc__t0_inter_t1_using_prim__inter_env_using_enva ()
{
  // (t1-t0)*prim+t0, (env-cmb)*env_a+cmb
  if (!cmb.combine_ext)
  {
    cc_t0_inter_t1_using_prima ();
    return;
  }
  T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 0,
    GR_CMBX_B, 0);
  T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_TMU_CCOLOR, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 3;
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_CONSTANT_ALPHA, 0,
    GR_CMBX_B, 0);
  cmb.ccolor = rdp.env_color;
}

static void cc__t0_inter_t1_using_shade__inter_env_using_enva ()
{
  // (t1-t0)*shade+t0, (env-cmb)*env_a+cmb
  if (!cmb.combine_ext)
  {
    cc_t0_inter_t1_using_enva ();
    return;
  }
  T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 0,
    GR_CMBX_B, 0);
  T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  cmb.tex |= 3;
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_CONSTANT_ALPHA, 0,
    GR_CMBX_B, 0);
  cmb.ccolor = rdp.env_color;
}

//Added by Gonetz
static void cc_t0_inter_t1_using_shade ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
    T0_INTER_T1_USING_FACTOR (0x7F);
  }
}

//Added by Gonetz
static void cc_t1_inter_t0_using_shade ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
    T0_INTER_T1_USING_FACTOR (0x7F);
  }
}

//Added by Gonetz
static void cc_t1_inter_t0_using_shadea ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
  }
  T1_INTER_T0_USING_SHADEA ();
}

static void cc_t0_inter_shade_using_t0a ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    USE_T0();
    A_USE_T0();
  }
  else
  {
    //(shade-t0)*t0a+t0 = t0*(1-t0a)+shade*t0a
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    rdp.best_tex = 1;
    cmb.tex = 1;
    cmb.tmu0_func = GR_COMBINE_FUNCTION_BLEND_LOCAL;
    cmb.tmu0_fac = GR_COMBINE_FACTOR_LOCAL_ALPHA;
  }
}

//Added by Gonetz
static void cc__env_inter_t0_using_shadea__mul_shade ()
{
  //((t0-env)*shadea+env)*shade
  if (!cmb.combine_ext)
  {
    cc_t0_mul_shade ();
    return;
  }
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITALPHA, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.env_color;
  cmb.tex |= 1;
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_ZERO, 0);
}

static void cc_prim_inter__t0_mul_t1_add_env__using_shadea ()
{
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 3;
  }
  else
  {
    T0_MUL_T1 ();
  }
  // * not guaranteed to work if another iterated alpha is set
  CCMB (GR_COMBINE_FUNCTION_BLEND,
    GR_COMBINE_FACTOR_LOCAL_ALPHA,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  SETSHADE_PRIM ();
}

static void cc_env_inter__prim_inter_shade_using_t0__using_shadea ()
{
  if (!cmb.combine_ext)
  {
    cc_shade_sub_prim_mul_t0_add_prim ();
    return;
  }
  T0CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITALPHA, 0,
    GR_CMBX_B, 0);
  CC_ENV ();
}

static void cc_shade_inter__prim_inter_shade_using_t0__using_shadea ()
{
  if (!cmb.combine_ext)
  {
    cc_shade_sub_prim_mul_t0_add_prim ();
    return;
  }
  T0CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
    GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_LOCAL_TEXTURE_RGB, 0,
    GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
  CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
    GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITALPHA, 0,
    GR_CMBX_B, 0);
}

// ** ((A-B)*C+D)*E **
static void cc_t0_sub_env_mul_prim_mul_shade_add_prim_mul_shade () //Aded by Gonetz
{
  //(t0-env)*shade+shade, (cmb-0)*prim+0
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.env_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    if (rdp.env_color & 0xFFFFFF00)
    {
      MOD_0 (TMOD_TEX_SUB_COL);
      MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    }
    MULSHADE_PRIM ();
    USE_T0 ();
  }
}

static void cc__t1_sub_prim_mul_t0_add_env__mul_shade () //Aded by Gonetz
{
  // (t1-prim)*t0+env, (cmb-0)*shade+0
  if (cmb.combine_ext)
  {
    T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 3;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    if (rdp.prim_color & 0xFFFFFF00)
    {
      MOD_1 (TMOD_TEX_SUB_COL);
      MOD_1_COL (rdp.prim_color & 0xFFFFFF00);
    }
    T0_MUL_T1 ();
  }
}

static void cc__one_sub_shade_mul_t0_add_shade__mul_prim ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
	  GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
	  GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
	CC_PRIM ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE_MINUS_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    USE_T0 ();
  }
}

static void cc__one_sub_shade_mul_t0_add_shade__mul_env ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
	  GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
	  GR_CMBX_CONSTANT_COLOR, 0,
      GR_CMBX_ZERO, 0);
	CC_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE_MINUS_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    USE_T0 ();
  }
}

static void cc__t0_inter_t1_using_shadea__mul_shade ()
{
  if (cmb.combine_ext)
  {
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
  }
  T0_INTER_T1_USING_SHADEA ();
}

static void cc_t0_inter_prim_using_prima ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CC_1SUBPRIMA ();
    SETSHADE_PRIM ();
    SETSHADE_PRIMA ();
    USE_T0 ();
  }
}

static void cc__env_inter_one_using_t0__mul_shade ()
{
  //(one-env)*t0+env, (cmb-0)*shade+0
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color&0xFFFFFF00;
    cmb.tex |= 1;
  }
  else
  {
    MOD_0 (TMOD_COL_INTER_COL1_USING_TEX);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    MOD_0_COL1 (0xFFFFFF00);
    USE_T0 ();
  }
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
}

static void cc_env_inter_one_using__one_sub_t0_mul_primlod ()
{
  if (cmb.combine_ext)
  {
    // (noise-t0)*primlod+0, (1-env)*cmb+env
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rand()&0xFFFFFF00;
    cmb.tex |= 1;
    percent = (float)(lod_frac) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    cmb.tex |= 1;
  }
  else
  {
    USE_T0 ();
  }
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE_MINUS_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_ENV ();
}

static void cc__prim_inter_t0_using_env__mul_shade ()
{
  // (t0-prim)*env+prim, (cmb-0)*shade+0
  if ((rdp.prim_color & 0xFFFFFF00) == 0)
  {
    cc_t0_mul_env_mul_shade ();
  }
  else if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CCOLOR, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.env_color & 0xFFFFFF00;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    uint32_t onesubenv = ~rdp.env_color;
    CC_C1MULC2(rdp.prim_color, onesubenv);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_COL_INTER_TEX_USING_COL1);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    MOD_0_COL1 (rdp.env_color & 0xFFFFFF00);
    USE_T0 ();
  }
}

static void cc__one_inter_prim_using_t1__mul_shade ()
{
  if (cmb.combine_ext)
  {
    if ((settings.hacks&hack_BAR) && rdp.cur_tile == 1)
    {
      T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_LOCAL_TEXTURE_RGB, 0,
        GR_CMBX_ZERO, 1);
      cmb.tex |= 1;
    }
    else
    {
      T1CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_LOCAL_TEXTURE_RGB, 0,
        GR_CMBX_ZERO, 1);
      T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_ZERO, 0,
        GR_CMBX_B, 0);
      cmb.tex |= 2;
    }
    cmb.tex_ccolor = rdp.prim_color | 0xFF;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    if ((settings.hacks&hack_BAR) && rdp.cur_tile == 1)
    {
      MOD_0 (TMOD_COL_INTER_COL1_USING_TEX);
      MOD_0_COL (0xFFFFFF00);
      MOD_0_COL1 (rdp.prim_color & 0xFFFFFF00);
      USE_T0 ();
    }
    else
    {
      MOD_1 (TMOD_COL_INTER_COL1_USING_TEX);
      MOD_1_COL (0xFFFFFF00);
      MOD_1_COL1 (rdp.prim_color & 0xFFFFFF00);
      USE_T1 ();
    }
  }
}

static void cc_prim_sub__prim_sub_t0_mul_prima__mul_shade ()
{
  // (prim-t0)*prim_a+0, (prim-cmb)*shade+0
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = rdp.prim_color;
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    CC_PRIM();
  }
  else
  {
    if ((rdp.prim_color & 0xFFFFFF00) == 0)
    {
      cc_t0_mul_prima_mul_shade ();
      return;
    }
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MOD_0 (TMOD_COL_INTER_TEX_USING_COL1);
    MOD_0_COL (rdp.prim_color & 0xFFFFFF00);
    uint8_t prima = (uint8_t)(rdp.prim_color&0xFF);
    MOD_0_COL1 ((prima<<24)|(prima<<16)|(prima<<8));
    USE_T0 ();
  }
}

static void cc__env_inter_prim_using__t0_sub_shade_mul_primlod_add_env ()
{
  // (t0-shade)*lodf+env, (prim-env)*cmb+env
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = rdp.env_color;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    cmb.tex |= 1;
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CC_PRIM ();
    SETSHADE_ENV ();
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,//TEXTURE_RGB,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);//CONSTANT);
    MOD_0 (TMOD_COL_INTER_COL1_USING_TEX);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    MOD_0_COL1 (rdp.prim_color & 0xFFFFFF00);
    USE_T0 ();
    MULSHADE_PRIMSUBENV ();
    MULSHADE_PRIMLOD();
    SUBSHADE_PRIMSUBENV ();
  }
}

static void cc__t0_mul_shade__inter_env_using_enva ()
{
  // (t0-0)*shade+0, (env-cmb)*env_a+cmb    ** INC **
  uint32_t enva  = rdp.env_color&0xFF;
  if (enva == 0xFF)
    cc_env ();
  else if (enva == 0)
    cc_t0_mul_shade ();
  else if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    MULSHADE_1MENVA ();
    CC_COLMULBYTE(rdp.env_color, (rdp.env_color&0xFF));
    cmb.tex_ccolor = cmb.ccolor;
  }
  else
  {
    CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    INTERSHADE_2 (rdp.env_color & 0xFFFFFF00, rdp.env_color & 0xFF);
    USE_T0 ();
    MOD_0 (TMOD_TEX_INTER_COLOR_USING_FACTOR);
    MOD_0_COL (rdp.env_color & 0xFFFFFF00);
    MOD_0_FAC (rdp.env_color & 0xFF);
  }
}

static void cc__t0_mul_shade__inter_one_using_shadea ()
{
  if (cmb.combine_ext)
  {
    T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
      GR_CMBX_ITRGB, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    CCMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_B, 0);
  }
  else
  {
    cc_t0_mul_shade ();
  }
}

static void ac_t0_mul_t1_add_t1 ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
  }
  else
  {
    A_T0_MUL_T1 ();
  }
}

//Added by Gonetz
static void ac__t1_sub_one_mul_primlod_add_t0__mul_prim ()
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    percent = (float)lod_frac / 255.0f;
  }
  else
  {
    cmb.tmu1_a_func = GR_COMBINE_FUNCTION_BLEND_LOCAL;
    cmb.tmu1_a_fac = GR_COMBINE_FACTOR_DETAIL_FACTOR;
    percent = (255 - lod_frac) / 255.0f;
    cmb.tmu0_a_func = GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA;
    cmb.tmu0_a_fac = GR_COMBINE_FACTOR_OTHER_ALPHA;
  }
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  cmb.tex |= 3;
}

static void ac__t0_sub_t1_mul_enva_add_t0__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
    cmb.tex |= 3;
  }
  else
  {
    A_T0_MUL_T1 ();
  }
}

static void ac__t0_sub_one_mul_enva_add_t0__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    SETSHADE_A(0xFF);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
    cmb.tex |= 1;
  }
  else
  {
    A_USE_T0 ();
  }
}

static void ac__t0_sub_t1_mul_primlod_add_t0__mul_prim ()
{
   ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_LOCAL,
         GR_COMBINE_LOCAL_CONSTANT,
         GR_COMBINE_OTHER_TEXTURE);
   CA_PRIM ();
   T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_ZERO, 0,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
   T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_DETAIL_FACTOR, 0,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
   cmb.tex |= 3;
   percent = (float)lod_frac / 255.0f;
   cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

static void ac__t1_sub_prim_mul_primlod_add_t0__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    A_T0_INTER_T1_USING_FACTOR (lod_frac);
  }
}

static void ac__t1_sub_t0_mul_enva_add_t1__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF);
  }
  else
  {
    uint8_t factor = (uint8_t)(rdp.env_color&0xFF);
    A_T0_INTER_T1_USING_FACTOR (factor);
  }
}

static void ac__t1_sub_t0_mul_primlod__mul_env_add_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (uint32_t)((float)(rdp.env_color&0xFF)*(float)rdp.prim_lodfrac/255.0f);
  }
  else
  {
    cmb.tmu1_a_func = GR_COMBINE_FUNCTION_LOCAL;
    cmb.tmu0_a_func = GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL;
    cmb.tmu0_a_fac = GR_COMBINE_FACTOR_DETAIL_FACTOR;
    percent = (rdp.prim_lodfrac * (rdp.env_color&0xFF)) / 65025.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent; \
  }
  cmb.tex |= 3;
}

static void ac__t0_sub_one_mul_enva_add_t1__mul_prim ()
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
    cmb.tex |= 3;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_ENV ();
    SETSHADE_A_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    SETSHADE_A_PRIM ();
    SETSHADE_A_ENV ();
    A_T0_MUL_T1 ();
  }
}

static void ac__t1_sub_one_mul_primlod_add_t0__mul_env ()
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    percent = (float)lod_frac / 255.0f;
  }
  else
  {
    cmb.tmu1_a_func = GR_COMBINE_FUNCTION_BLEND_LOCAL;
    cmb.tmu1_a_fac = GR_COMBINE_FACTOR_DETAIL_FACTOR;
    percent = (255 - lod_frac) / 255.0f;
    cmb.tmu0_a_func = GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA;
    cmb.tmu0_a_fac = GR_COMBINE_FACTOR_OTHER_ALPHA;
  }
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_ENV ();
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  cmb.tex |= 3;
}

static void ac__t0_mul_primlod_add_t0__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex |= 1;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    A_USE_T0 ();
  }
}

//Added by Gonetz
static void ac__t0_sub_t1__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
  }
  else
  {
    A_T0_SUB_T1 ();
  }
}

static void ac__t1_mul_t1_add_t1__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 2;
  }
  else
  {
    A_USE_T1 ();
  }
}

static void ac__t1_sub_one_mul_primlod_add_t0__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    percent = (float)lod_frac / 255.0f;
  }
  else
  {
    cmb.tmu1_a_func = GR_COMBINE_FUNCTION_BLEND_LOCAL;
    cmb.tmu1_a_fac = GR_COMBINE_FACTOR_DETAIL_FACTOR;
    percent = (255 - lod_frac) / 255.0f;
    cmb.tmu0_a_func = GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA;
    cmb.tmu0_a_fac = GR_COMBINE_FACTOR_OTHER_ALPHA;
  }
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  cmb.tex |= 3;
}

static void ac__t1_sub_shade_mul_primlod_add_t0__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex |= 3;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    A_T0_INTER_T1_USING_FACTOR (lod_frac);
  }
}

static void ac__prim_sub_one_mul_primlod_add_t0__mul_env ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    SETSHADE_A_PRIM ();
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    cmb.tex |= 1;
  }
  else
  {
    A_USE_T0 ();
  }
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_ENV ();
}

// ** A*B+C **
static void ac_t0_mul_prim_add_t0 ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_B, 0);
    CA_PRIM ();
    A_USE_T0 ();
  }
  else
    ac_t0();
}

static void ac__t0_inter_t1_using_t1a__mul_prim_add__t0_inter_t1_using_t1a ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_B, 0);
    CA_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CA_PRIM ();
  }
  A_T0_INTER_T1_USING_T1A ();
}

static void ac__t1_inter_t0_using_t0a__mul_prim_add__t1_inter_t0_using_t0a ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_B, 0);
    CA_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CA_PRIM ();
  }
  A_T1_INTER_T0_USING_T0A ();
}

static void ac_t0_mul_primlod_add_t0 ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex |= 1;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
  }
  else
  {
    A_USE_T0 ();
  }
}

// ** (A*B+C)*D **
static void ac__t0_mul_primlod_add_shade__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    A_USE_T0 ();
  }
}

static void ac__t1_mul_primlod_add_shade__mul_shade ()
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 2;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    A_USE_T1 ();
  }
}

// ** ((A-B)*C+D)+E **
static void ac__t0_sub_t1_mul_prim_add_shade__mul_shade ()
 //(t0-t1)*prim+shade, (cmb-0)*shade+0
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA,
      GR_COMBINE_FACTOR_TEXTURE_ALPHA,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CA_PRIM ();
    A_T1_SUB_T0 ();
  }
}

static void ac__t1_sub_t0_mul_prim_add_shade__mul_shade ()
 //(t1-t0)*prim+shade, (cmb-0)*shade+0
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 3;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA,
      GR_COMBINE_FACTOR_TEXTURE_ALPHA,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_CONSTANT);
    CA_PRIM ();
    A_T1_SUB_T0 ();
  }
}

static void ac__t0_add_prim_mul_shade__mul_shade ()
{
  // (shade-0)*prim+t0, (cmb-0)*shade+0
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_A_PRIM ();
    A_USE_T0();
  }
}

// ** (A-B)*C **
static void ac_t0_sub_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  } else {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_A_PRIM ();
    A_USE_T0 ();
  }
}

static void ac_t0_sub_prim_mul_shade_mul_env ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_ENV ();
  } else {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_A_PRIM ();
    MULSHADE_A_ENV ();
    A_USE_T0 ();
  }
}

static void ac_t0_sub_shade_mul_prim ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex |= 1;
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF);
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  } else {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_A_PRIM ();
    A_USE_T0 ();
  }
}

static void ac__t0_mul_t1__sub_prim_mul_shade ()  //Aded by Gonetz
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ZERO, 0);
   CA_PRIM();
   A_T0_MUL_T1 ();
}

static void ac__one_sub_t1_mul_t0_add_shade__sub_prim_mul_shade ()  //Aded by Gonetz
{
  // (1-t1)*t0+shade, (cmb-prim)*shade+0
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_ONE_MINUS_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex |= 3;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_PRIM();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    A_T0_MUL_T1 ();
  }
}

static void ac__t1_mul_primlod_add_t0__sub_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
  }
  A_T1_MUL_PRIMLOD_ADD_T0 ();
}

static void ac__t1_mul_primlod_add_t0__sub_env_mul_prim ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_ENV ();
    SETSHADE_A_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CA_PRIM ();
  }
  A_T1_MUL_PRIMLOD_ADD_T0 ();
}

static void ac__t1_mul_prima_add_t0__sub_env_mul_shade ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_ENV ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
  }
  A_T1_MUL_PRIMA_ADD_T0 ();
}

static void ac_prim_sub_shade_mul_prim ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_PRIM();
  }
  else
  {
    if (!(rdp.prim_color & 0xFF))
    {
      ac_zero();
    }
    else
    {
      ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
        GR_COMBINE_FACTOR_ONE,
        GR_COMBINE_LOCAL_ITERATED,
        GR_COMBINE_OTHER_CONSTANT);
      CA_PRIM();
    }
  }
}

// ** (A+B)*C*D **
static void ac_one_plus_env_mul_prim_mul_shade ()
{
   ACMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_ONE_MINUS_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ZERO, 0);
   MULSHADE_A_PRIM ();
   CA_ENV();
}

// ** (A-B)*C+A **
static void ac__t0_mul_t1__sub_env_mul_prim_add__t0_mul_t1 ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_TEXTURE_ALPHA, 0);
    CA_ENV();
    SETSHADE_A_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
  }
  A_T0_MUL_T1 ();
}

// ** (A-B)*C+D **
static void ac__t0_sub_prim_mul_shade_add_shade__mul_env ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ITALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF) ;
    cmb.tex |= 1;
    ACMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_ENV();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_A_ENV ();
    MOD_0 (TMOD_TEX_SUB_COL);
    MOD_0_COL (rdp.prim_color & 0xFF);
    A_USE_T0 ();
  }
}

static void ac_t0_sub_one_mul_enva_add_t1 ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
    cmb.tex |= 3;
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CA_ENV();
  }
  else
  {
    ac__t0_mul_t1__mul_env ();
  }
}

static void ac_t1_sub_one_mul_enva_add_t0 ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
    SETSHADE_A (0xFF);
    cmb.tex |= 3;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
    A_USE_T0 ();
  }
}

static void ac_t1_sub_one_mul_primlod_add_t0 ()  //Aded by Gonetz
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    percent = (float)lod_frac / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    cmb.tex |= 3;
  }
  else
  {
    //    A_T0_MUL_T1 ();
    //    A_T1_MUL_PRIMLOD_ADD_T0 ();
    cmb.tmu1_a_func = GR_COMBINE_FUNCTION_BLEND_LOCAL;
    cmb.tmu1_a_fac = GR_COMBINE_FACTOR_DETAIL_FACTOR;
    percent = (255 - lod_frac) / 255.0f;
    cmb.tmu0_a_func = GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA;
    cmb.tmu0_a_fac = GR_COMBINE_FACTOR_OTHER_ALPHA;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    cmb.tex |= 3;
  }
}

static void ac_t1_sub_prim_mul_shade_add_prim ()  //Aded by Gonetz
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_B, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.prim_color&0xFF) ;
    cmb.tex |= 2;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
      GR_COMBINE_FACTOR_TEXTURE_ALPHA,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    CA_PRIM ();
    MOD_1 (TMOD_TEX_SUB_COL);
    MOD_1_COL (rdp.prim_color & 0xFF);
    A_USE_T1 ();
  }
}

static void ac_t0_sub_env_mul_shadea_add_env ()  //Aded by Gonetz
{
  if (!cmb.combine_ext)
  {
    ac_t0_mul_shade ();
    return;
  }
  T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
    GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
    GR_CMBX_ZERO, 1,
    GR_CMBX_ZERO, 0);
  cmb.tex |= 1;
  ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
    GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITALPHA, 0,
    GR_CMBX_B, 0);
  CA_ENV ();
}

static void ac_one_sub_t1_add_t0_mul_env ()
{
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_B, 1);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF);
    cmb.tex |= 3;
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    CA_ENV ();
    A_T0_ADD_T1();
    cmb.tmu1_a_invert = FXTRUE;
  }
}

static void ac_shade_sub_t0_mul_primlod_add_prim ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_TMU_CALPHA, 0,
      GR_CMBX_ZERO, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (lod_frac&0xFF);
    cmb.tex |= 1;
    ACMBEXT(GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_TEXTURE_ALPHA, 0);
    CA_PRIM ();
  }
  else
    ac_t0();
}

//Added by Gonetz
static void ac_t0_inter_t1_using_shadea ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE);
  }
  A_T0_INTER_T1_USING_SHADEA ();
}

static void ac__env_sub_one_mul_t1_add_t0__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ZERO, 1,
      GR_CMBX_ZERO, 0);
    SETSHADE_A(0xFF);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
    cmb.tex |= 3;
  }
  else
  {
    uint8_t factor = (uint8_t)(rdp.env_color&0xFF);
    A_T0_INTER_T1_USING_FACTOR (factor);
  }
}

static void ac__t1_mul_enva_add_t0__sub_prim_mul_shade ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_ITALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_PRIM ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE);
    MULSHADE_A_PRIM ();
  }
  A_T1_MUL_ENVA_ADD_T0 ();
}

//Added by Gonetz
static void ac__t0_inter_t1_using_shadea__mul_prim ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    A_T0_INTER_T1_USING_SHADEA ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    A_T0_INTER_T1_USING_FACTOR (0x7F);
  }
  CA_PRIM ();
}

//Added by Gonetz
static void ac__t0_inter_t1_using_shadea__mul_env ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    A_T0_INTER_T1_USING_SHADEA ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_LOCAL,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_TEXTURE);
    A_T0_INTER_T1_USING_FACTOR (0x7F);
  }
  CA_ENV ();
}

//Added by Gonetz
static void ac__t0_inter_t1_using_primlod__mul_env_add__t0_inter_t1_using_primlod ()
{
  if (cmb.combine_ext)
  {
    ACMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
      GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_B, 0);
    CA_ENV ();
  }
  else
  {
    ACMB (GR_COMBINE_FUNCTION_BLEND,
      GR_COMBINE_FACTOR_TEXTURE_ALPHA,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_ITERATED);
    SETSHADE_A_ENV ();
    CA (0xFF);
  }
  A_T0_INTER_T1_USING_FACTOR (lod_frac);
}

static void ac__t1_sub_one_mul_enva_add_t0__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    cmb.tex |= 3;
    cmb.dc0_detailmax = cmb.dc1_detailmax = (float)(rdp.env_color&0xFF) / 255.0f;
  }
  else
  {
    // (t1-1)*env+t0, (cmb-0)*prim+0
    A_T0_MUL_T1 ();

    MOD_1 (TMOD_TEX_SCALE_FAC_ADD_FAC);
    MOD_1_FAC (rdp.env_color & 0xFF);
  }
}

static void ac__one_inter_t0_using_prim__mul_env ()
{
  if (cmb.combine_ext)
  {
    T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_B, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
    cmb.tex |= 1;
    cmb.dc0_detailmax = cmb.dc1_detailmax = (float)(rdp.prim_color&0xFF) / 255.0f;
    ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_CONSTANT_ALPHA, 0,
      GR_CMBX_ZERO, 0);
    CA_ENV ();
  }
  else
  {
	ac_t0_mul_prim_add_env ();
  }
}

static void ac__t1_sub_one_mul_enva_add_t0__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  if (cmb.combine_ext)
  {
    T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
      GR_CMBX_ZERO, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
      GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
      GR_CMBX_DETAIL_FACTOR, 0,
      GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
    cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | 0xFF ;
    percent = (rdp.env_color&0xFF) / 255.0f;
    cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
    cmb.tex |= 3;
  }
  else
  {
    uint8_t factor = (uint8_t)(rdp.env_color&0xFF);
    A_T0_INTER_T1_USING_FACTOR (factor);
  }
}
