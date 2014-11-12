//forward decls
static void cc_t0_mul_env_mul_shade(void);
static void cc_t0_mul_shade(void);

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

static void cc__t0_mul_t1__sub_env_mul_shade_add_shade ()  //Aded by Gonetz
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ITRGB, 0);
   CC_ENV ();
   T0_MUL_T1 ();
}

static void cc_t0_inter_env_using_enva ()
{
   //(env-t0)*env_a+t0
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE);
   T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
         GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_TMU_CALPHA, 0,
         GR_CMBX_B, 0);
   cmb.tex_ccolor = rdp.env_color;
   cmb.tex |= 1;
}

static void cc_one_sub_shade_mul__t0_mul_shadea__add_shade ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE_MINUS_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_ITALPHA, 0,
        GR_CMBX_ZERO, 0);
  cmb.tex |= 1;
}

static void cc__t0_mul_t1_add_env__mul_shadea_add_shade ()
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
   // * not guaranteed to work if another iterated alpha is set
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
         GR_COMBINE_FACTOR_LOCAL_ALPHA,
         GR_COMBINE_LOCAL_ITERATED,
         GR_COMBINE_OTHER_TEXTURE);
}

static void cc_prim_sub_t0_mul_t1_add_t0 ()  //Aded by Gonetz
{
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
  CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
    GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
    GR_CMBX_ITRGB, 0,
    GR_CMBX_B, 0);
  CC_ENV ();
  USE_T0 ();
}

static void cc__prim_sub_env_mul_t0_add_env__add_primlod ()
{
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

static void cc_prim_sub_env_mul__one_sub_t0_mul_primlod_add_prim__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ONE_MINUS_X,
        GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ZERO,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
  cmb.dc0_detailmax = cmb.dc1_detailmax = (float)lod_frac / 255.0f;
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

static void cc_prim_sub_env_mul__t1_sub_prim_mul_prima_add_t0__add_env ()
{
   // (t1-prim)*prim_a+t0, (prim-env)*cmb+env
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
         GR_COMBINE_FACTOR_TEXTURE_RGB,
         GR_COMBINE_LOCAL_CONSTANT,
         GR_COMBINE_OTHER_ITERATED);
   CC_ENV ();
   SETSHADE_PRIM ();
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

static void cc_prim_sub_env_mul_t0a_add_t0 ()
{
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE);
   CC_PRIMSUBENV ();
   T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
         GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, 0,
         GR_CMBX_B, 0);
   cmb.tex_ccolor = cmb.ccolor;
   cmb.tex |= 1;
}

//Added by Gonetz
static void cc_prim_sub_env_mul__prim_inter_t0_using_shadea__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
  PRIM_INTER_T0_USING_SHADEA ();
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
  T0CCMBEXT(GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_B, 0);
  cmb.tex_ccolor = rdp.prim_color;
  cmb.tex |= 1;
  percent = (float)(lod_frac) / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

static void cc_prim_sub_env_mul__t0_sub_prim_mul_primlod_add_shade__add_env ()
{
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
   CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_B, 0);
   CC_PRIM ();
   T0_SUB_PRIM_MUL_PRIMLOD_ADD_T1 ();
}

//Added by Gonetz
static void cc__env_inter_prim_using_t0__sub_shade_mul_t0a_add_shade ()
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

//Added by Gonetz
static void cc_prim_sub_env_mul_prima_add_t0 ()
{
  if (rdp.prim_color != 0x000000ff)
  {
      CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
        GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_CONSTANT_ALPHA, 0,
        GR_CMBX_TEXTURE_RGB, 0);
      CC_PRIM ();
      SETSHADE_ENV ();
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

static void cc_prim_sub_env_mul__t1_sub_prim_mul_t0_add_t0__add_env ()
{
  CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
    GR_COMBINE_FACTOR_TEXTURE_RGB,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_ITERATED);
  CC_ENV ();
  SETSHADE_PRIM ();
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

//Added by Gonetz
static void cc__prim_sub_env_mul_prim_add_t0__mul_prim ()
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

//Added by Gonetz
static void cc_prim_sub_env_mul_prim_add_env ()
{
   CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_COLOR, 0,
         GR_CMBX_B, 0);
   SETSHADE_ENV();
   CC_PRIM ();
}

static void cc_prim_sub_env_mul_primlod_add_env ()
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

static void cc_prim_sub_env_mul_enva_add_env ()
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

