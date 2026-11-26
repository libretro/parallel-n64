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
/* Rice's colour combiner (RiceCombiner.h). C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_opengl.h"

#include "m64p_plugin.h"
#include "Config.h"
#include "OGLTexture.h"
#include "RenderBase.h"
#include "RiceCombiner.h"
#include "RiceCombinerText.h"
#include "RiceGLTex.h"
#include "RSP_Parser.h"
#include "Video.h"

#include "../../mupen64plus-video-gliden64/src/CombinerKeyJournal.h"
#include "../../mupen64plus-video-gliden64/src/CombinerKeyPath.h"

/* lines of each game's (1: a file a game) */
#define COMBINER_JOURNAL_VERSION 2u
#define COMBINER_JOURNAL_OLD_VERSION 1u

RiceCombiner rice_combiner;

#define ALPHA_TEST "    if(gl_FragColor.a < AlphaRef) discard;                        \n"

static GLuint vertexProgram = 9999;
static const char *vertexShader =
"#version " GLSL_VERSION "\n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#endif
"attribute mediump vec4 aPosition;                          \n"
"attribute lowp vec4    aColor;                             \n"
"attribute lowp vec2    aTexCoord0;                         \n"
"attribute lowp vec2    aTexCoord1;                         \n"
"attribute lowp vec2    aAtlasTransform;                    \n"
"attribute mediump float aFogCoord;                         \n"
"                                                           \n"
"uniform vec2 FogMinMax;                                    \n"
"                                                           \n"
"varying lowp float vFactor;                                \n"
"varying lowp vec4  vShadeColor;                            \n"
"varying mediump vec2 vTexCoord0;                           \n"
"varying lowp vec2    vTexCoord1;                           \n"
"varying lowp float   vFog;                                 \n"
"                                                           \n"
"void main()                                                \n"
"{                                                          \n"
"    gl_Position = aPosition;                               \n"
"    vShadeColor = aColor;                                  \n"
"    vTexCoord0 = aTexCoord0;                               \n"
"    vTexCoord1 = aTexCoord1;                               \n"
"    vFog = (FogMinMax[1] - aFogCoord) / (FogMinMax[1] - FogMinMax[0]); \n"
"    vFog = clamp(vFog, 0.0, 1.0);                          \n"
"}                                                          \n"
"                                                           \n";

/* the COPY cycle's */
static const char *fragmentCopy =
"#version " GLSL_VERSION "\n"
#if !defined(HAVE_OPENGLES)
"#define lowp                                               \n"
"#define mediump                                            \n"
#else
"precision lowp float;                                      \n"
#endif
"uniform sampler2D uTex0;                                   \n"
"uniform float AlphaRef;                                    \n"
"varying lowp vec2 vTexCoord0;                              \n"
"void main()                                                \n"
"{                                                          \n"
"   gl_FragColor = texture2D(uTex0,vTexCoord0).bgra;        \n"
ALPHA_TEST
"}";

static GLuint copyProgram, copyAlphaLocation;

/* the FILL cycle's */
static const char *fragmentFill =
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

static GLuint fillProgram, fillColorLocation;

static void print_log(GLuint object, bool program)
{
    char log[1024];
    if (program)
        glGetProgramInfoLog(object, 1024, NULL, log);
    else
        glGetShaderInfoLog(object, 1024, NULL, log);
    printf("%s\n", log);
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLint success;
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &source, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &success);
    if (!success)
        print_log(sh, false);
    return sh;
}

static void use_program(GLuint program)
{
    if (program != rice_combiner.currentProgram)
    {
        glUseProgram(program);
        rice_combiner.currentProgram = program;
    }
}

