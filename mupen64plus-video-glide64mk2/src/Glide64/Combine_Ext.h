static void cc__t0_inter_one_using_primlod__mul_prim (void)
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIM ();
  T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_B, 0);
  cmb.tex |= 1;
  percent = (float)lod_frac / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

static void cc__t1_inter_one_using_env__mul_prim (void)
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIM ();
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

static void cc__t1_inter_one_using_enva__mul_t0 (void)
{
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE);
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

static void cc__t0_mul_shade__add__t1_mul_shade ()
{
   //combiner is used in Spiderman. It seems that t0 is used instead of t1
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

static void ac__t0_inter_t1_using_primlod__sub_env_mul_shade_add_shade (void)
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ALOCAL, 0);
   CA_ENV ();
   A_T0_INTER_T1_USING_FACTOR (lod_frac);
}

static void cc__t0_mul_prim__inter_env_using_enva (void)
{
  uint32_t enva  = rdp.env_color&0xFF;
  if (enva == 0xFF)
    cc_env ();
  else if (enva == 0)
    cc_t0_mul_prim ();
  else
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
}

static void cc__t1_inter_t0_using_t1__mul_shade (void)
{
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_LOCAL,
         GR_COMBINE_LOCAL_ITERATED,
         GR_COMBINE_OTHER_TEXTURE);
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

//Added by Gonetz
static void cc__t1_inter_t0_using_shadea__mul_shade (void)
{
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
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ONE_MINUS_X,
        GR_CMBX_LOCAL_TEXTURE_RGB, 0,
        GR_CMBX_B, 0);
  cmb.tex |= 1;
  cmb.tex_ccolor = rdp.prim_color;
}

static void cc__t0_inter_one_using_primlod__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  T0CCMBEXT(GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_B, 0);
  cmb.tex |= 1;
  percent = (float)lod_frac / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

//Added by Gonetz
static void cc__t0_inter_env_using_enva__mul_shade (void)
{
  // (env-t0)*env_a+t0, (cmb-0)*shade+0
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_TMU_CALPHA, 0,
        GR_CMBX_B, 0);
  cmb.tex |= 1;
  cmb.tex_ccolor = rdp.env_color;
}

//Added by Gonetz
static void cc__t0_inter_env_using_shadea__mul_shade ()
{
  // (env-t0)*shade_a+t0, (cmb-0)*shade+0
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

static void cc__t0_mul_prim_add_env__mul_shade ()
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

static void cc__t1_sub_t0_mul_primlod_add_prim__mul_shade ()
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

static void cc__t1_sub_prim_mul_t0__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
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

static void cc__t1_sub_t0_mul_t0_add_shade__mul_shade (void) //Aded by Gonetz
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
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

static void cc__one_sub_shade_mul_t0_add_shade__mul_shade ()
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

static void cc__t0_sub_prim_mul_t1_add_t1__mul_shade (void)
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

static void cc__t1_sub_env_mul_t0_add_t0__mul_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
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

static void cc__t0_mul_prima_add_prim_mul__shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
        GR_CMBX_TMU_CALPHA, 0,
        GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
}

static void cc__t0_inter_prim_using_prima__inter_env_using_enva ()
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

static void cc_prim_inter_t1_mul_shade_using_texa (void)
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

static void cc__prim_inter_t0_using_t0a__mul_shade (void)
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
        GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
}

static void cc__prim_inter_t0_using_t0a__inter_env_using_enva ()
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

static void cc__shade_inter_t0_using_shadea__mul_shade (void)
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

static void cc__t0_mul_t1__add_env_mul__t0_mul_t1__add_env (void)
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

static void cc_t0_add_prim_mul_one_sub_t0_add_t0 (void) //Aded by Gonetz
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

static void cc__one_sub_prim_mul_t0_add_prim__mul_prima_add__one_sub_prim_mul_t0_add_prim () //Aded by Gonetz
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

// ** A-B*C **
static void cc_env_sub__t0_sub_t1_mul_primlod__mul_prim (void) //Aded by Gonetz
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

static void cc_env_sub__t0_mul_scale_add_env__mul_prim (void)
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

static void cc_one_sub__one_sub_t0_mul_enva_add_prim__mul_prim () //Aded by Gonetz
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

static void cc__t0_sub_env_mul_enva__add_prim_mul_shade (void)
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

// ** A*B+C **
//Added by Gonetz
static void cc_t0_mul_prim_add_t1 ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
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

static void cc_shirt (void)
{
  // (t1-0)*prim+0, (1-t0)*t1+cmb
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
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

static void cc__t0_add_primlod__mul_prim_add_env (void)
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

static void cc__t1_sub_prim_mul_enva_add_t0__mul_prim_add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_CONSTANT);
  CC_PRIM ();
  SETSHADE_ENV ();
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

//Added by Gonetz
static void cc__t0_mul_t1__sub_prim_mul_env_add_shade ()
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

static void cc__t0_mul_shadea_add_env__mul_shade_add_prim (void)
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

//Added by Gonetz
static void cc__t1_sub_t0_mul_primlod_add_prim__mul_shade_add_shade (void)
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

static void cc__t0_mul_enva_add_t1__mul_shade_add_prim (void)
{
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
         GR_COMBINE_FACTOR_TEXTURE_RGB,
         GR_COMBINE_LOCAL_CONSTANT,
         GR_COMBINE_OTHER_ITERATED);
   CC_PRIM ();
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

static void cc__t0_add_prim__mul_shade_add_t0 ()
{
   CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
         GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_B, 0);
   CC_PRIM ();
   USE_T0 ();
}

static void cc__t0_add_prim__mul_shade_add_t1 (void)
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

static void cc__t0_add_primlod__mul_shade_add_env ()
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

