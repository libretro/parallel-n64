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
    uint8_t cvg;
    uint8_t cvbit;
    uint8_t xoff;
    uint8_t yoff;
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

extern RECT __src, __dst;
extern int res;
extern int32_t pitchindwords;

extern uint8_t* rdram_8;
extern uint16_t* rdram_16;
extern uint32_t plim;
extern uint32_t idxlim16;
extern uint32_t idxlim32;
extern uint8_t hidden_bits[0x400000];

extern int overlay;
extern int skip;
extern int step;

extern onetime onetimewarnings;
extern uint32_t gamma_table[0x100];
extern uint32_t gamma_dither_table[0x4000];
extern int32_t vi_restore_table[0x400];
extern int32_t oldvstart;
extern int32_t* PreScale;

extern NOINLINE void DisplayError(char * error);

extern STRICTINLINE int32_t irand(void);
extern void rdp_init(void);
extern void rdp_close(void);
extern void rdp_update(void);

#endif