void RiceCombiner_Create(void)
{
    GLint success;
    GLuint vs, fs;
    RiceCombiner *c = &rice_combiner;

    memset(c, 0, sizeof(*c));
    DecodedMuxCache_Init(&c->muxCache, DECODED_MUX_CACHE_SIZE);
    DecodedMux_Init(&c->mux);
    c->lastIndex = -1;
    c->programs  = (RiceCombinerProgram*)calloc(RICE_COMBINER_MAX_PROGRAMS, sizeof(*c->programs));

    /* the copy and fill programs */
    copyProgram = glCreateProgram();
    vs = compile_shader(GL_VERTEX_SHADER, vertexShader);
    fs = compile_shader(GL_FRAGMENT_SHADER, fragmentCopy);

    glAttachShader(copyProgram, vs);
    glAttachShader(copyProgram, fs);

    glBindAttribLocation(copyProgram, VS_TEXCOORD0, "aTexCoord0");
    glBindAttribLocation(copyProgram, VS_POSITION, "aPosition");

    glLinkProgram(copyProgram);
    copyAlphaLocation = glGetUniformLocation(copyProgram, "AlphaRef");
    glGetProgramiv(copyProgram, GL_LINK_STATUS, &success);
    if (!success)
        print_log(copyProgram, true);

    glDeleteShader(fs);

    fs = compile_shader(GL_FRAGMENT_SHADER, fragmentFill);

    fillProgram = glCreateProgram();
    glAttachShader(fillProgram, vs);
    glAttachShader(fillProgram, fs);

    glBindAttribLocation(fillProgram, VS_POSITION, "aPosition");

    glLinkProgram(fillProgram);

    fillColorLocation = glGetUniformLocation(fillProgram, "uColor");

    glDeleteShader(fs);
    glDeleteShader(vs);
}

void RiceCombiner_Destroy(void)
{
    RiceCombiner *c = &rice_combiner;
    int i;

    combiner_journal_close(c->journal);
    c->journal = NULL;

    for (i = 0; i < c->numPrograms; i++)
    {
        glDeleteProgram(c->programs[i].programID);
        c->programs[i].programID = 0;
    }
    c->numPrograms = 0;
    free(c->programs);
    c->programs = NULL;

    DecodedMuxCache_Free(&c->muxCache);
}

void RiceCombiner_Update(uint32_t dwMux0, uint32_t dwMux1)
{
    RiceCombiner *c = &rice_combiner;
    if (DecodedMux_Update(&c->mux, &c->muxCache, dwMux0, dwMux1))
    {
        c->tex0Enabled = c->mux.m_bTexel0IsUsed;
        c->tex1Enabled = c->mux.m_bTexel1IsUsed;
    }
}

void RiceCombiner_SetTexelRepeat(void)
{
    RiceCombiner *c = &rice_combiner;
    if (c->tex0Enabled || c->tex1Enabled)
    {
        if (c->tex0Enabled || gRDP.otherMode.cycle_type == G_CYC_COPY)
            RiceGLTex_SetTileRepeat(gRSP.curTile);
        if (c->tex1Enabled)
            RiceGLTex_SetTileRepeat((gRSP.curTile + 1) & 7);
    }
}

static void init_cycle_copy(void)
{
    COGLTexture *pTexture;

    RiceGLTex_DisableMultiTexture();
    RiceGLTex_EnableUnit(0, true);
    use_program(copyProgram);
    glUniform1f(copyAlphaLocation, rice_combiner.alphaRef);
    glEnableVertexAttribArray(VS_POSITION);
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glDisableVertexAttribArray(VS_COLOR);
    glDisableVertexAttribArray(VS_TEXCOORD1);
    glDisableVertexAttribArray(VS_FOG);
    pTexture = (COGLTexture *)g_textures[gRSP.curTile].m_pCTexture;
    if (pTexture)
    {
        RiceGLTex_Bind(pTexture->m_dwTextureName, 0);
        RiceGLTex_SetTileRepeat(gRSP.curTile);
    }
}

/* DrawFrameBuffer puts a frame the CPU wrote into RDRAM on screen through
 * this. Fixed-function state alone leaves no program bound: the draw then
 * runs with program 0, which is undefined in a core context and crashes
 * Mesa. Sample the frame with the copy program instead, with nothing
 * discarded. */
void RiceCombiner_InitSimpleTextureDraw(uint32_t tile)
{
    RiceGLTex_DisableMultiTexture();
    if (g_textures[tile].m_pCTexture)
    {
        RiceGLTex_EnableUnit(0, true);
        glBindTexture(GL_TEXTURE_2D, ((COGLTexture*)(g_textures[tile].m_pCTexture))->m_dwTextureName);
    }
    RiceCombiner_SetTexelRepeat();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    rice_combiner.alphaTest = false;
    use_program(copyProgram);
    glUniform1f(copyAlphaLocation, 0.0f);
}

