/*
Copyright (C) 2002 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
/* Rice's combiner programs as text (RiceCombinerText.h). C89. */
#include <stdio.h>
#include <string.h>

#include "RiceCombinerText.h"

#define ALPHA_TEST "    if(gl_FragColor.a < AlphaRef) discard;                        \n"

static const char *fragmentHeader =
"#define saturate(x) clamp( x, 0.0, 1.0 )                   \n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#else
"precision lowp float;                                      \n"
#endif
"#ifdef NEED_TEX0                                           \n"
"uniform sampler2D uTex0;                                   \n"
"#endif                                                     \n"
"                                                           \n"
"#ifdef NEED_TEX1                                           \n"
"uniform sampler2D uTex1;                                   \n"
"#endif                                                     \n"
"                                                           \n"
"uniform vec4 EnvColor;                                     \n"
"uniform vec4 PrimColor;                                    \n"
"uniform vec4 EnvFrac;                                      \n"
"uniform vec4 PrimFrac;                                     \n"
"uniform float AlphaRef;                                    \n"
"uniform vec4 FogColor;                                     \n"
"                                                           \n"
"varying lowp vec4  vShadeColor;                            \n"
"varying mediump vec2  vTexCoord0;                          \n"
"varying lowp vec2  vTexCoord1;                             \n"
"varying lowp float vFog;                                   \n"
"                                                           \n"
"void main()                                                \n"
"{                                                          \n"
"    vec4 comb,comb2;                                       \n"
"                                                           \n"
"#ifdef NEED_TEX0                                           \n"
"    vec4 t0 = texture2D(uTex0,vTexCoord0);                 \n"
"#endif                                                     \n"
"                                                           \n"
"#ifdef NEED_TEX1                                           \n"
"    vec4 t1 = texture2D(uTex1,vTexCoord1);                 \n"
"#endif                                                     \n";

static const char *fragmentFooter =
"                                                           \n"
"#ifdef FOG                                                 \n"
"   gl_FragColor.rgb = mix(FogColor.rgb, comb.rgb, vFog);             \n"
"   gl_FragColor.a = comb.a;                                                \n"
"#else                                                                      \n"
"   gl_FragColor = comb;                                                    \n"
"#endif                                                                     \n"
"                                                                           \n"
"#ifdef ALPHA_TEST                                                          \n"
   ALPHA_TEST
"#endif                                                                     \n"
"}                                                                          \n";

/* color -- alpha -- alpha as a color, each MUX_ */
static const char *const muxToFP_Maps[][3] = {
{"vec3(0.0)", "0.0", "vec3(0.0)"},                          /* MUX_0 */
{"vec3(1.0)", "1.0", "vec3(1.0)"},                          /* MUX_1 */
{"comb.rgb", "comb.a", "vec3(comb.a)"},                     /* MUX_COMBINED */
{"t0.bgr", "t0.a", "vec3(t0.a)"},                           /* MUX_TEXEL0 */
{"t1.bgr", "t1.a", "vec3(t1.a)"},                           /* MUX_TEXEL1 */
{"PrimColor.rgb", "PrimColor.a", "vec3(PrimColor.a)"},      /* MUX_PRIM */
{"vShadeColor.rgb", "vShadeColor.a", "vec3(vShadeColor.a)"},/* MUX_SHADE */
{"EnvColor.rgb", "EnvColor.a", "vec3(EnvColor.a)"},         /* MUX_ENV */
{"comb.rgb", "comb.a", "vec3(comb.a)"},                     /* MUX_COMBALPHA */
{"t0.bgr", "t0.a", "vec3(t0.a)"},                           /* MUX_T0_ALPHA */
{"t1.bgr", "t1.a", "vec3(t1.a)"},                           /* MUX_T1_ALPHA */
{"PrimColor.rgb", "PrimColor.a", "vec3(PrimColor.a)"},      /* MUX_PRIM_ALPHA */
{"vShadeColor.rgb", "vShadeColor.a", "vec3(vShadeColor.a)"},/* MUX_SHADE_ALPHA */
{"EnvColor.rgb", "EnvColor.a", "vec3(EnvColor.a)"},         /* MUX_ENV_ALPHA */
{"vec3(EnvFrac.a)", "EnvFrac.a", "vec3(EnvFrac.a)"},        /* MUX_LODFRAC */
{"vec3(PrimFrac.a)", "PrimFrac.a", "vec3(PrimFrac.a)"},     /* MUX_PRIMLODFRAC */
{"vec3(1.0)", "1.0", "vec3(1.0)"},                          /* MUX_K5 */
{"vec3(1.0)", "1.0", "vec3(1.0)"},                          /* MUX_UNK, not used */
};

/* for the colour channel */
static const char *MuxToOC(uint8_t val)
{
    if (val & MUX_ALPHAREPLICATE)
        return muxToFP_Maps[val & 0x1F][2];
    return muxToFP_Maps[val & 0x1F][0];
}

