/*
Copyright (C) 2003 Rice1964

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

#include <stdlib.h>
#include "../../mupen64plus-video-gliden64/src/CombinerKeyJournal.h"
#include "../../mupen64plus-video-gliden64/src/CombinerKeyPath.h"
#include "OGLES2FragmentShaders.h"
#include "OGLRender.h"
#include "OGLTexture.h"

#define ALPHA_TEST "    if(gl_FragColor.a < AlphaRef) discard;                        \n"

GLuint vertexProgram = 9999;
const char *vertexShader =
"#version " GLSL_VERSION "\n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#endif
"attribute mediump vec4 aPosition;                          \n"\
"attribute lowp vec4    aColor;                             \n"\
"attribute lowp vec2    aTexCoord0;                         \n"\
"attribute lowp vec2    aTexCoord1;                         \n"\
"attribute lowp vec2    aAtlasTransform;                    \n"\
"attribute mediump float aFogCoord;                         \n"\
"                                                           \n"\
"uniform vec2 FogMinMax;                                    \n"\
"                                                           \n"\
"varying lowp float vFactor;                                \n"\
"varying lowp vec4  vShadeColor;                            \n"\
"varying mediump vec2 vTexCoord0;                           \n"\
"varying lowp vec2    vTexCoord1;                           \n"\
"varying lowp float   vFog;                                 \n"\
"                                                           \n"\
"void main()                                                \n"\
"{                                                          \n"\
"    gl_Position = aPosition;                               \n"\
"    vShadeColor = aColor;                                  \n"\
"    vTexCoord0 = aTexCoord0;                               \n"\
"    vTexCoord1 = aTexCoord1;                               \n"\
"    vFog = (FogMinMax[1] - aFogCoord) / (FogMinMax[1] - FogMinMax[0]); \n"\
"    vFog = clamp(vFog, 0.0, 1.0);                          \n"\
"}                                                          \n"\
"                                                           \n";

const char *fragmentHeader =
"#define saturate(x) clamp( x, 0.0, 1.0 )                   \n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#else
"precision lowp float;                                      \n"
#endif
"#ifdef NEED_TEX0                                           \n"\
"uniform sampler2D uTex0;                                   \n"\
"#endif                                                     \n"\
"                                                           \n"\
"#ifdef NEED_TEX1                                           \n"\
"uniform sampler2D uTex1;                                   \n"\
"#endif                                                     \n"\
"                                                           \n"\
"uniform vec4 EnvColor;                                     \n"\
"uniform vec4 PrimColor;                                    \n"\
"uniform vec4 EnvFrac;                                      \n"\
"uniform vec4 PrimFrac;                                     \n"\
"uniform float AlphaRef;                                    \n"\
"uniform vec4 FogColor;                                     \n"\
"                                                           \n"\
"varying lowp vec4  vShadeColor;                            \n"\
"varying mediump vec2  vTexCoord0;                          \n"\
"varying lowp vec2  vTexCoord1;                             \n"\
"varying lowp float vFog;                                   \n"\
"                                                           \n"\
"void main()                                                \n"\
"{                                                          \n"\
"    vec4 comb,comb2;                                       \n"\
"                                                           \n"\
"#ifdef NEED_TEX0                                           \n"\
"    vec4 t0 = texture2D(uTex0,vTexCoord0);                 \n"\
"#endif                                                     \n"\
"                                                           \n"\
"#ifdef NEED_TEX1                                           \n"\
"    vec4 t1 = texture2D(uTex1,vTexCoord1);                 \n"\
"#endif                                                     \n";

const char *fragmentFooter =
"                                                           \n"\
"#ifdef FOG                                                 \n"\
"   gl_FragColor.rgb = mix(FogColor.rgb, comb.rgb, vFog);             \n"\
"   gl_FragColor.a = comb.a;                                                \n"\
"#else                                                                      \n"\
"   gl_FragColor = comb;                                                    \n"\
"#endif                                                                     \n"\
"                                                                           \n"\
"#ifdef ALPHA_TEST                                                          \n"\
   ALPHA_TEST                                                                  
"#endif                                                                     \n"\
"}                                                                          \n";

//Fragment shader for InitCycleCopy
const char *fragmentCopy =
"#version " GLSL_VERSION "\n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#else
"precision lowp float;                                      \n"
#endif
"uniform sampler2D uTex0;                                   \n"\
"uniform float AlphaRef;                                    \n"\
"varying lowp vec2 vTexCoord0;                              \n"\
"void main()                                                \n"\
"{                                                          \n"\
"   gl_FragColor = texture2D(uTex0,vTexCoord0).bgra;        \n" \
ALPHA_TEST
"}";

GLuint copyProgram,copyAlphaLocation;

//Fragment shader for InitCycleFill
const char *fragmentFill =
"#version " GLSL_VERSION "\n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#else
"precision lowp float;                                      \n"
#endif
"uniform vec4 uColor;                                       \n"
"void main()                                                \n"
"{                                                          \n"
"   gl_FragColor = uColor;                                  \n"
"}";

GLuint fillProgram,fillColorLocation;

COGLFragmentShaderCombiner::COGLFragmentShaderCombiner(CRender *pRender)
: COGLColorCombiner(pRender)
{
    m_bShaderIsSupported = true;
}
COGLFragmentShaderCombiner::~COGLFragmentShaderCombiner()
{
}

bool COGLFragmentShaderCombiner::Initialize(void)
{
    if( !COGLColorCombiner::Initialize() )
        return false;

    m_bShaderIsSupported = true;

    return true;
}

void COGLFragmentShaderCombiner::InitCombinerCycle12(void)
{
}
void COGLFragmentShaderCombiner::DisableCombiner(void)
{
    COGLColorCombiner::DisableCombiner();
}

void COGLFragmentShaderCombiner::InitCombinerCycleCopy(void)
{
    COGLColorCombiner::InitCombinerCycleCopy();
}

void COGLFragmentShaderCombiner::InitCombinerCycleFill(void)
{
    COGLColorCombiner::InitCombinerCycleFill();
}
void COGLFragmentShaderCombiner::InitCombinerBlenderForSimpleTextureDraw(uint32_t tile)
{
    COGLColorCombiner::InitCombinerBlenderForSimpleTextureDraw(tile);
}

#ifdef DEBUGGER
void COGLFragmentShaderCombiner::DisplaySimpleMuxString(void)
{
    COGLColorCombiner::DisplaySimpleMuxString();
}
#endif



COGL_FragmentProgramCombiner::COGL_FragmentProgramCombiner(CRender *pRender)
: COGLColorCombiner4(pRender)
{
    delete m_pDecodedMux;
    m_pDecodedMux = new DecodedMuxForPixelShader;
    m_journal = NULL;
    m_bFragmentProgramIsSupported = true;
    m_AlphaRef = 0.0f;
    bAlphaTestState = false;
    bAlphaTestPreviousState = false;
    bFogState = false;
    bFogPreviousState = false;

    //Create shaders for fill and copy
    GLint success;
    GLuint vs,fs;
    copyProgram = glCreateProgram();
    vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs,1,&vertexShader,NULL);
    glCompileShader(vs);

    glGetShaderiv(vs,GL_COMPILE_STATUS,&success);
    if(!success)
    {
        char log[1024];
        glGetShaderInfoLog(vs,1024,NULL,log);
        printf("%s\n",log);
    }

    fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs,1,&fragmentCopy,NULL);
    glCompileShader(fs);

    glGetShaderiv(fs,GL_COMPILE_STATUS,&success);
    if(!success)
    {
        char log[1024];
        glGetShaderInfoLog(fs,1024,NULL,log);
        printf("%s\n",log);
    }

    glAttachShader(copyProgram,vs);
    glAttachShader(copyProgram,fs);

    glBindAttribLocation(copyProgram,VS_TEXCOORD0,"aTexCoord0");
    glBindAttribLocation(copyProgram,VS_POSITION,"aPosition");

    glLinkProgram(copyProgram);
    copyAlphaLocation = glGetUniformLocation(copyProgram,"AlphaRef");
    glGetProgramiv(copyProgram,GL_LINK_STATUS,&success);
    if(!success)
    {
        char log[1024];
        glGetProgramInfoLog(copyProgram,1024,NULL,log);
        printf("%s\n",log);
    }

    glDeleteShader(fs);

    //Fill shader
    fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs,1,&fragmentFill,NULL);
    glCompileShader(fs);

    glGetShaderiv(fs,GL_COMPILE_STATUS,&success);
    if(!success)
    {
        char log[1024];
        glGetShaderInfoLog(fs,1024,NULL,log);
        printf("%s\n",log);
    }

    fillProgram = glCreateProgram();
    glAttachShader(fillProgram,vs);
    glAttachShader(fillProgram,fs);

    glBindAttribLocation(fillProgram,VS_POSITION,"aPosition");

    glLinkProgram(fillProgram);


    fillColorLocation = glGetUniformLocation(fillProgram,"uColor");

    glDeleteShader(fs);
    glDeleteShader(vs);
}
COGL_FragmentProgramCombiner::~COGL_FragmentProgramCombiner()
{
    combiner_journal_close(m_journal);
    m_journal = NULL;

    int size = m_vCompiledShaders.size();
    for (int i=0; i<size; i++)
    {
        GLuint ID = m_vCompiledShaders[i].programID;
        glDeleteProgram(ID);

        m_vCompiledShaders[i].programID = 0;
    }

    m_vCompiledShaders.clear();
}

bool COGL_FragmentProgramCombiner::Initialize(void)
{
    m_bFragmentProgramIsSupported = true;

    BuildKnownKeys();

    return true;
}

void COGL_FragmentProgramCombiner::UseProgram(GLuint program)
{
    if (program != currentProgram) {
        glUseProgram(program);
        currentProgram = program;
    }
}


void COGL_FragmentProgramCombiner::DisableCombiner(void)
{
}

void COGL_FragmentProgramCombiner::InitCombinerCycleCopy(void)
{
    OGLRenderer()->DisableMultiTexture();
    OGLRenderer()->EnableTexUnit(0, true);
    UseProgram(copyProgram);
    glUniform1f(copyAlphaLocation,m_AlphaRef);
    glEnableVertexAttribArray(VS_POSITION);
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glDisableVertexAttribArray(VS_COLOR);
    glDisableVertexAttribArray(VS_TEXCOORD1);
    glDisableVertexAttribArray(VS_FOG);
    COGLTexture* pTexture = g_textures[gRSP.curTile].m_pCOGLTexture;
    if( pTexture )
    {
        OGLRenderer()->BindTexture(pTexture->m_dwTextureName, 0);
        OGLRenderer()->SetTexelRepeatFlags(gRSP.curTile);
    }
}

/* DrawFrameBuffer puts a frame the CPU wrote into RDRAM on screen through
 * this. The base version only sets up fixed-function state, which leaves
 * no program bound: the draw then runs with program 0, which is undefined
 * in a core context and crashes Mesa. Sample the frame with the copy
 * program instead, with nothing discarded. */