static void init_cycle_fill(void)
{
    use_program(fillProgram);
    glUniform4f(fillColorLocation,
          ((gRDP.fillColor>>16)&0xFF)/255.0f, ((gRDP.fillColor>>8)&0xFF)/255.0f,
          ((gRDP.fillColor)&0xFF)/255.0f, ((gRDP.fillColor>>24)&0xFF)/255.0f);
}

/* the slot the next program goes in: a new one, or once full the oldest
 * (its program deleted) */
static int take_slot(void)
{
    RiceCombiner *c = &rice_combiner;
    int i;
    if (c->numPrograms < RICE_COMBINER_MAX_PROGRAMS)
        return c->numPrograms++;
    i = c->nextReplaced;
    c->nextReplaced = (i + 1) % RICE_COMBINER_MAX_PROGRAMS;
    glDeleteProgram(c->programs[i].programID);
    return i;
}

/* one program, for the mux, fog and alpha test it is asked for */
static int build_program(void)
{
    static char oglNewFP[RICE_COMBINER_TEXT_SIZE];
    RiceCombiner *c = &rice_combiner;
    RiceCombinerProgram res;
    GLint success;
    char tmpShader[4096];
    const char *shaderSource = tmpShader;
    GLint tex0, tex1;
    int slot;

    memset(&res, 0, sizeof(res));

    if (vertexProgram == 9999)
    {
        vertexProgram = res.vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(res.vertexShaderID, 1, &vertexShader, NULL);
        glCompileShader(res.vertexShaderID);
    }
    else
        res.vertexShaderID = vertexProgram;

    RiceCombiner_ProgramText(&c->mux, oglNewFP, NULL, NULL);

    res.fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

    strcpy(tmpShader, "#version " GLSL_VERSION "\n");
    if (c->alphaTest)
        strcat(tmpShader, "#define ALPHA_TEST\n");
    if (c->fog)
        strcat(tmpShader, "#define FOG\n");

    res.fogIsUsed = c->fog;
    res.alphaTest = c->alphaTest;
    strcat(tmpShader, oglNewFP);

    glShaderSource(res.fragmentShaderID, 1, &shaderSource, NULL);
    glCompileShader(res.fragmentShaderID);

    glGetShaderiv(res.fragmentShaderID, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        printf("Error compiling shader!\n %s", oglNewFP);
        print_log(res.fragmentShaderID, false);
    }

    res.programID = glCreateProgram();
    glAttachShader(res.programID, res.vertexShaderID);
    glAttachShader(res.programID, res.fragmentShaderID);

    glBindAttribLocation(res.programID, VS_COLOR, "aColor");
    glBindAttribLocation(res.programID, VS_TEXCOORD0, "aTexCoord0");
    glBindAttribLocation(res.programID, VS_TEXCOORD1, "aTexCoord1");
    glBindAttribLocation(res.programID, VS_POSITION, "aPosition");
    glBindAttribLocation(res.programID, VS_FOG, "aFogCoord");

    glLinkProgram(res.programID);

    glGetProgramiv(res.programID, GL_LINK_STATUS, &success);
    if (!success)
    {
        printf("Error linking program!\n");
        print_log(res.fragmentShaderID, false);
    }

    use_program(res.programID);

    /* the samplers' units */
    tex0 = glGetUniformLocation(res.programID, "uTex0");
    tex1 = glGetUniformLocation(res.programID, "uTex1");
    if (tex0 != -1)
        glUniform1i(tex0, 0);
    if (tex1 != -1)
        glUniform1i(tex1, 1);

    res.PrimColorLocation = glGetUniformLocation(res.programID, "PrimColor");
    res.EnvColorLocation  = glGetUniformLocation(res.programID, "EnvColor");
    res.PrimFracLocation  = glGetUniformLocation(res.programID, "PrimFrac");
    res.EnvFracLocation   = glGetUniformLocation(res.programID, "EnvFrac");
    res.AlphaRefLocation  = glGetUniformLocation(res.programID, "AlphaRef");
    res.FogColorLocation  = glGetUniformLocation(res.programID, "FogColor");
    res.FogMinMaxLocation = glGetUniformLocation(res.programID, "FogMinMax");

    res.dwMux0 = c->mux.m_dwMux0;
    res.dwMux1 = c->mux.m_dwMux1;

    slot = take_slot();
    c->programs[slot] = res;
    c->lastIndex = slot;
    return slot;
}

