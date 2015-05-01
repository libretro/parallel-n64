#include <string.h>
#include <stdlib.h>
#include "OpenGL.h"
#include "ShaderCombiner.h"
#include "Common.h"
#include "Textures.h"
#include "Config.h"


//(sa - sb) * m + a
static const u32 saRGBExpanded[] =
{
    COMBINED,           TEXEL0,             TEXEL1,             PRIMITIVE,
    SHADE,              ENVIRONMENT,        ONE,                NOISE,
    ZERO,               ZERO,               ZERO,               ZERO,
    ZERO,               ZERO,               ZERO,               ZERO
};

static const u32 sbRGBExpanded[] =
{
    COMBINED,           TEXEL0,             TEXEL1,             PRIMITIVE,
    SHADE,              ENVIRONMENT,        CENTER,             K4,
    ZERO,               ZERO,               ZERO,               ZERO,
    ZERO,               ZERO,               ZERO,               ZERO
};

static const u32 mRGBExpanded[] =
{
    COMBINED,           TEXEL0,             TEXEL1,             PRIMITIVE,
    SHADE,              ENVIRONMENT,        SCALE,              COMBINED_ALPHA,
    TEXEL0_ALPHA,       TEXEL1_ALPHA,       PRIMITIVE_ALPHA,    SHADE_ALPHA,
    ENV_ALPHA,          LOD_FRACTION,       PRIM_LOD_FRAC,      K5,
    ZERO,               ZERO,               ZERO,               ZERO,
    ZERO,               ZERO,               ZERO,               ZERO,
    ZERO,               ZERO,               ZERO,               ZERO,
    ZERO,               ZERO,               ZERO,               ZERO
};

static const u32 aRGBExpanded[] =
{
    COMBINED,           TEXEL0,             TEXEL1,             PRIMITIVE,
    SHADE,              ENVIRONMENT,        ONE,                ZERO
};

static const u32 saAExpanded[] =
{
    COMBINED,           TEXEL0_ALPHA,       TEXEL1_ALPHA,       PRIMITIVE_ALPHA,
    SHADE_ALPHA,        ENV_ALPHA,          ONE,                ZERO
};

static const u32 sbAExpanded[] =
{
    COMBINED,           TEXEL0_ALPHA,       TEXEL1_ALPHA,       PRIMITIVE_ALPHA,
    SHADE_ALPHA,        ENV_ALPHA,          ONE,                ZERO
};

static const u32 mAExpanded[] =
{
    LOD_FRACTION,       TEXEL0_ALPHA,       TEXEL1_ALPHA,       PRIMITIVE_ALPHA,
    SHADE_ALPHA,        ENV_ALPHA,          PRIM_LOD_FRAC,      ZERO,
};

static const u32 aAExpanded[] =
{
    COMBINED,           TEXEL0_ALPHA,       TEXEL1_ALPHA,       PRIMITIVE_ALPHA,
    SHADE_ALPHA,        ENV_ALPHA,          ONE,                ZERO
};

ShaderProgram *scProgramRoot = NULL;
ShaderProgram *scProgramCurrent = NULL;
int scProgramChanged = 0;
int scProgramCount = 0;

GLint _vertex_shader = 0;

const char *_frag_header = "                                \n"
#if defined(__LIBRETRO__) && !defined(HAVE_OPENGLES2) // Desktop GL fix
"#version 120                                               \n"
"#define highp                                              \n"
"#define lowp                                               \n"
"#define mediump                                            \n"
#endif
"uniform sampler2D uTex0;                                   \n"\
"uniform sampler2D uTex1;                                   \n"\
"uniform sampler2D uTexNoise;                               \n"\
"uniform lowp vec4 uEnvColor;                               \n"\
"uniform lowp vec4 uPrimColor;                              \n"\
"uniform lowp vec4 uFogColor;                               \n"\
"uniform highp float uAlphaRef;                             \n"\
"uniform lowp float uPrimLODFrac;                           \n"\
"uniform lowp float uK4;                                    \n"\
"uniform lowp float uK5;                                    \n"\
"                                                           \n"\
"varying lowp float vFactor;                                \n"\
"varying lowp vec4 vShadeColor;                             \n"\
"varying mediump vec2 vTexCoord0;                           \n"\
"varying mediump vec2 vTexCoord1;                           \n"\
"                                                           \n"\
"void main()                                                \n"\
"{                                                          \n"\
"lowp vec4 lFragColor;                                      \n";