void COGL_FragmentProgramCombiner::InitCombinerBlenderForSimpleTextureDraw(uint32_t tile)
{
    COGLColorCombiner::InitCombinerBlenderForSimpleTextureDraw(tile);
    UseProgram(copyProgram);
    glUniform1f(copyAlphaLocation, 0.0f);
}

void COGL_FragmentProgramCombiner::InitCombinerCycleFill(void)
{
    UseProgram(fillProgram);
    glUniform4f(fillColorLocation,((gRDP.fillColor>>16)&0xFF)/255.0f,((gRDP.fillColor>>8)&0xFF)/255.0f,((gRDP.fillColor)&0xFF)/255.0f,((gRDP.fillColor>>24)&0xFF)/255.0f);
}

#ifdef BGR_SHADER
const char *muxToFP_Maps[][3] = {
//color -- alpha -- alpha as a color
{"vec3(0.0)", "0.0", "vec3(0.0)"},                      //MUX_0 = 0,
{"vec3(1.0)", "1.0", "vec3(1.0)"},                      //MUX_1,
{"comb.rgb", "comb.a", "vec3(comb.a)"},                    //MUX_COMBINED,
{"t0.rgb", "t0.a", "vec3(t0.a)"},                        //MUX_TEXEL0,
{"t1.rgb", "t1.a", "vec3(t1.a)"},                        //MUX_TEXEL1,
{"PrimColor.rgb", "PrimColor.a", "vec3(PrimColor.a)"},          //MUX_PRIM,
{"vShadeColor.rgb", "vShadeColor.a", "vec3(vShadeColor.a)"},      //MUX_SHADE,
{"EnvColor.rgb", "EnvColor.a", "vec3(EnvColor.a)"},            //MUX_ENV,
{"comb.rgb", "comb.a", "vec3(comb.a)"},                    //MUX_COMBALPHA,
{"t0.rgb", "t0.a", "vec3(t0.a)"},                        //MUX_T0_ALPHA,
{"t1.rgb", "t1.a", "vec3(t1.a)"},                        //MUX_T1_ALPHA,
{"PrimColor.rgb", "PrimColor.a", "vec3(PrimColor.a)"},          //MUX_PRIM_ALPHA,
{"vShadeColor.rgb", "vShadeColor.a", "vec3(vShadeColor.a)"},      //MUX_SHADE_ALPHA,
{"EnvColor.rgb", "EnvColor.a", "vec3(EnvColor.a)"},            //MUX_ENV_ALPHA,
{"vec3(EnvFrac.a)", "EnvFrac.a", "vec3(EnvFrac.a)"},                //MUX_LODFRAC,
{"vec3(PrimFrac.a)", "PrimFrac.a", "vec3(PrimFrac.a)"},              //MUX_PRIMLODFRAC,
{"vec3(1.0)", "1.0", "vec3(1.0)"},                      //MUX_K5,
{"vec3(1.0)", "1.0", "vec3(1.0)"},                      //MUX_UNK,  // Should not be used
};
#else
const char *muxToFP_Maps[][3] = {
//color -- alpha -- alpha as a color
{"vec3(0.0)", "0.0", "vec3(0.0)"},                      //MUX_0 = 0,
{"vec3(1.0)", "1.0", "vec3(1.0)"},                      //MUX_1,
{"comb.rgb", "comb.a", "vec3(comb.a)"},                    //MUX_COMBINED,
{"t0.bgr", "t0.a", "vec3(t0.a)"},                        //MUX_TEXEL0,
{"t1.bgr", "t1.a", "vec3(t1.a)"},                        //MUX_TEXEL1,
{"PrimColor.rgb", "PrimColor.a", "vec3(PrimColor.a)"},          //MUX_PRIM,
{"vShadeColor.rgb", "vShadeColor.a", "vec3(vShadeColor.a)"},      //MUX_SHADE,
{"EnvColor.rgb", "EnvColor.a", "vec3(EnvColor.a)"},            //MUX_ENV,
{"comb.rgb", "comb.a", "vec3(comb.a)"},                    //MUX_COMBALPHA,
{"t0.bgr", "t0.a", "vec3(t0.a)"},                        //MUX_T0_ALPHA,
{"t1.bgr", "t1.a", "vec3(t1.a)"},                        //MUX_T1_ALPHA,
{"PrimColor.rgb", "PrimColor.a", "vec3(PrimColor.a)"},          //MUX_PRIM_ALPHA,
{"vShadeColor.rgb", "vShadeColor.a", "vec3(vShadeColor.a)"},      //MUX_SHADE_ALPHA,
{"EnvColor.rgb", "EnvColor.a", "vec3(EnvColor.a)"},            //MUX_ENV_ALPHA,
{"vec3(EnvFrac.a)", "EnvFrac.a", "vec3(EnvFrac.a)"},                //MUX_LODFRAC,
{"vec3(PrimFrac.a)", "PrimFrac.a", "vec3(PrimFrac.a)"},              //MUX_PRIMLODFRAC,
{"vec3(1.0)", "1.0", "vec3(1.0)"},                      //MUX_K5,
{"vec3(1.0)", "1.0", "vec3(1.0)"},                      //MUX_UNK,  // Should not be used
};
#endif