static int find_program(void)
{
    const RiceCombiner *c = &rice_combiner;
    int i;
    for (i = 0; i < c->numPrograms; i++)
    {
        const RiceCombinerProgram *p = &c->programs[i];
        if (p->dwMux0 == c->mux.m_dwMux0
              && p->dwMux1 == c->mux.m_dwMux1
              && p->fogIsUsed == c->fog
              && p->alphaTest == c->alphaTest)
            return i;
    }
    return -1;
}

/* each key an earlier session met, built before the first draw */
static void prebuild_key(void *ctx, uint64_t key, unsigned flags)
{
    (void)ctx;
    rice_combiner.alphaTest = (flags & 1) != 0;
    rice_combiner.fog       = (flags & 2) != 0;
    RiceCombiner_Update((uint32_t)key, (uint32_t)(key >> 32));
    if (find_program() < 0)
        build_program();
}

void RiceCombiner_BuildKnownKeys(void)
{
    char path[PATH_MAX_LENGTH];
    char folder[PATH_MAX_LENGTH];
    char game[COMBINER_JOURNAL_GAME + 1];
    RiceCombiner *c = &rice_combiner;
    const bool alpha = c->alphaTest;
    const bool fog   = c->fog;

    if (c->journal)
        return;
    /* one journal every game shares, this game's keys built; the files a
     * game of before read into it once */
    combiner_key_shared(path, folder, sizeof(path), "rice", game, gfx_info.HEADER);
    if (!combiner_journal_read_game(path, COMBINER_JOURNAL_VERSION, game, prebuild_key, NULL)
            && combiner_journal_gather(path, COMBINER_JOURNAL_VERSION, folder, "rice_", COMBINER_JOURNAL_OLD_VERSION))
        combiner_journal_read_game(path, COMBINER_JOURNAL_VERSION, game, prebuild_key, NULL);
    /* the next draw decodes its own mux, with the game's own state */
    c->mux.m_dwMux0 = c->mux.m_dwMux1 = 0;
    c->alphaTest = alpha;
    c->fog       = fog;
    c->journal = combiner_journal_open_game(path, COMBINER_JOURNAL_VERSION, game);
}

static void set_vertex_arrays(int index)
{
    use_program(rice_combiner.programs[index].programID);
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][0]));

    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[0].u));

    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[1].u));

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]));

    glEnableVertexAttribArray(VS_FOG);
    glVertexAttribPointer(VS_FOG, 1, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][4]));
}

