#ifndef _VI_H_
#define _VI_H_

#include <stdint.h>
//#include <ddraw.h>
#include "Gfx #1.3.h"
#include "z64.h"

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH        640
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT       480
#endif

typedef struct {
    unsigned char r, g, b, cvg;
} CCVG;

typedef struct {
    int ntscnolerp;
    int copymstrangecrashes;
    int fillmcrashes;
    int fillmbitcrashes;
    int syncfullcrash;
} onetime;

typedef struct {
    UINT8 cvg;
    UINT8 cvbit;
    UINT8 xoff;
    UINT8 yoff;
} CVtcmaskDERIVATIVE;

#ifdef _WIN32
#include <windows.h>
#else
typedef struct _RECT {
   int32_t left;
   int32_t top;
   int32_t right;
   int32_t bottom;
} RECT, *PRECT;

typedef struct {   // Declare an unnamed structure and give it the
                   // typedef name POINT.
   int32_t x;
   int32_t y;
} POINT;
#endif

//extern LPDIRECTDRAW7 lpdd;
//extern LPDIRECTDRAWSURFACE7 lpddsprimary;
//extern LPDIRECTDRAWSURFACE7 lpddsback;
//extern DDSURFACEDESC2 ddsd;
extern RECT __src, __dst;
extern int res;
extern int32_t pitchindwords;

extern UINT8* rdram_8;
extern UINT16* rdram_16;
extern UINT32 plim;
extern UINT32 idxlim16;
extern UINT32 idxlim32;
extern UINT8 hidden_bits[0x400000];

extern int overlay;
extern int skip;
extern int step;

extern onetime onetimewarnings;
extern UINT32 gamma_table[0x100];
extern UINT32 gamma_dither_table[0x4000];
extern INT32 vi_restore_table[0x400];
extern INT32 oldvstart;
extern INT32* PreScale;

extern NOINLINE void DisplayError(char * error);

extern STRICTINLINE INT32 irand(void);
extern void rdp_init(void);
extern void rdp_close(void);
extern void rdp_update(void);

#endif