const char *_vert = "                                       \n"
#if defined(__LIBRETRO__) && !defined(HAVE_OPENGLES2) // Desktop GL fix
"#version 120                                               \n"
"#define highp                                              \n"
"#define lowp                                               \n"
"#define mediump                                            \n"
#endif
"attribute highp vec4 	aPosition;                          \n"\
"attribute lowp vec4 	aColor;                             \n"\
"attribute highp vec2   aTexCoord0;                         \n"\
"attribute highp vec2   aTexCoord1;                         \n"\
"                                                           \n"\
"uniform bool		    uEnableFog;                         \n"\
"uniform float			 uFogScale, uFogOffset;         \n"\
"uniform float 			uRenderState;                       \n"\
"                                                           \n"\
"uniform mediump vec2 	uTexScale;                          \n"\
"uniform mediump vec2 	uTexOffset[2];                      \n"\
"uniform mediump vec2 	uCacheShiftScale[2];                \n"\
"uniform mediump vec2 	uCacheScale[2];                     \n"\
"uniform mediump vec2 	uCacheOffset[2];                    \n"\
"                                                           \n"\
"varying lowp float     vFactor;                            \n"\
"varying lowp vec4 		vShadeColor;                        \n"\
"varying mediump vec2 	vTexCoord0;                         \n"\
"varying mediump vec2 	vTexCoord1;                         \n"\
"                                                           \n"\
"void main()                                                \n"\
"{                                                          \n"\
"gl_Position = aPosition;                                   \n"\
"vShadeColor = aColor;                                      \n"\
"                                                           \n"\
"if (uRenderState == 1.0)                                   \n"\
"{                                                          \n"\
"vTexCoord0 = (aTexCoord0 * (uTexScale[0] *                 \n"\
"           uCacheShiftScale[0]) + (uCacheOffset[0] -       \n"\
"           uTexOffset[0])) * uCacheScale[0];               \n"\
"vTexCoord1 = (aTexCoord0 * (uTexScale[1] *                 \n"\
"           uCacheShiftScale[1]) + (uCacheOffset[1] -       \n"\
"           uTexOffset[1])) * uCacheScale[1];               \n"\
"}                                                          \n"\
"else                                                       \n"\
"{                                                          \n"\
"vTexCoord0 = aTexCoord0;                                   \n"\
"vTexCoord1 = aTexCoord1;                                   \n"\
"}                                                          \n"\
"                                                           \n";

const char * _vertfog = "                                   \n"\
"if (uEnableFog)                                            \n"\
"{                                                          \n"\
"vFactor = max(-1.0, aPosition.z / aPosition.w)             \n"\
"   * uFogScale + uFogOffset;                          \n"\
"vFactor = clamp(vFactor, 0.0, 1.0);                        \n"\
"}                                                          \n";

const char * _vertzhack = "                                 \n"\
"if (uRenderState == 1.0)                                   \n"\
"{                                                          \n"\
"gl_Position.z = (gl_Position.z + gl_Position.w*9.0) * 0.1; \n"\
"}                                                          \n";


static const char * _color_param_str(int param)
{
   switch(param)
   {
      case COMBINED:          return "lFragColor.rgb";
      case TEXEL0:            return "lTex0.rgb";
      case TEXEL1:            return "lTex1.rgb";
      case PRIMITIVE:         return "uPrimColor.rgb";
      case SHADE:             return "vShadeColor.rgb";
      case ENVIRONMENT:       return "uEnvColor.rgb";
      case CENTER:            return "vec3(0.0)";
      case SCALE:             return "vec3(0.0)";
      case COMBINED_ALPHA:    return "vec3(lFragColor.a)";
      case TEXEL0_ALPHA:      return "vec3(lTex0.a)";
      case TEXEL1_ALPHA:      return "vec3(lTex1.a)";
      case PRIMITIVE_ALPHA:   return "vec3(uPrimColor.a)";
      case SHADE_ALPHA:       return "vec3(vShadeColor.a)";
      case ENV_ALPHA:         return "vec3(uEnvColor.a)";
      case LOD_FRACTION:      return "vec3(0.0)";
      case PRIM_LOD_FRAC:     return "vec3(uPrimLODFrac)";
      case NOISE:             return "lNoise.rgb";
      case K4:                return "vec3(uK4)";
      case K5:                return "vec3(uK5)";
      case ONE:               return "vec3(1.0)";
      case ZERO:              return "vec3(0.0)";
      default:
                              return "vec3(0.0)";
   }
}

