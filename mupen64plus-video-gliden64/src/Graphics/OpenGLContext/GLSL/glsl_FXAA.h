#ifndef GLI_GLSL_FXAA_H
#define GLI_GLSL_FXAA_H
/* FXAA's GLSL (glsl_FXAA.c). C89. */

#ifdef __cplusplus
extern "C" {
#endif

/* the vertex and fragment shaders' lines, NULL after the last */
const char *const *gli_glsl_fxaa_vertex(void);
const char *const *gli_glsl_fxaa_fragment(void);

#ifdef __cplusplus
}
#endif

#endif
