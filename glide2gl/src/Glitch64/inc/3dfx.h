#ifndef __3DFX_H__
#define __3DFX_H__

typedef uint8_t FxU8;
typedef int8_t  FxI8;
typedef uint16_t  FxU16;
typedef int16_t  FxI16;
typedef int32_t   FxI32;
typedef uint32_t   FxU32;
typedef uintptr_t   AnyPtr;
typedef int             FxBool;
typedef float           FxFloat;
typedef double          FxDouble;

/*
** color types
*/
typedef uint32_t                FxColor_t;
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