static const char * _alpha_param_str(int param)
{
   switch(param)
   {
      case COMBINED:          return "lFragColor.a";
      case TEXEL0:            return "lTex0.a";
      case TEXEL1:            return "lTex1.a";
      case PRIMITIVE:         return "uPrimColor.a";
      case SHADE:             return "vShadeColor.a";
      case ENVIRONMENT:       return "uEnvColor.a";
      case CENTER:            return "0.0";
      case SCALE:             return "0.0";
      case COMBINED_ALPHA:    return "lFragColor.a";
      case TEXEL0_ALPHA:      return "lTex0.a";
      case TEXEL1_ALPHA:      return "lTex1.a";
      case PRIMITIVE_ALPHA:   return "uPrimColor.a";
      case SHADE_ALPHA:       return "vShadeColor.a";
      case ENV_ALPHA:         return "uEnvColor.a";
      case LOD_FRACTION:      return "0.0";
      case PRIM_LOD_FRAC:     return "uPrimLODFrac";
      case NOISE:             return "lNoise.a";
      case K4:                return "uK4";
      case K5:                return "uK5";
      case ONE:               return "1.0";
      case ZERO:              return "0.0";
      default:
                              return "0.0";
   }
}

static bool mux_find(DecodedMux *dmux, int index, int src)
{
      if (dmux->decode[index].sa == src) return true;
      if (dmux->decode[index].sb == src) return true;
      if (dmux->decode[index].m == src) return true;
      if (dmux->decode[index].a == src) return true;
   return false;
}

static bool mux_swap(DecodedMux *dmux, int cycle, int src0, int src1)
{
   int i, r;
   r = false;
   for(i = 0; i < 2; i++)
   {
      int ii = (cycle == 0) ? i : (2+i);
      {
         if (dmux->decode[ii].sa == src0) {dmux->decode[ii].sa = src1; r=true;}
         else if (dmux->decode[ii].sa == src1) {dmux->decode[ii].sa = src0; r=true;}

         if (dmux->decode[ii].sb == src0) {dmux->decode[ii].sb = src1; r=true;}
         else if (dmux->decode[ii].sb == src1) {dmux->decode[ii].sb = src0; r=true;}

         if (dmux->decode[ii].m == src0) {dmux->decode[ii].m = src1; r=true;}
         else if (dmux->decode[ii].m == src1) {dmux->decode[ii].m = src0; r=true;}

         if (dmux->decode[ii].a == src0) {dmux->decode[ii].a = src1; r=true;}
         else if (dmux->decode[ii].a == src1) {dmux->decode[ii].a = src0; r=true;}
      }
   }
   return r;
}

static bool mux_replace(DecodedMux *dmux, int cycle, int src, int dest)
{
   int i, r;
   r = false;

   for(i = 0; i < 2; i++)
   {
      int ii = (cycle == 0) ? i : (2+i);
      if (dmux->decode[ii].sa == src) {dmux->decode[ii].sa = dest; r=true;}
      if (dmux->decode[ii].sb == src) {dmux->decode[ii].sb = dest; r=true;}
      if (dmux->decode[ii].m  == src) {dmux->decode[ii].m  = dest; r=true;}
      if (dmux->decode[ii].a  == src) {dmux->decode[ii].a  = dest; r=true;}
   }
   return r;
}