static void set_constants(int index)
{
    RiceCombinerProgram *prog = &rice_combiner.programs[index];
    float *pf;

    use_program(prog->programID);
    if (prog->EnvColorLocation != -1)
    {
        pf = gRDP.fvEnvColor;
        if (memcmp(pf, prog->EnvColors, sizeof(prog->EnvColors)))
        {
            memcpy(prog->EnvColors, pf, sizeof(prog->EnvColors));
            glUniform4fv(prog->EnvColorLocation, 1, pf);
        }
    }

    if (prog->PrimColorLocation != -1)
    {
        pf = gRDP.fvPrimitiveColor;
        if (memcmp(pf, prog->PrimColors, sizeof(prog->PrimColors)))
        {
            memcpy(prog->PrimColors, pf, sizeof(prog->PrimColors));
            glUniform4fv(prog->PrimColorLocation, 1, pf);
        }
    }

    if (prog->EnvFracLocation != -1)
    {
        /* the bits compared, not the floats */
        if (*(int *)&gRDP.LODFrac != *(int *)&prog->EnvLODFrac)
        {
            float frac = gRDP.LODFrac / 255.0f;
            float tempf[4];
            tempf[0] = tempf[1] = tempf[2] = tempf[3] = frac;

            prog->EnvLODFrac = (float)gRDP.LODFrac;
            glUniform4fv(prog->EnvFracLocation, 1, tempf);
        }
    }

    if (prog->PrimFracLocation != -1)
    {
        if (*(int *)&gRDP.primLODFrac != *(int *)&prog->PrimLODFrac)
        {
            float frac2 = gRDP.primLODFrac / 255.0f;
            float tempf2[4];
            tempf2[0] = tempf2[1] = tempf2[2] = tempf2[3] = frac2;

            prog->PrimLODFrac = (float)gRDP.primLODFrac;
            glUniform4fv(prog->PrimFracLocation, 1, tempf2);
        }
    }

    if (prog->FogColorLocation != -1)
    {
        pf = &gRDP.fvFogColor[0];
        if (memcmp(pf, prog->FogColors, sizeof(prog->FogColors)))
        {
            memcpy(prog->FogColors, pf, sizeof(prog->FogColors));
            glUniform4fv(prog->FogColorLocation, 1, pf);
        }
    }

    if (prog->FogMinMaxLocation != -1)
    {
        if (gRSPfFogMin != prog->FogMin || gRSPfFogMax != prog->FogMax)
        {
            prog->FogMin = gRSPfFogMin;
            prog->FogMax = gRSPfFogMax;
            glUniform2f(prog->FogMinMaxLocation, gRSPfFogMin, gRSPfFogMax);
        }
    }

    if (prog->AlphaRefLocation != -1)
    {
        if (rice_combiner.alphaRef != prog->AlphaRef)
        {
            prog->AlphaRef = rice_combiner.alphaRef;
            glUniform1f(prog->AlphaRefLocation, rice_combiner.alphaRef);
        }
    }
}

static void init_cycle12(uint32_t dwAlpha)
{
    RiceCombiner *c = &rice_combiner;
    bool combinerIsChanged = false;

    RiceGLTex_EnableMultiTexture();

    if (c->mux.m_dwMux0 != c->lastMux0 || c->mux.m_dwMux1 != c->lastMux1
          || c->alphaTest != c->alphaTestPrevious || c->fog != c->fogPrevious || c->lastIndex < 0)
    {
        combinerIsChanged = true;
        c->lastIndex = find_program();
        if (c->lastIndex < 0)
        {
            static int strict = -1;
            if (strict < 0)
                strict = getenv("PARALLEL_N64_COMBINER_STRICT") != NULL;
            if (strict)
                abort();
            combiner_journal_push(c->journal,
                  ((uint64_t)c->mux.m_dwMux1 << 32) | c->mux.m_dwMux0,
                  (c->alphaTest ? 1u : 0u) | (c->fog ? 2u : 0u));
            c->lastIndex = build_program();
        }

        c->lastMux0 = c->mux.m_dwMux0;
        c->lastMux1 = c->mux.m_dwMux1;
        c->alphaTestPrevious = c->alphaTest;
        c->fogPrevious = c->fog;
        c->alphaRef = (float)dwAlpha / 255.0f;
    }

    set_constants(c->lastIndex);
    if (c->cycleChanged || combinerIsChanged || gRDP.texturesAreReloaded || gRDP.colorsAreReloaded)
    {
        if (c->cycleChanged || combinerIsChanged)
        {
            set_constants(c->lastIndex);
            set_vertex_arrays(c->lastIndex);
        }
        else if (gRDP.colorsAreReloaded)
            set_constants(c->lastIndex);

        RiceCombiner_SetTexelRepeat();

        gRDP.colorsAreReloaded = false;
        gRDP.texturesAreReloaded = false;
    }
    else
        RiceCombiner_SetTexelRepeat();
}

void RiceCombiner_InitMode(uint32_t dwAlpha)
{
    RiceCombiner *c = &rice_combiner;

    if (currentRomOptions.bNormalCombiner)
        return;
    if (gRDP.otherMode.cycle_type == G_CYC_COPY)
    {
        init_cycle_copy();
        c->cycleChanged = true;
    }
    else if (gRDP.otherMode.cycle_type == G_CYC_FILL)
    {
        init_cycle_fill();
        c->cycleChanged = true;
    }
    else
    {
        init_cycle12(dwAlpha);
        c->cycleChanged = false;
    }
}
