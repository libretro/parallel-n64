#ifndef __3DFX_H__
#define __3DFX_H__

/*
** color types
*/
typedef struct { float r, g, b, a; } FxColor4;

/*
** fundamental types
*/
#define FXTRUE    1
#define FXFALSE   0

/*
** helper macros
*/
#define FXBIT( i )    ( 1L << (i) )

#endif /* !__3DFX_H__ */