static void cc__t0_mul_prima_add_prim_mul__shade_add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
        GR_CMBX_TMU_CALPHA, 0,
        GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
}

static void cc__t1_mul_t1_add_t0__mul_prim ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CC_PRIM ();
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

// (A*B+C)*D
static void cc__t0_mul_prim_add_shade__mul_env ()
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

static void cc__prim_mul_shade_add_env__mul_shade () //Aded by Gonetz
{
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
   T1CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
         GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
         GR_CMBX_ZERO, 0,
         GR_CMBX_B, 0);
   T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ZERO, 1,
         GR_CMBX_ZERO, 0);
   cmb.tex |= 3;

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

static void cc__t0_mul_t1_add_prim__mul_shade () //Aded by Gonetz
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ZERO, 0);
   CC_PRIM ();
   T0_MUL_T1 ();
}

// ** (A-B)*C **
static void cc__t0_mul_prim_add_shade__sub_env_mul_shade ()
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

static void cc__t0_sub_env_mul_shade__sub_prim_mul_shade ()
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

static void cc_t0_sub_prim_mul_shade ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ZERO, 0);
   CC_PRIM ();
   USE_T0 ();
}

static void cc__t0_mul_t1__sub_prim_mul_shade ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ZERO, 0);
   CC_PRIM ();
   T0_MUL_T1 ();
}

static void cc_t0_sub_env_mul_shade ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ZERO, 0);
   CC_ENV ();
   USE_T0 ();
}

static void cc__t0_mul_prima_add_t0__sub_center_mul_scale ()
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

static void cc__t1_inter_t0_using_primlod__sub_shade_mul_prim ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_COLOR, 0,
         GR_CMBX_ZERO, 0);
   CC_PRIM ();
   T1_INTER_T0_USING_FACTOR (lod_frac);
}

static void cc__t0_inter_t1_using_enva__sub_shade_mul_prim ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_COLOR, 0,
         GR_CMBX_ZERO, 0);
   CC_PRIM ();
   uint8_t factor = (uint8_t)(rdp.env_color&0xFF);
   T0_INTER_T1_USING_FACTOR (factor);
}

static void cc_one_sub__t0_mul_shadea__mul_shade () //Aded by Gonetz
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

// ** (A-B)*C+D **
static void cc_t0_sub_t1_mul_prim_mul_shade_add_t1 ()  //Aded by Gonetz
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

static void cc_t0_sub_prim_mul_t1_add_shade ()  //Aded by Gonetz
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

static void cc_t0_sub_prim_mul_primlod_add_prim ()  //Aded by Gonetz
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_COLOR, 0,
         GR_CMBX_B, 0);
   SETSHADE_PRIM ();
   CC_PRIMLOD ();
   USE_T0 ();
}

static void cc_t0_sub_prim_mul_env_add_shade ()  //Aded by Gonetz
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

static void cc__t0_inter_t1_using_shadea__sub_prim_mul_env_add_shade ()  //Aded by Gonetz
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
   CC_ENV ();
   SUBSHADE_PRIMMULENV ();
   T0_INTER_T1_USING_SHADEA ();
}

static void cc_t0_sub_prim_mul_enva_add_prim ()  //Aded by Gonetz41
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_COLOR, 0,
         GR_CMBX_B, 0);
   SETSHADE_PRIM ();
   CC_ENVA ();
   USE_T0 ();
}

static void cc_t0_sub_prim_mul_shade_add_env ()
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

static void cc_t1_sub_prim_mul_shade_add_env ()
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

static void cc_t1_sub_k4_mul_prima_add_t0 ()
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

static void cc__t0_sub_prim_mul_shade_add_env__mul_shade ()
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

static void cc__t0_sub_prim_mul_shade_add_env__mul_shadea ()
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

static void cc_t0_sub_k4_mul_k5_add_t0 ()  //Aded by Gonetz
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

static void cc__t0_inter_t1_using_t0__sub_shade_mul_prima_add_shade ()  //Aded by Gonetz
{
   cmb.tex |= 3;
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_COLOR, 0,
         GR_CMBX_B, 0);
   CC_PRIMA();
   T0_INTER_T1_USING_T0 ();
}

static void cc_t0_sub_env_mul_prima_add_env ()  //Aded by Gonetz
{
   T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_DETAIL_FACTOR, 0,
         GR_CMBX_B, 0);
   cmb.tex |= 1;
   percent = (rdp.prim_color&0xFF) / 255.0f;
   cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
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
   T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ZERO, 1,
         GR_CMBX_ZERO, 0);
   cmb.tex |= 1;
   cmb.tex_ccolor = rdp.env_color;
}

static void cc__t0_sub_t1_mul_enva_add_shade__sub_env_mul_prim ()
// (t0-t1)*env_a+shade, (cmb-env)*prim+0
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
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_B, 0);
  CC_ENV ();
  T0_INTER_T1_USING_FACTOR (lod_frac);
}

static void cc_one_sub_prim_mul_t0a_add_prim ()
{
   CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ONE_MINUS_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_ZERO,
         GR_CMBX_TEXTURE_ALPHA, 0,
         GR_CMBX_B, 0);
   CC_PRIM ();
   USE_T0 ();
}

static void cc_t0_sub_prim_mul_shade_add_shade ()  //Aded by Gonetz
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ITRGB, 0);
   CC_PRIM ();
   USE_T0 ();
}

static void cc__t0_mul_t0__sub_prim_mul_shade_add_shade ()  //Aded by Gonetz
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

static void cc__t0_mul_t1__sub_prim_mul_shade_add_shade ()  //Aded by Gonetz
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ITRGB, 0);
   CC_PRIM ();
   T0_MUL_T1 ();
}