static void *mux_new(u64 dmux, bool cycle2)
{
   int i;
   DecodedMux *mux = malloc(sizeof(DecodedMux)); 

   mux->combine.mux = dmux;
   mux->flags = 0;

   //set to ZERO.
   for(i = 0; i < 4;i++)
   {
      mux->decode[i].sa = ZERO;
      mux->decode[i].sb = ZERO;
      mux->decode[i].m  = ZERO;
      mux->decode[i].a  = ZERO;
   }

   //rgb cycle 0
   mux->decode[0].sa = saRGBExpanded[mux->combine.saRGB0];
   mux->decode[0].sb = sbRGBExpanded[mux->combine.sbRGB0];
   mux->decode[0].m  = mRGBExpanded[mux->combine.mRGB0];
   mux->decode[0].a  = aRGBExpanded[mux->combine.aRGB0];
   mux->decode[1].sa = saAExpanded[mux->combine.saA0];
   mux->decode[1].sb = sbAExpanded[mux->combine.sbA0];
   mux->decode[1].m  = mAExpanded[mux->combine.mA0];
   mux->decode[1].a  = aAExpanded[mux->combine.aA0];

   if (cycle2)
   {
      //rgb cycle 1
      mux->decode[2].sa = saRGBExpanded[mux->combine.saRGB1];
      mux->decode[2].sb = sbRGBExpanded[mux->combine.sbRGB1];
      mux->decode[2].m  = mRGBExpanded[mux->combine.mRGB1];
      mux->decode[2].a  = aRGBExpanded[mux->combine.aRGB1];
      mux->decode[3].sa = saAExpanded[mux->combine.saA1];
      mux->decode[3].sb = sbAExpanded[mux->combine.sbA1];
      mux->decode[3].m  = mAExpanded[mux->combine.mA1];
      mux->decode[3].a  = aAExpanded[mux->combine.aA1];

      //texel 0/1 are swapped in 2nd cycle.
      mux_swap(mux, 1, TEXEL0, TEXEL1);
      mux_swap(mux, 1, TEXEL0_ALPHA, TEXEL1_ALPHA);
   }

   //simplifying mux:
   if (mux_replace(mux, G_CYC_1CYCLE, LOD_FRACTION, ZERO) || mux_replace(mux, G_CYC_2CYCLE, LOD_FRACTION, ZERO))
      LOG(LOG_VERBOSE, "SC Replacing LOD_FRACTION with ZERO\n");
#if 1
   if (mux_replace(mux, G_CYC_1CYCLE, K4, ZERO) || mux_replace(mux, G_CYC_2CYCLE, K4, ZERO))
      LOG(LOG_VERBOSE, "SC Replacing K4 with ZERO\n");

   if (mux_replace(mux, G_CYC_1CYCLE, K5, ZERO) || mux_replace(mux, G_CYC_2CYCLE, K5, ZERO))
      LOG(LOG_VERBOSE, "SC Replacing K5 with ZERO\n");
#endif

   if (mux_replace(mux, G_CYC_1CYCLE, CENTER, ZERO) || mux_replace(mux, G_CYC_2CYCLE, CENTER, ZERO))
      LOG(LOG_VERBOSE, "SC Replacing CENTER with ZERO\n");

   if (mux_replace(mux, G_CYC_1CYCLE, SCALE, ZERO) || mux_replace(mux, G_CYC_2CYCLE, SCALE, ZERO))
      LOG(LOG_VERBOSE, "SC Replacing SCALE with ZERO\n");

   //Combiner has initial value of zero in cycle 0
   if (mux_replace(mux, G_CYC_1CYCLE, COMBINED, ZERO))
      LOG(LOG_VERBOSE, "SC Setting CYCLE1 COMBINED to ZERO\n");

   if (mux_replace(mux, G_CYC_1CYCLE, COMBINED_ALPHA, ZERO))
      LOG(LOG_VERBOSE, "SC Setting CYCLE1 COMBINED_ALPHA to ZERO\n");

   if (!config.enableNoise)
   {
      if (mux_replace(mux, G_CYC_1CYCLE, NOISE, ZERO))
         LOG(LOG_VERBOSE, "SC Setting CYCLE1 NOISE to ZERO\n");

      if (mux_replace(mux, G_CYC_2CYCLE, NOISE, ZERO))
         LOG(LOG_VERBOSE, "SC Setting CYCLE2 NOISE to ZERO\n");

   }

   //mutiplying by zero: (A-B)*0 + C = C
   for(i = 0 ; i < 4; i++)
   {
      if (mux->decode[i].m == ZERO)
      {
         mux->decode[i].sa = ZERO;
         mux->decode[i].sb = ZERO;
      }
   }

   //(A1-B1)*C1 + D1
   //(A2-B2)*C2 + D2
   //1. ((A1-B1)*C1 + D1 - B2)*C2 + D2 = A1*C1*C2 - B1*C1*C2 + D1*C2 - B2*C2 + D2
   //2. (A2 - (A1-B1)*C1 - D1)*C2 + D2 = A2*C2 - A1*C1*C2 + B1*C1*C2 - D1*C2 + D2
   //3. (A2 - B2)*((A1-B1)*C1 + D1) + D2 = A2*A1*C1 - A2*B1*C1 + A2*D1 - B2*A1*C1 + B2*B1*C1 - B2*D1 + D2
   //4. (A2-B2)*C2 + (A1-B1)*C1 + D1 = A2*C2 - B2*C2 + A1*C1 - B1*C1 + D1

   if (cycle2)
   {

      if (!mux_find(mux, 2, COMBINED))
         mux->flags |= SC_IGNORE_RGB0;

      if (!(mux_find(mux, 2, COMBINED_ALPHA) || mux_find(mux, 3, COMBINED_ALPHA) || mux_find(mux, 3, COMBINED)))
         mux->flags |= SC_IGNORE_ALPHA0;

      if (mux->decode[2].sa == ZERO && mux->decode[2].sb == ZERO && mux->decode[2].m == ZERO && mux->decode[2].a == COMBINED)
         mux->flags |= SC_IGNORE_RGB1;

      if (mux->decode[3].sa == ZERO && mux->decode[3].sb == ZERO && mux->decode[3].m == ZERO &&
            (mux->decode[3].a == COMBINED_ALPHA || mux->decode[3].a == COMBINED))
         mux->flags |= SC_IGNORE_ALPHA1;
   }

   return mux;
}