//Added by Gonetz
static void cc_prim_sub_shade_mul__t0_inter_t1_using_shadea__add_shade ()
{
   CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_TEXTURE_RGB, 0,
         GR_CMBX_B, 0);
   CC_PRIM ();
   T0_INTER_T1_USING_SHADEA ();
}

static void cc__t0_sub_env_mul_shade__sub_prim_mul_shade_add_prim ()
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

static void cc__env_sub_shade_mul_t0_add_shade__mul_prim ()
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

//Added by Gonetz
static void cc_env_sub_shade_mul__t0_inter_t1_using_shadea__add_shade ()
{
   CCMBEXT(GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_X,
         GR_CMBX_ITRGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_TEXTURE_RGB, 0,
         GR_CMBX_B, 0);
   CC_ENV ();
   T0_INTER_T1_USING_SHADEA ();
}

static void cc__t0_mul_shade_mul_shadea__add__t1_mul_one_sub_shadea ()
{
   // (t0-0)*shade+0, (cmb-t0)*shadea+t0
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

//Added by Gonetz
static void cc_shade_sub_env_mul__t0_mul_t1__add__t0_mul_t1 ()
{
   CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_COLOR, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_TEXTURE_RGB, 0,
         GR_CMBX_TEXTURE_RGB, 0);
   CC_ENV ();
   T0_MUL_T1 ();
}

static void cc__t0_add_prim_mul_shade__mul_shade_add_env ()
{
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

//Added by Gonetz
static void cc_t1_inter_t0_using_shade ()
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

//Added by Gonetz
static void cc_t1_inter_t0_using_shadea ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_ZERO, 1,
         GR_CMBX_ZERO, 0);
   T1_INTER_T0_USING_SHADEA ();
}

static void cc_t0_inter_shade_using_t0a ()
{
   CCMBEXT(GR_CMBX_ITRGB, GR_FUNC_MODE_X,
         GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_TEXTURE_ALPHA, 0,
         GR_CMBX_B, 0);
   USE_T0();
   A_USE_T0();
}

//Added by Gonetz
static void cc__env_inter_t0_using_shadea__mul_shade ()
{
  //((t0-env)*shadea+env)*shade
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
  // * not guaranteed to work if another iterated alpha is set
  CCMB (GR_COMBINE_FUNCTION_BLEND,
    GR_COMBINE_FACTOR_LOCAL_ALPHA,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
  SETSHADE_PRIM ();
}

static void cc_env_inter__prim_inter_shade_using_t0__using_shadea ()
{
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

static void cc__t1_sub_prim_mul_t0_add_env__mul_shade () //Aded by Gonetz
{
   // (t1-prim)*t0+env, (cmb-0)*shade+0
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

static void cc__one_sub_shade_mul_t0_add_shade__mul_prim ()
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

static void cc__one_sub_shade_mul_t0_add_shade__mul_env ()
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

static void cc__t0_inter_t1_using_shadea__mul_shade ()
{
   CCMBEXT(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_ITRGB, 0,
         GR_CMBX_ZERO, 0);
   T0_INTER_T1_USING_SHADEA ();
}

static void cc_t0_inter_prim_using_prima ()
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

static void cc__env_inter_one_using_t0__mul_shade ()
{
   //(one-env)*t0+env, (cmb-0)*shade+0
   T0CCMBEXT(GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_ZERO,
         GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_ONE_MINUS_X,
         GR_CMBX_LOCAL_TEXTURE_RGB, 0,
         GR_CMBX_B, 0);
   cmb.tex_ccolor = rdp.env_color&0xFFFFFF00;
   cmb.tex |= 1;
   CCMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_LOCAL,
         GR_COMBINE_LOCAL_ITERATED,
         GR_COMBINE_OTHER_TEXTURE);
}

static void cc_env_inter_one_using__one_sub_t0_mul_primlod ()
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

static void cc__one_inter_prim_using_t1__mul_shade ()
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

static void cc_prim_sub__prim_sub_t0_mul_prima__mul_shade ()
{
   // (prim-t0)*prim_a+0, (prim-cmb)*shade+0
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

static void cc__env_inter_prim_using__t0_sub_shade_mul_primlod_add_env ()
{
   // (t0-shade)*lodf+env, (prim-env)*cmb+env
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

static void cc__t0_mul_shade__inter_env_using_enva ()
{
  // (t0-0)*shade+0, (env-cmb)*env_a+cmb    ** INC **
  uint32_t enva  = rdp.env_color&0xFF;
  if (enva == 0xFF)
    cc_env ();
  else if (enva == 0)
    cc_t0_mul_shade ();
  else
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
}

static void cc__t0_mul_shade__inter_one_using_shadea ()
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

static void ac_t0_mul_t1_add_t1 ()
{
   ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE);
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

//Added by Gonetz
static void ac__t1_sub_one_mul_primlod_add_t0__mul_prim ()
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

static void ac__t0_sub_one_mul_enva_add_t0__mul_prim ()
{
   ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_LOCAL,
         GR_COMBINE_LOCAL_CONSTANT,
         GR_COMBINE_OTHER_TEXTURE);
   CA_PRIM ();
   T0ACMBEXT(GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_TMU_CALPHA, 0,
         GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
   SETSHADE_A(0xFF);
   cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF) ;
   cmb.tex |= 1;
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

static void ac__t1_sub_t0_mul_enva_add_t1__mul_prim ()
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
  T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CALPHA, 0,
        GR_CMBX_B, 0);
  cmb.tex |= 3;
  cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (rdp.env_color&0xFF);
}

static void ac__t1_sub_t0_mul_primlod__mul_env_add_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
  T1ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
        GR_CMBX_ZERO, 0,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
  T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_TMU_CALPHA, 0,
        GR_CMBX_ZERO, 0);
  cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (uint32_t)((float)(rdp.env_color&0xFF)*(float)rdp.prim_lodfrac/255.0f);
  cmb.tex |= 3;
}