char oglNewFP[4092];

char* MuxToOC(uint8_t val)
{
    // For color channel
    if( val&MUX_ALPHAREPLICATE )
        return (char*)muxToFP_Maps[val&0x1F][2];
    else
        return (char*)muxToFP_Maps[val&0x1F][0];
}

char* MuxToOA(uint8_t val)
{
    // For alpha channel
    return (char*)muxToFP_Maps[val&0x1F][1];
}

#define COMBINER_JOURNAL_VERSION 1u

static void CheckFpVars(uint8_t MuxVar, bool &bNeedT0, bool &bNeedT1)
{
    MuxVar &= 0x1f;
    if (MuxVar == MUX_TEXEL0 || MuxVar == MUX_T0_ALPHA)
        bNeedT0 = true;
    if (MuxVar == MUX_TEXEL1 || MuxVar == MUX_T1_ALPHA)
        bNeedT1 = true;
}

void COGL_FragmentProgramCombiner::GenerateProgramStr()
{
    DecodedMuxForPixelShader &mux = *(DecodedMuxForPixelShader*)m_pDecodedMux;

    mux.splitType[0] = mux.splitType[1] = mux.splitType[2] = mux.splitType[3] = CM_FMT_TYPE_NOT_CHECKED;
    m_pDecodedMux->Reformat(false);

    char tempstr[500], newFPBody[4092];
    bool bNeedT0 = false, bNeedT1 = false, bNeedComb2 = false;
    newFPBody[0] = 0;

    for( int cycle=0; cycle<2; cycle++ )
    {
        for( int channel=0; channel<2; channel++)
        {
            char* (*func)(uint8_t) = channel==0?MuxToOC:MuxToOA;
            char *dst = channel==0?(char*)"rgb":(char*)"a";
            N64CombinerType &m = mux.m_n64Combiners[cycle*2+channel];
            switch( mux.splitType[cycle*2+channel] )
            {
            case CM_FMT_TYPE_NOT_USED:
                tempstr[0] = 0;
                break;
            case CM_FMT_TYPE_D:
                sprintf(tempstr, "comb.%s = %s;\n", dst, func(m.d));
                CheckFpVars(m.d, bNeedT0, bNeedT1);
                break;
            case CM_FMT_TYPE_A_MOD_C:
                sprintf(tempstr, "comb.%s = %s * %s;\n", dst, func(m.a), func(m.c));
                CheckFpVars(m.a, bNeedT0, bNeedT1);
                CheckFpVars(m.c, bNeedT0, bNeedT1);
                break;
            case CM_FMT_TYPE_A_ADD_D:
                sprintf(tempstr, "comb.%s = saturate(%s + %s);\n", dst, func(m.a), func(m.d));
                CheckFpVars(m.a, bNeedT0, bNeedT1);
                CheckFpVars(m.d, bNeedT0, bNeedT1);
                break;
            case CM_FMT_TYPE_A_SUB_B:
                sprintf(tempstr, "comb.%s = %s - %s;\n", dst, func(m.a), func(m.b));
                CheckFpVars(m.a, bNeedT0, bNeedT1);
                CheckFpVars(m.b, bNeedT0, bNeedT1);
                break;
            case CM_FMT_TYPE_A_MOD_C_ADD_D:
                sprintf(tempstr, "comb.%s = saturate(%s * %s + %s);\n", dst, func(m.a), func(m.c),func(m.d));
                CheckFpVars(m.a, bNeedT0, bNeedT1);
                CheckFpVars(m.c, bNeedT0, bNeedT1);
                CheckFpVars(m.d, bNeedT0, bNeedT1);
                break;
            case CM_FMT_TYPE_A_LERP_B_C:
                //ARB ASM LERP and mix have different parameter ordering.
                //sprintf(tempstr, "comb.%s = saturate(mix(%s, %s, %s));\n", dst,func(m.a),func(m.b), func(m.c));
                sprintf(tempstr, "comb.%s = (%s - %s) * %s + %s;\n", dst,func(m.a),func(m.b), func(m.c),func(m.b));
                CheckFpVars(m.a, bNeedT0, bNeedT1);
                CheckFpVars(m.b, bNeedT0, bNeedT1);
                CheckFpVars(m.c, bNeedT0, bNeedT1);
                //sprintf(tempstr, "SUB comb.%s, %s, %s;\nMAD_SAT comb.%s, comb, %s, %s;\n", dst, func(m.a), func(m.b), dst, func(m.c), func(m.b));
                break;
            default:
                sprintf(tempstr, "comb2.%s = %s - %s;\ncomb.%s = saturate(comb2.%s * %s + %s);\n", dst, func(m.a), func(m.b),  dst,dst, func(m.c),func(m.d));
                CheckFpVars(m.a, bNeedT0, bNeedT1);
                CheckFpVars(m.b, bNeedT0, bNeedT1);
                CheckFpVars(m.c, bNeedT0, bNeedT1);
                CheckFpVars(m.d, bNeedT0, bNeedT1);
                bNeedComb2 = true;
                break;
            }
            strcat(newFPBody, tempstr);
        }
    }

    oglNewFP[0] = 0;
    if (bNeedT0)
        strcat(oglNewFP, "#define NEED_TEX0\n");
    if (bNeedT1)
        strcat(oglNewFP, "#define NEED_TEX1\n");
    strcat(oglNewFP, fragmentHeader);
    strcat(oglNewFP, newFPBody);
    strcat(oglNewFP, fragmentFooter);

}

