/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef MAIN_H
#define MAIN_H

#include <m64p_types.h>
#include <m64p_config.h>

//#define DEBUGLOG
//#define TEXTUREMANAGEMENT_LOG
//#define LOG_TO_STDERR
#define LOG_TO_STDOUT

#if defined(LOG_TO_STDERR)
#define LOG_TYPE stderr
#elif defined(LOG_TO_STDOUT)
#define LOG_TYPE stdout
#else
#define LOG_TYPE stderr
#endif


#ifdef DEBUGLOG
#define LOG(...) WriteLog(M64MSG_VERBOSE, __VA_ARGS__)
#define LOGINFO(...) WriteLog(M64MSG_INFO, __VA_ARGS__)
#else
#define LOG(...)
#define LOGINFO(...)
#endif

#ifdef TEXTUREMANAGEMENT_LOG
#define TEXLOG(...) fprintf(LOG_TYPE, __VA_ARGS__)
#else
#define TEXLOG(...)
#endif

void WriteLog(m64p_msg_level level, const char *msg, ...);

#define zscale 1.0f

// VP added this utility function
// returns the bytes per pixel of a given GR texture format
int grTexFormatSize(int fmt);

extern int packed_pixels_support;

extern int default_texture; // the infamous "32*1024*1024" is now configurable
extern int depth_texture;
void set_depth_shader(void);
void set_bw_shader(void);
extern float invtex[2];
extern int buffer_cleared; // mark that the buffer has been cleared, used to check if we need to reload the texture buffer content

#include <stdio.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL_opengles2.h>
#include "../glide_funcs.h"

void add_tex(unsigned int id);
void init_textures(void);
void free_textures(void);
void remove_tex(unsigned int idmin, unsigned int idmax);

void set_lambda(void);

int CheckTextureBufferFormat(GrChipID_t tmu, FxU32 startAddress, GrTexInfo *info );
void init_geometry(void);

void vbo_draw(void);
void vbo_disable(void);

void init_combiner(void);
void updateCombiner(int i);
void updateCombinera(int i);
void check_compile(GLuint shader);
void check_link(GLuint program);
void free_combiners(void);
void compile_shader(void);
void set_copy_shader(void);

//Vertex Attribute Locations
#define POSITION_ATTR 0
#define COLOUR_ATTR 1
#define TEXCOORD_0_ATTR 2
#define TEXCOORD_1_ATTR 3
#define FOG_ATTR 4

extern int width, height, widtho, heighto;
extern int tex0_width, tex0_height, tex1_width, tex1_height;
extern float texture_env_color[4];
extern int fog_enabled;
extern float lambda;
extern int need_lambda[2];
extern float lambda_color[2][4];
extern int inverted_culling;
extern int culling_mode;
extern int render_to_texture;
extern int lfb_color_fmt;
extern int need_to_compile;
extern int blackandwhite0;
extern int blackandwhite1;

extern int blend_func_separate_support;
extern int fog_coord_support;
extern int bgra8888_support;
//extern int pbuffer_support;
extern int glsl_support;
extern unsigned int pBufferAddress;
extern int viewport_width, viewport_height;

void disable_textureSizes(void);

int getFullScreenWidth(void);
int getFullScreenHeight(void);

#ifndef GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS // TODO: Not present
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS 18283
#endif

#ifndef GLES
#define glClearDepthf glClearDepth
#endif


#define CHECK_FRAMEBUFFER_STATUS() \
{\
 GLenum status; \
 status = glCheckFramebufferStatus(GL_FRAMEBUFFER); \
 /*DISPLAY_WARNING("%x\n", status);*/\
 switch(status) { \
 case GL_FRAMEBUFFER_COMPLETE: \
   /*DISPLAY_WARNING("framebuffer complete!\n");*/\
   break; \
 case GL_FRAMEBUFFER_UNSUPPORTED: \
   DISPLAY_WARNING("framebuffer GL_FRAMEBUFFER_UNSUPPORTED_EXT\n");\
    /* you gotta choose different formats */ \
   /*assert(0);*/ \
   break; \
 case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: \
   DISPLAY_WARNING("framebuffer INCOMPLETE_ATTACHMENT\n");\
   break; \
 case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: \
   DISPLAY_WARNING("framebuffer FRAMEBUFFER_MISSING_ATTACHMENT\n");\
   break; \
 case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS: \
   DISPLAY_WARNING("framebuffer FRAMEBUFFER_DIMENSIONS\n");\
   break; \
 default: \
   break; \
   /* programming error; will fail on all hardware */ \
   /*assert(0);*/ \
 }\
}

#ifdef LOGGING
void OPEN_LOG();
void CLOSE_LOG();
//void LOG(const char *text, ...);
#else // LOGGING
#define OPEN_LOG()
#define CLOSE_LOG()
//#define LOG
#endif // LOGGING

extern void check_gl_error(const char *stmt, const char *fname, int line);

//#define LOG_GL_CALLS

#ifdef LOG_GL_CALLS
#define GL_CHECK(func) \
   do { \
   func; \
   check_gl_error(#func, __FILE__, __LINE__); \
} while(0)
#else
#define GL_CHECK(func) func
#endif

int retro_return(bool just_flipping);

#endif