static void ac__t0_sub_one_mul_enva_add_t1__mul_prim ()
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

static void ac__t1_sub_one_mul_primlod_add_t0__mul_env ()
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
  T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
  cmb.tex |= 1;
  percent = (float)lod_frac / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

//Added by Gonetz
static void ac__t0_sub_t1__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
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

static void ac__t1_mul_t1_add_t1__mul_shade ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_ITERATED,
    GR_COMBINE_OTHER_TEXTURE);
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

static void ac__t1_sub_one_mul_primlod_add_t0__mul_shade ()
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

static void ac__prim_sub_one_mul_primlod_add_t0__mul_env ()
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
   ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_LOCAL,
         GR_COMBINE_LOCAL_CONSTANT,
         GR_COMBINE_OTHER_TEXTURE);
   CA_ENV ();
}

// ** A*B+C **
static void ac_t0_mul_prim_add_t0 ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_B, 0);
   CA_PRIM ();
   A_USE_T0 ();
}

static void ac__t0_inter_t1_using_t1a__mul_prim_add__t0_inter_t1_using_t1a ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_B, 0);
   CA_PRIM ();
   A_T0_INTER_T1_USING_T1A ();
}

static void ac__t1_inter_t0_using_t0a__mul_prim_add__t1_inter_t0_using_t0a ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_B, 0);
   CA_PRIM ();
   A_T1_INTER_T0_USING_T0A ();
}

static void ac_t0_mul_primlod_add_t0 ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE);
  T0ACMBEXT(GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_ZERO,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, GR_FUNC_MODE_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
  cmb.tex |= 1;
  percent = (float)lod_frac / 255.0f;
  cmb.dc0_detailmax = cmb.dc1_detailmax = percent;
}

// ** (A*B+C)*D **
static void ac__t0_mul_primlod_add_shade__mul_shade ()
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

static void ac__t1_mul_primlod_add_shade__mul_shade ()
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

// ** ((A-B)*C+D)+E **
static void ac__t0_sub_t1_mul_prim_add_shade__mul_shade ()
 //(t0-t1)*prim+shade, (cmb-0)*shade+0
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

static void ac__t1_sub_t0_mul_prim_add_shade__mul_shade ()
 //(t1-t0)*prim+shade, (cmb-0)*shade+0
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

static void ac__t0_add_prim_mul_shade__mul_shade ()
{
   // (shade-0)*prim+t0, (cmb-0)*shade+0
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

// ** (A-B)*C **
static void ac_t0_sub_prim_mul_shade ()
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
}

static void ac_t0_sub_prim_mul_shade_mul_env ()
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
}

static void ac_t0_sub_shade_mul_prim ()
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

static void ac__t1_mul_primlod_add_t0__sub_prim_mul_shade ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ZERO, 0);
   CA_PRIM ();
   A_T1_MUL_PRIMLOD_ADD_T0 ();
}