/* Builds one program, for the fog and alpha test it is asked for. */
int COGL_FragmentProgramCombiner::ParseDecodedMux()
{
    OGLShaderCombinerSaveType res;
    GLint success;

    if(vertexProgram == 9999)
    {
        vertexProgram = res.vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(res.vertexShaderID, 1, &vertexShader,NULL);
        glCompileShader(res.vertexShaderID);
    }
    else
    {
        res.vertexShaderID = vertexProgram;
    }


    GenerateProgramStr();

    {
       {
          const int alphaTest = bAlphaTestState ? 1 : 0;
          const int fog = bFogState ? 1 : 0;
          char tmpShader[4096];
          const char *shaderSource = tmpShader;

          res.fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

          strcpy(tmpShader,"#version " GLSL_VERSION "\n");

          if(alphaTest == 1)
             strcat(tmpShader,"#define ALPHA_TEST\n");

          if(fog == 1)
             strcat(tmpShader,"#define FOG\n");

          res.fogIsUsed = fog == 1;
          res.alphaTest = alphaTest == 1;
          strcat(tmpShader,oglNewFP);

          glShaderSource(res.fragmentShaderID, 1, &shaderSource, NULL);

          glCompileShader(res.fragmentShaderID);

          glGetShaderiv(res.fragmentShaderID, GL_COMPILE_STATUS, &success);
          if (!success)
          {
             char Log[1024];
             GLint nLength;
             glGetShaderInfoLog(res.fragmentShaderID, 1024, &nLength, Log);
             printf("Error compiling shader!\n %s",oglNewFP);
             printf("%s", Log);
          }

          res.programID = glCreateProgram();
          glAttachShader(res.programID,res.vertexShaderID);
          glAttachShader(res.programID,res.fragmentShaderID);

          //Bind Attributes
          glBindAttribLocation(res.programID,VS_COLOR,"aColor");
          glBindAttribLocation(res.programID,VS_TEXCOORD0,"aTexCoord0");
          glBindAttribLocation(res.programID,VS_TEXCOORD1,"aTexCoord1");
          glBindAttribLocation(res.programID,VS_POSITION,"aPosition");
          glBindAttribLocation(res.programID,VS_FOG,"aFogCoord");

          glLinkProgram(res.programID);

          glGetProgramiv(res.programID, GL_LINK_STATUS, &success);
          if (!success)
          {
             char Log[1024];
             GLint nLength;
             glGetShaderInfoLog(res.fragmentShaderID, 1024, &nLength, Log);
             printf("Error linking program!\n");
             printf("%s\n",Log);
          }

          UseProgram(res.programID);

          //Bind texture samplers
          GLint tex0 = glGetUniformLocation(res.programID,"uTex0");
          GLint tex1 = glGetUniformLocation(res.programID,"uTex1");

          if(tex0 != -1)
             glUniform1i(tex0,0);
          if(tex1 != -1)
             glUniform1i(tex1,1);

          //Bind Uniforms
          res.PrimColorLocation = glGetUniformLocation(res.programID,"PrimColor");
          res.EnvColorLocation = glGetUniformLocation(res.programID,"EnvColor");
          res.PrimFracLocation = glGetUniformLocation(res.programID,"PrimFrac");
          res.EnvFracLocation = glGetUniformLocation(res.programID,"EnvFrac");
          res.AlphaRefLocation = glGetUniformLocation(res.programID,"AlphaRef");
          res.FogColorLocation = glGetUniformLocation(res.programID,"FogColor");
          res.FogMinMaxLocation = glGetUniformLocation(res.programID,"FogMinMax");

          res.dwMux0 = m_pDecodedMux->m_dwMux0;
          res.dwMux1 = m_pDecodedMux->m_dwMux1;

          m_vCompiledShaders.push_back(res);
       }
    }
    m_lastIndex = (int)m_vCompiledShaders.size()-1;

    return m_lastIndex;
}