/* for the alpha channel */
static const char *MuxToOA(uint8_t val)
{
    return muxToFP_Maps[val & 0x1F][1];
}

static void CheckFpVars(uint8_t MuxVar, bool *bNeedT0, bool *bNeedT1)
{
    MuxVar &= 0x1f;
    if (MuxVar == MUX_TEXEL0 || MuxVar == MUX_T0_ALPHA)
        *bNeedT0 = true;
    if (MuxVar == MUX_TEXEL1 || MuxVar == MUX_T1_ALPHA)
        *bNeedT1 = true;
}

void RiceCombiner_ProgramText(DecodedMux *mux, char *out, bool *needT0, bool *needT1)
{
    char tempstr[500], newFPBody[RICE_COMBINER_TEXT_SIZE];
    bool bNeedT0 = false, bNeedT1 = false;
    int cycle, channel;

    mux->splitType[0] = mux->splitType[1] = mux->splitType[2] = mux->splitType[3] = CM_FMT_TYPE_NOT_CHECKED;
    DecodedMux_Reformat(mux, false);

    newFPBody[0] = 0;

    for (cycle = 0; cycle < 2; cycle++)
    {
        for (channel = 0; channel < 2; channel++)
        {
            const char *(*func)(uint8_t) = channel == 0 ? MuxToOC : MuxToOA;
            const char *dst = channel == 0 ? "rgb" : "a";
            const N64CombinerType *m = &mux->m_n64Combiners[cycle*2+channel];
            switch (mux->splitType[cycle*2+channel])
            {
            case CM_FMT_TYPE_NOT_USED:
                tempstr[0] = 0;
                break;
            case CM_FMT_TYPE_D:
                sprintf(tempstr, "comb.%s = %s;\n", dst, func(m->d));
                CheckFpVars(m->d, &bNeedT0, &bNeedT1);
                break;
            case CM_FMT_TYPE_A_MOD_C:
                sprintf(tempstr, "comb.%s = %s * %s;\n", dst, func(m->a), func(m->c));
                CheckFpVars(m->a, &bNeedT0, &bNeedT1);
                CheckFpVars(m->c, &bNeedT0, &bNeedT1);
                break;
            case CM_FMT_TYPE_A_ADD_D:
                sprintf(tempstr, "comb.%s = saturate(%s + %s);\n", dst, func(m->a), func(m->d));
                CheckFpVars(m->a, &bNeedT0, &bNeedT1);
                CheckFpVars(m->d, &bNeedT0, &bNeedT1);
                break;
            case CM_FMT_TYPE_A_SUB_B:
                sprintf(tempstr, "comb.%s = %s - %s;\n", dst, func(m->a), func(m->b));
                CheckFpVars(m->a, &bNeedT0, &bNeedT1);
                CheckFpVars(m->b, &bNeedT0, &bNeedT1);
                break;
            case CM_FMT_TYPE_A_MOD_C_ADD_D:
                sprintf(tempstr, "comb.%s = saturate(%s * %s + %s);\n", dst, func(m->a), func(m->c), func(m->d));
                CheckFpVars(m->a, &bNeedT0, &bNeedT1);
                CheckFpVars(m->c, &bNeedT0, &bNeedT1);
                CheckFpVars(m->d, &bNeedT0, &bNeedT1);
                break;
            case CM_FMT_TYPE_A_LERP_B_C:
                /* mix() orders its arguments differently from a lerp */
                sprintf(tempstr, "comb.%s = (%s - %s) * %s + %s;\n", dst, func(m->a), func(m->b), func(m->c), func(m->b));
                CheckFpVars(m->a, &bNeedT0, &bNeedT1);
                CheckFpVars(m->b, &bNeedT0, &bNeedT1);
                CheckFpVars(m->c, &bNeedT0, &bNeedT1);
                break;
            default:
                sprintf(tempstr, "comb2.%s = %s - %s;\ncomb.%s = saturate(comb2.%s * %s + %s);\n", dst, func(m->a), func(m->b), dst, dst, func(m->c), func(m->d));
                CheckFpVars(m->a, &bNeedT0, &bNeedT1);
                CheckFpVars(m->b, &bNeedT0, &bNeedT1);
                CheckFpVars(m->c, &bNeedT0, &bNeedT1);
                CheckFpVars(m->d, &bNeedT0, &bNeedT1);
                break;
            }
            strcat(newFPBody, tempstr);
        }
    }

    out[0] = 0;
    if (bNeedT0)
        strcat(out, "#define NEED_TEX0\n");
    if (bNeedT1)
        strcat(out, "#define NEED_TEX1\n");
    strcat(out, fragmentHeader);
    strcat(out, newFPBody);
    strcat(out, fragmentFooter);
    if (needT0)
        *needT0 = bNeedT0;
    if (needT1)
        *needT1 = bNeedT1;
}