static void ac__t1_mul_primlod_add_t0__sub_env_mul_prim ()  //Aded by Gonetz
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ZERO, 0);
   CA_ENV ();
   SETSHADE_A_PRIM ();
   A_T1_MUL_PRIMLOD_ADD_T0 ();
}

static void ac__t1_mul_prima_add_t0__sub_env_mul_shade ()  //Aded by Gonetz
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ZERO, 0);
   CA_ENV ();
   A_T1_MUL_PRIMA_ADD_T0 ();
}

static void ac_prim_sub_shade_mul_prim ()  //Aded by Gonetz
{
   ACMBEXT(GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_ZERO, 0);
   CA_PRIM();
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
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_TEXTURE_ALPHA, 0);
   CA_ENV();
   SETSHADE_A_PRIM ();
   A_T0_MUL_T1 ();
}

// ** (A-B)*C+D **
static void ac__t0_sub_prim_mul_shade_add_shade__mul_env ()  //Aded by Gonetz
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

static void ac_t0_sub_one_mul_enva_add_t1 ()  //Aded by Gonetz
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

static void ac_t1_sub_one_mul_enva_add_t0 ()  //Aded by Gonetz
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

static void ac_t1_sub_one_mul_primlod_add_t0 ()  //Aded by Gonetz
{
   ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE);
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

static void ac_t1_sub_prim_mul_shade_add_prim ()  //Aded by Gonetz
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

static void ac_t0_sub_env_mul_shadea_add_env ()  //Aded by Gonetz
{
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

static void ac_shade_sub_t0_mul_primlod_add_prim ()
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

//Added by Gonetz
static void ac_t0_inter_t1_using_shadea ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_ZERO, 1,
         GR_CMBX_ZERO, 0);
  A_T0_INTER_T1_USING_SHADEA ();
}

static void ac__env_sub_one_mul_t1_add_t0__mul_prim ()
{
  ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_LOCAL,
    GR_COMBINE_LOCAL_CONSTANT,
    GR_COMBINE_OTHER_TEXTURE);
  CA_PRIM ();
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

static void ac__t1_mul_enva_add_t0__sub_prim_mul_shade ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, GR_FUNC_MODE_NEGATIVE_X,
         GR_CMBX_ITALPHA, 0,
         GR_CMBX_ZERO, 0);
   CA_PRIM ();
   A_T1_MUL_ENVA_ADD_T0 ();
}

//Added by Gonetz
static void ac__t0_inter_t1_using_shadea__mul_prim ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_ZERO, 0);
   A_T0_INTER_T1_USING_SHADEA ();
   CA_PRIM ();
}

//Added by Gonetz
static void ac__t0_inter_t1_using_shadea__mul_env ()
{
   ACMBEXT(GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_ITALPHA, GR_FUNC_MODE_ZERO,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_ZERO, 0);
   A_T0_INTER_T1_USING_SHADEA ();
   CA_ENV ();
}

//Added by Gonetz
static void ac__t0_inter_t1_using_primlod__mul_env_add__t0_inter_t1_using_primlod ()
{
   ACMBEXT(GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
         GR_CMBX_TEXTURE_ALPHA, GR_FUNC_MODE_X,
         GR_CMBX_CONSTANT_ALPHA, 0,
         GR_CMBX_B, 0);
   CA_ENV ();
   A_T0_INTER_T1_USING_FACTOR (lod_frac);
}

static void ac__t1_sub_one_mul_enva_add_t0__mul_prim ()
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
  T0ACMBEXT(GR_CMBX_OTHER_TEXTURE_ALPHA, GR_FUNC_MODE_X,
        GR_CMBX_TMU_CALPHA, GR_FUNC_MODE_NEGATIVE_X,
        GR_CMBX_DETAIL_FACTOR, 0,
        GR_CMBX_LOCAL_TEXTURE_ALPHA, 0);
  cmb.tex_ccolor = (cmb.tex_ccolor&0xFFFFFF00) | (0xFF) ;
  cmb.tex |= 3;
  cmb.dc0_detailmax = cmb.dc1_detailmax = (float)(rdp.env_color&0xFF) / 255.0f;
}

static void ac__one_inter_t0_using_prim__mul_env ()
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

static void ac__t1_sub_one_mul_enva_add_t0__mul_shade ()
{
   ACMB (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_LOCAL,
         GR_COMBINE_LOCAL_ITERATED,
         GR_COMBINE_OTHER_TEXTURE);
   CA_PRIM ();
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