/* Each key an earlier session met, built before the first draw. */
void COGL_FragmentProgramCombiner::PrebuildKey(void *ctx, uint64_t key, unsigned flags)
{
    COGL_FragmentProgramCombiner *self = (COGL_FragmentProgramCombiner*)ctx;

    self->bAlphaTestState = (flags & 1) != 0;
    self->bFogState       = (flags & 2) != 0;
    self->UpdateCombiner((uint32_t)key, (uint32_t)(key >> 32));
    if (self->FindCompiledMux() < 0)
        self->ParseDecodedMux();
}

void COGL_FragmentProgramCombiner::BuildKnownKeys()
{
    char path[PATH_MAX_LENGTH];
    const bool alpha = bAlphaTestState;
    const bool fog   = bFogState;

    if (m_journal)
        return;
    combiner_key_path(path, sizeof(path), "rice", gfx_info.HEADER);
    combiner_journal_read(path, COMBINER_JOURNAL_VERSION, PrebuildKey, this);
    /* the next draw decodes its own mux, with the game's own state */
    m_pDecodedMux->m_dwMux0 = m_pDecodedMux->m_dwMux1 = 0;
    bAlphaTestState = alpha;
    bFogState       = fog;
    m_journal = combiner_journal_open(path, COMBINER_JOURNAL_VERSION);
}