static int program_compare(ShaderProgram *prog, DecodedMux *dmux, u32 flags)
{
   if (prog)
      return ((prog->combine.mux == dmux->combine.mux) && (prog->flags == flags));
   return 1;
}

static void glcompiler_error(GLint shader)
{
   int len, i;
   char* log;

   glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
   log = (char*) malloc(len + 1);
   glGetShaderInfoLog(shader, len, &i, log);
   log[len] = 0;
   LOG(LOG_ERROR, "COMPILE ERROR: %s \n", log);
   free(log);
}

static void gllinker_error(GLint program)
{
   int len, i;
   char* log;

   glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
   log = (char*)malloc(len + 1);
   glGetProgramInfoLog(program, len, &i, log);
   log[len] = 0;
   LOG(LOG_ERROR, "LINK ERROR: %s \n", log);
   free(log);
}

static void locate_attributes(ShaderProgram *p)
{
   glBindAttribLocation(p->program, SC_POSITION,   "aPosition");
   glBindAttribLocation(p->program, SC_COLOR,      "aColor");
   glBindAttribLocation(p->program, SC_TEXCOORD0,  "aTexCoord0");
   glBindAttribLocation(p->program, SC_TEXCOORD1,  "aTexCoord1");
}

#define LocateUniform(A) \
    p->uniforms.A.loc = glGetUniformLocation(p->program, #A);

static void locate_uniforms(ShaderProgram *p)
{
   LocateUniform(uTex0);
   LocateUniform(uTex1);
   LocateUniform(uTexNoise);
   LocateUniform(uEnvColor);
   LocateUniform(uPrimColor);
   LocateUniform(uPrimLODFrac);
   LocateUniform(uK4);
   LocateUniform(uK5);
   LocateUniform(uFogColor);
   LocateUniform(uEnableFog);
   LocateUniform(uRenderState);
   LocateUniform(uFogScale);
   LocateUniform(uFogOffset);
   LocateUniform(uAlphaRef);
   LocateUniform(uTexScale);
   LocateUniform(uTexOffset[0]);
   LocateUniform(uTexOffset[1]);
   LocateUniform(uCacheShiftScale[0]);
   LocateUniform(uCacheShiftScale[1]);
   LocateUniform(uCacheScale[0]);
   LocateUniform(uCacheScale[1]);
   LocateUniform(uCacheOffset[0]);
   LocateUniform(uCacheOffset[1]);
}

static void force_uniforms(void)
{
   SC_ForceUniform1i(uTex0, 0);
   SC_ForceUniform1i(uTex1, 1);
   SC_ForceUniform1i(uTexNoise, 2);
   SC_ForceUniform4fv(uEnvColor, &gDP.envColor.r);
   SC_ForceUniform4fv(uPrimColor, &gDP.primColor.r);
   SC_ForceUniform1f(uPrimLODFrac, gDP.primColor.l);
   SC_ForceUniform1f(uK4, gDP.convert.k4);
   SC_ForceUniform1f(uK5, gDP.convert.k5);
   SC_ForceUniform4fv(uFogColor, &gDP.fogColor.r);
   SC_ForceUniform1i(uEnableFog, ((gSP.geometryMode & G_FOG)));
   SC_ForceUniform1f(uRenderState, (float) OGL.renderState);
   SC_ForceUniform1f(uFogScale, (float) gSP.fog.multiplier / 256.0f);
   SC_ForceUniform1f(uFogOffset, (float) gSP.fog.offset / 255.0f);
   SC_ForceUniform1f(uAlphaRef, (gDP.otherMode.cvgXAlpha) ? 0.5f : gDP.blendColor.a);
   SC_ForceUniform2f(uTexScale, gSP.texture.scales, gSP.texture.scalet);

   if (gSP.textureTile[0])
      SC_ForceUniform2f(uTexOffset[0], gSP.textureTile[0]->fuls, gSP.textureTile[0]->fult);
   else
      SC_ForceUniform2f(uTexOffset[0], 0.0f, 0.0f);

   if (gSP.textureTile[1])
      SC_ForceUniform2f(uTexOffset[1], gSP.textureTile[1]->fuls, gSP.textureTile[1]->fult);
   else
      SC_ForceUniform2f(uTexOffset[1], 0.0f, 0.0f);

   if (cache.current[0])
   {
      SC_ForceUniform2f(uCacheShiftScale[0], cache.current[0]->shiftScaleS, cache.current[0]->shiftScaleT);
      SC_ForceUniform2f(uCacheScale[0], cache.current[0]->scaleS, cache.current[0]->scaleT);
      SC_ForceUniform2f(uCacheOffset[0], cache.current[0]->offsetS, cache.current[0]->offsetT);
   }
   else
   {
      SC_ForceUniform2f(uCacheShiftScale[0], 1.0f, 1.0f);
      SC_ForceUniform2f(uCacheScale[0], 1.0f, 1.0f);
      SC_ForceUniform2f(uCacheOffset[0], 0.0f, 0.0f);
   }

   if (cache.current[1])
   {
      SC_ForceUniform2f(uCacheShiftScale[1], cache.current[1]->shiftScaleS, cache.current[1]->shiftScaleT);
      SC_ForceUniform2f(uCacheScale[1], cache.current[1]->scaleS, cache.current[1]->scaleT);
      SC_ForceUniform2f(uCacheOffset[1], cache.current[1]->offsetS, cache.current[1]->offsetT);
   }
   else
   {
      SC_ForceUniform2f(uCacheShiftScale[1], 1.0f, 1.0f);
      SC_ForceUniform2f(uCacheScale[1], 1.0f, 1.0f);
      SC_ForceUniform2f(uCacheOffset[1], 0.0f, 0.0f);
   }
}

void Combiner_Init(void)
{
   //compile vertex shader:
   GLint success;
   const char *src[1];
   char buff[4096], *str;
   str = buff;

   str += sprintf(str, "%s", _vert);
   str += sprintf(str, "%s", _vertfog);
   if (config.zHack)
      str += sprintf(str, "%s", _vertzhack);

   str += sprintf(str, "}\n\n");

   src[0] = buff;
   _vertex_shader = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(_vertex_shader, 1, (const char**) src, NULL);
   glCompileShader(_vertex_shader);
   glGetShaderiv(_vertex_shader, GL_COMPILE_STATUS, &success);
   if (!success)
      glcompiler_error(_vertex_shader);

   gDP.otherMode.cycleType = G_CYC_1CYCLE;
}

static void Combiner_DeletePrograms(ShaderProgram *prog)
{
   if (prog)
   {
      Combiner_DeletePrograms(prog->left);
      Combiner_DeletePrograms(prog->right);
      glDeleteProgram(prog->program);
      //glDeleteShader(prog->fragment);
      free(prog);
      scProgramCount--;
   }
}

void Combiner_Destroy(void)
{
   Combiner_DeletePrograms(scProgramRoot);
   glDeleteShader(_vertex_shader);
   scProgramCount = scProgramChanged = 0;
   scProgramRoot = scProgramCurrent = NULL;
}

static ShaderProgram *ShaderCombiner_Compile(DecodedMux *dmux, int flags)
{
   int i, j;
   GLint success, len[1];
   char frag[4096], *src[1], *buffer;
   ShaderProgram *prog;

   buffer = (char*)frag;
   prog = (ShaderProgram*) malloc(sizeof(ShaderProgram));

   prog->left = prog->right = NULL;
   prog->usesT0 = prog->usesT1 = prog->usesCol = prog->usesNoise = 0;
   prog->combine = dmux->combine;
   prog->flags = flags;
   prog->vertex = _vertex_shader;

   for(i = 0; i < ((flags & SC_2CYCLE) ? 4 : 2); i++)
   {
      //make sure were not ignoring cycle:
      if ((dmux->flags&(1<<i)) == 0)
      {
         {
            prog->usesT0 |= (dmux->decode[i].sa == TEXEL0 || dmux->decode[i].sa == TEXEL0_ALPHA);
            prog->usesT1 |= (dmux->decode[i].sa == TEXEL1 || dmux->decode[i].sa == TEXEL1_ALPHA);
            prog->usesCol |= (dmux->decode[i].sa == SHADE || dmux->decode[i].sa == SHADE_ALPHA);
            prog->usesNoise |= (dmux->decode[i].sa == NOISE);

            prog->usesT0 |= (dmux->decode[i].sb == TEXEL0 || dmux->decode[i].sb == TEXEL0_ALPHA);
            prog->usesT1 |= (dmux->decode[i].sb == TEXEL1 || dmux->decode[i].sb == TEXEL1_ALPHA);
            prog->usesCol |= (dmux->decode[i].sb == SHADE || dmux->decode[i].sb == SHADE_ALPHA);
            prog->usesNoise |= (dmux->decode[i].sb == NOISE);

            prog->usesT0 |= (dmux->decode[i].m == TEXEL0 || dmux->decode[i].m == TEXEL0_ALPHA);
            prog->usesT1 |= (dmux->decode[i].m == TEXEL1 || dmux->decode[i].m == TEXEL1_ALPHA);
            prog->usesCol |= (dmux->decode[i].m == SHADE || dmux->decode[i].m == SHADE_ALPHA);
            prog->usesNoise |= (dmux->decode[i].m == NOISE);

            prog->usesT0 |= (dmux->decode[i].a == TEXEL0 || dmux->decode[i].a == TEXEL0_ALPHA);
            prog->usesT1 |= (dmux->decode[i].a == TEXEL1 || dmux->decode[i].a == TEXEL1_ALPHA);
            prog->usesCol |= (dmux->decode[i].a == SHADE || dmux->decode[i].a == SHADE_ALPHA);
            prog->usesNoise |= (dmux->decode[i].a == NOISE);
         }
      }
   }

   buffer += sprintf(buffer, "%s", _frag_header);
   if (prog->usesT0)
      buffer += sprintf(buffer, "lowp vec4 lTex0 = texture2D(uTex0, vTexCoord0); \n");
   if (prog->usesT1)
      buffer += sprintf(buffer, "lowp vec4 lTex1 = texture2D(uTex1, vTexCoord1); \n");
   if (prog->usesNoise)
      buffer += sprintf(buffer, "lowp vec4 lNoise = texture2D(uTexNoise, (1.0 / 1024.0) * gl_FragCoord.st); \n");

   for(i = 0; i < ((flags & SC_2CYCLE) ? 2 : 1); i++)
   {
      if ((dmux->flags&(1<<(i*2))) == 0)
      {
         buffer += sprintf(buffer, "lFragColor.rgb = (%s - %s) * %s + %s; \n",
               _color_param_str(dmux->decode[i*2].sa),
               _color_param_str(dmux->decode[i*2].sb),
               _color_param_str(dmux->decode[i*2].m),
               _color_param_str(dmux->decode[i*2].a)
               );
      }

      if ((dmux->flags&(1<<(i*2+1))) == 0)
      {
         buffer += sprintf(buffer, "lFragColor.a = (%s - %s) * %s + %s; \n",
               _alpha_param_str(dmux->decode[i*2+1].sa),
               _alpha_param_str(dmux->decode[i*2+1].sb),
               _alpha_param_str(dmux->decode[i*2+1].m),
               _alpha_param_str(dmux->decode[i*2+1].a)
               );
      }
      buffer += sprintf(buffer, "gl_FragColor = lFragColor; \n");
   };

   //fog
   if (flags&SC_FOGENABLED)
   {
      buffer += sprintf(buffer, "gl_FragColor = mix(gl_FragColor, uFogColor, vFactor); \n");
   }

   //alpha function
   if (flags&SC_ALPHAENABLED)
   {
      if (flags&SC_ALPHAGREATER)
         buffer += sprintf(buffer, "if (gl_FragColor.a < uAlphaRef) %s;\n", config.hackAlpha ? "gl_FragColor.a = 0" : "discard");
      else
         buffer += sprintf(buffer, "if (gl_FragColor.a <= uAlphaRef) %s;\n", config.hackAlpha ? "gl_FragColor.a = 0" : "discard");
   }
   buffer += sprintf(buffer, "} \n\n");
   *buffer = 0;

   prog->program = glCreateProgram();

   //Compile:

   src[0] = frag;
   len[0] = min(4096, strlen(frag));
   prog->fragment = glCreateShader(GL_FRAGMENT_SHADER);

   glShaderSource(prog->fragment, 1, (const char**) src, len);
   glCompileShader(prog->fragment);


   glGetShaderiv(prog->fragment, GL_COMPILE_STATUS, &success);
   if (!success)
      glcompiler_error(prog->fragment);

   //link
   locate_attributes(prog);
   glAttachShader(prog->program, prog->fragment);
   glAttachShader(prog->program, prog->vertex);
   glLinkProgram(prog->program);
   glGetProgramiv(prog->program, GL_LINK_STATUS, &success);
   if (!success)
      gllinker_error(prog->program);

   //remove fragment shader:
   glDeleteShader(prog->fragment);

   locate_uniforms(prog);
   return prog;
}

void Combiner_Set(u64 mux, int flags)
{
   DecodedMux *dmux;
   ShaderProgram *root, *prog;

   //determine flags
   if (flags == -1)
   {
      flags = 0;
      if ((gSP.geometryMode & G_FOG))
         flags |= SC_FOGENABLED;

      if ((gDP.otherMode.alphaCompare == G_AC_THRESHOLD) && !(gDP.otherMode.alphaCvgSel))
      {
         flags |= SC_ALPHAENABLED;
         if (gDP.blendColor.a > 0.0f)
            flags |= SC_ALPHAGREATER;
      }
      else if (gDP.otherMode.cvgXAlpha)
      {
         flags |= SC_ALPHAENABLED;
         flags |= SC_ALPHAGREATER;
      }

      if (gDP.otherMode.cycleType == G_CYC_2CYCLE)
         flags |= SC_2CYCLE;
   }

   dmux = (DecodedMux*)mux_new(mux, flags & SC_2CYCLE);

   //if already bound:
   if (scProgramCurrent)
   {
      if (program_compare(scProgramCurrent, dmux, flags))
      {
         scProgramChanged = 0;
         return;
      }
   }

   //traverse binary tree for cached programs
   scProgramChanged = 1;

   root = (ShaderProgram*)scProgramRoot;
   prog = (ShaderProgram*)root;
   while(!program_compare(prog, dmux, flags))
   {
      root = prog;
      if (prog->combine.mux < dmux->combine.mux)
         prog = prog->right;
      else
         prog = prog->left;
   }

   //build new program
   if (!prog)
   {
      scProgramCount++;
      prog = ShaderCombiner_Compile(dmux, flags);
      if (!root)
         scProgramRoot = prog;
      else if (root->combine.mux < dmux->combine.mux)
         root->right = prog;
      else
         root->left = prog;

   }

   prog->lastUsed = OGL.frame_dl;
   scProgramCurrent = prog;
   glUseProgram(prog->program);
   force_uniforms();

   if (dmux)
      free(dmux);
}

void ShaderCombiner_UpdateBlendColor(void)
{
   SC_SetUniform1f(uAlphaRef, (gDP.otherMode.cvgXAlpha) ? 0.5f : gDP.blendColor.a);
}

void ShaderCombiner_UpdateEnvColor(void)
{
   SC_SetUniform4fv(uEnvColor, &gDP.envColor.r);
}

void ShaderCombiner_UpdateFogColor(void)
{
   SC_SetUniform4fv(uFogColor, &gDP.fogColor.r );
}

void ShaderCombiner_UpdateConvertColor(void)
{
   SC_SetUniform1f(uK4, gDP.convert.k4);
   SC_SetUniform1f(uK5, gDP.convert.k5);
}

void ShaderCombiner_UpdatePrimColor(void)
{
   SC_SetUniform4fv(uPrimColor, &gDP.primColor.r);
   SC_SetUniform1f(uPrimLODFrac, gDP.primColor.l);
}

void ShaderCombiner_UpdateKeyColor(void)
{
}

void ShaderCombiner_UpdateLightParameters(void)
{
}
