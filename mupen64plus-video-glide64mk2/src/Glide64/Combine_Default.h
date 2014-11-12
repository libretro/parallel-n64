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