void COGL_FragmentProgramCombiner::GenerateCombinerSetting(int index)
{
    GLuint ID = m_vCompiledShaders[index].programID;

    UseProgram(ID);
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][0]));

    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[0].u));

    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[1].u));

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE,GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]) );

    glEnableVertexAttribArray(VS_FOG);
    glVertexAttribPointer(VS_FOG, 1, GL_FLOAT,GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][4]) );
}

void COGL_FragmentProgramCombiner::GenerateCombinerSettingConstants(int index)
{
    OGLShaderCombinerSaveType &prog = m_vCompiledShaders[index];

    UseProgram(prog.programID);
    float *pf;
    if(prog.EnvColorLocation != -1)
    {
        pf = GetEnvColorfv();
        if (memcmp(pf, prog.EnvColors, sizeof(prog.EnvColors))) {
            memcpy(prog.EnvColors, pf, sizeof(prog.EnvColors));
            glUniform4fv(prog.EnvColorLocation, 1, pf);
        }
    }

    if(prog.PrimColorLocation != -1)
    {
        pf = GetPrimitiveColorfv();
        if (memcmp(pf, prog.PrimColors, sizeof(prog.PrimColors))) {
            memcpy(prog.PrimColors, pf, sizeof(prog.PrimColors));
            glUniform4fv(prog.PrimColorLocation, 1, pf);
        }
    }

    if(prog.EnvFracLocation != -1)
    {
        // avoid slow float compare..
        if( *(int *)&gRDP.LODFrac != *(int *)&prog.EnvLODFrac ) {
            float frac = gRDP.LODFrac / 255.0f;
            float tempf[4] = {frac,frac,frac,frac};

            prog.EnvLODFrac = (float)gRDP.LODFrac;
            glUniform4fv(prog.EnvFracLocation, 1, tempf);
        }
    }

    if(prog.PrimFracLocation != -1)
    {
        if( *(int *)&gRDP.primLODFrac != *(int *)&prog.PrimLODFrac ) {
            float frac2 = gRDP.primLODFrac / 255.0f;
            float tempf2[4] = {frac2,frac2,frac2,frac2};

            prog.PrimLODFrac = (float)gRDP.primLODFrac;
            glUniform4fv(prog.PrimFracLocation, 1, tempf2);
        }
    }

    if(prog.FogColorLocation != -1)
    {
        pf = &gRDP.fvFogColor[0];
        if (memcmp(pf, prog.FogColors, sizeof(prog.FogColors))) {
            memcpy(prog.FogColors, pf, sizeof(prog.FogColors));
            glUniform4fv(prog.FogColorLocation, 1, pf);
        }
    }

    if(prog.FogMinMaxLocation != -1)
    {
        if( gRSPfFogMin != prog.FogMin || gRSPfFogMax != prog.FogMax ) {
            prog.FogMin = gRSPfFogMin;
            prog.FogMax = gRSPfFogMax;
            glUniform2f(prog.FogMinMaxLocation,gRSPfFogMin,gRSPfFogMax);
        }
    }

    if(prog.AlphaRefLocation != -1)
    {
        if( m_AlphaRef != prog.AlphaRef ) {
            prog.AlphaRef = m_AlphaRef;
            glUniform1f(prog.AlphaRefLocation, m_AlphaRef);
        }
    }
}

int COGL_FragmentProgramCombiner::FindCompiledMux()
{
#ifdef DEBUGGER
    if( debuggerDropCombiners )
    {
        m_vCompiledShaders.clear();
        //m_dwLastMux0 = m_dwLastMux1 = 0;
        debuggerDropCombiners = false;
    }
#endif
    for( uint32_t i=0; i<m_vCompiledShaders.size(); i++ )
    {
        if( m_vCompiledShaders[i].dwMux0 == m_pDecodedMux->m_dwMux0 
            && m_vCompiledShaders[i].dwMux1 == m_pDecodedMux->m_dwMux1 
            && m_vCompiledShaders[i].fogIsUsed == bFogState
            && m_vCompiledShaders[i].alphaTest == bAlphaTestState)
        {
            return (int)i;
        }
    }

    return -1;
}

//////////////////////////////////////////////////////////////////////////
void COGL_FragmentProgramCombiner::InitCombinerCycle12(void)
{
#ifdef DEBUGGER
    if( debuggerDropCombiners )
    {
        UpdateCombiner(m_pDecodedMux->m_dwMux0,m_pDecodedMux->m_dwMux1);
        m_vCompiledShaders.clear();
        m_dwLastMux0 = m_dwLastMux1 = 0;
        debuggerDropCombiners = false;
    }
#endif

    OGLRenderer()->EnableMultiTexture();

    bool combinerIsChanged = false;

    if( m_pDecodedMux->m_dwMux0 != m_dwLastMux0 || m_pDecodedMux->m_dwMux1 != m_dwLastMux1
          ||  bAlphaTestState != bAlphaTestPreviousState || bFogState != bFogPreviousState || m_lastIndex < 0 )
    {
        combinerIsChanged = true;
        m_lastIndex = FindCompiledMux();
        if( m_lastIndex < 0 )       // Can not found
        {
            static int strict = -1;
            if (strict < 0)
                strict = getenv("PARALLEL_N64_COMBINER_STRICT") != NULL;
            if (strict)
                abort();
            combiner_journal_push(m_journal,
                  ((uint64_t)m_pDecodedMux->m_dwMux1 << 32) | m_pDecodedMux->m_dwMux0,
                  (bAlphaTestState ? 1u : 0u) | (bFogState ? 2u : 0u));
            m_lastIndex = ParseDecodedMux();
        }

        m_dwLastMux0 = m_pDecodedMux->m_dwMux0;
        m_dwLastMux1 = m_pDecodedMux->m_dwMux1;
        bAlphaTestPreviousState = bAlphaTestState;
        bFogPreviousState = bFogState;
        m_AlphaRef = (float)(OGLRenderer()->m_dwAlpha) / 255.0f;
    }


    GenerateCombinerSettingConstants(m_lastIndex);
    if( m_bCycleChanged || combinerIsChanged || gRDP.texturesAreReloaded || gRDP.colorsAreReloaded )
    {
        if( m_bCycleChanged || combinerIsChanged )
        {
            GenerateCombinerSettingConstants(m_lastIndex);
            GenerateCombinerSetting(m_lastIndex);
        }
        else if( gRDP.colorsAreReloaded )
        {
            GenerateCombinerSettingConstants(m_lastIndex);
        }

        OGLRenderer()->SetAllTexelRepeatFlag();

        gRDP.colorsAreReloaded = false;
        gRDP.texturesAreReloaded = false;
    }
    else
    {
        OGLRenderer()->SetAllTexelRepeatFlag();
    }
}

#ifdef DEBUGGER
void COGL_FragmentProgramCombiner::DisplaySimpleMuxString(void)
{
    COGLColorCombiner::DisplaySimpleMuxString();
    DecodedMuxForPixelShader &mux = *(DecodedMuxForPixelShader*)m_pDecodedMux;
    mux.Reformat(false);
    GenerateProgramStr();
    //sprintf(oglNewFP, oglFP, 
    //  MuxToOC(mux.aRGB0), MuxToOC(mux.bRGB0), MuxToOC(mux.cRGB0), MuxToOC(mux.dRGB0),
    //  MuxToOA(mux.aA0), MuxToOA(mux.bA0), MuxToOA(mux.cA0), MuxToOA(mux.dA0),
    //  MuxToOC(mux.aRGB1), MuxToOC(mux.bRGB1), MuxToOC(mux.cRGB1), MuxToOC(mux.dRGB1),
    //  MuxToOA(mux.aA1), MuxToOA(mux.bA1), MuxToOA(mux.cA1), MuxToOA(mux.dA1)
    //  );

    TRACE0("OGL Fragment Program:");
    TRACE0(oglNewFP);
}
#endif

