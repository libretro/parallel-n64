#ifndef __Z64_H__
#define __Z64_H__

#include <stdio.h>

#if defined (_MSC_VER) && (_MSC_VER >= 1300)
#include <basetsd.h>
#endif

#if defined (_MSC_VER) && (_MSC_VER < 1300)
typedef unsigned char UINT8;
typedef signed short INT16;
typedef unsigned short UINT16;
#endif

#if !defined (_MSC_VER) || (_MSC_VER >= 1600)
#include <stdint.h>
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint16_t UINT16;
typedef int16_t INT16;
typedef uint8_t UINT8;
typedef int8_t INT8;
#endif

#define SP_INTERRUPT    0x1
#define SI_INTERRUPT    0x2
#define AI_INTERRUPT    0x4
#define VI_INTERRUPT    0x8
#define PI_INTERRUPT    0x10
#define DP_INTERRUPT    0x20

#define SP_STATUS_HALT            0x0001
#define SP_STATUS_BROKE            0x0002
#define SP_STATUS_DMABUSY        0x0004
#define SP_STATUS_DMAFULL        0x0008
#define SP_STATUS_IOFULL        0x0010
#define SP_STATUS_SSTEP            0x0020
#define SP_STATUS_INTR_BREAK    0x0040
#define SP_STATUS_SIGNAL0        0x0080
#define SP_STATUS_SIGNAL1        0x0100
#define SP_STATUS_SIGNAL2        0x0200
#define SP_STATUS_SIGNAL3        0x0400
#define SP_STATUS_SIGNAL4        0x0800
#define SP_STATUS_SIGNAL5        0x1000
#define SP_STATUS_SIGNAL6        0x2000
#define SP_STATUS_SIGNAL7        0x4000

#define DP_STATUS_XBUS_DMA        0x01
#define DP_STATUS_FREEZE        0x02
#define DP_STATUS_FLUSH            0x04
#define DP_STATUS_START_GCLK        0x008
#define DP_STATUS_TMEM_BUSY        0x010
#define DP_STATUS_PIPE_BUSY        0x020
#define DP_STATUS_CMD_BUSY            0x040
#define DP_STATUS_CBUF_READY        0x080
#define DP_STATUS_DMA_BUSY            0x100
#define DP_STATUS_END_VALID        0x200
#define DP_STATUS_START_VALID        0x400

#define R4300i_SP_Intr 1


#define LSB_FIRST 1 
#ifdef LSB_FIRST
    #define BYTE_ADDR_XOR        3
    #define WORD_ADDR_XOR        1
    #define BYTE4_XOR_BE(a)     ((a) ^ 3)                
#else
    #define BYTE_ADDR_XOR        0
    #define WORD_ADDR_XOR        0
    #define BYTE4_XOR_BE(a)     (a)
#endif

#ifdef LSB_FIRST
#define BYTE_XOR_DWORD_SWAP 7
#define WORD_XOR_DWORD_SWAP 3
#else
#define BYTE_XOR_DWORD_SWAP 4
#define WORD_XOR_DWORD_SWAP 2
#endif
#define DWORD_XOR_DWORD_SWAP 1

#ifdef _MSC_VER
#define NOINLINE        __declspec(noinline)
#define STRICTINLINE    __forceinline
#define ALIGNED         __declspec(align(16))
#else
#define NOINLINE        __attribute__((noinline))
#define STRICTINLINE    INLINE
#define ALIGNED         __attribute__((aligned(16)))
#endif

#define PRESCALE_WIDTH 640
#define PRESCALE_HEIGHT 625

#define RDRAM_MASK 0x00ffffff

typedef unsigned int offs_t;

#define GET_GFX_INFO(member)    (gfx_info.member)

#define DRAM        GET_GFX_INFO(RDRAM)
#define DRAM16      ((i16 *)DRAM)
#define DRAM32      ((i32 *)DRAM)

#define SP_DMEM         GET_GFX_INFO(DMEM)
#define SP_IMEM         GET_GFX_INFO(IMEM)
#define SP_DMEM16       ((i16 *)SP_DMEM)
#define SP_IMEM16       ((i16 *)SP_IMEM)
#define SP_DMEM32       ((i32 *)SP_DMEM)
#define SP_IMEM32       ((i32 *)SP_IMEM)

#define rdram ((UINT32*)DRAM)
#define rsp_imem ((UINT32*)gfx_info.IMEM)
#define rsp_dmem ((UINT32*)gfx_info.DMEM)

#define rdram16 ((UINT16*)DRAM)
#define rdram8 (DRAM)

#define vi_origin (*(UINT32*)gfx_info.VI_ORIGIN_REG)
#define vi_width (*(UINT32*)gfx_info.VI_WIDTH_REG)
#define vi_control (*(UINT32*)gfx_info.VI_STATUS_REG)
#define vi_v_sync (*(UINT32*)gfx_info.VI_V_SYNC_REG)
#define vi_h_sync (*(UINT32*)gfx_info.VI_H_SYNC_REG)
#define vi_h_start (*(UINT32*)gfx_info.VI_H_START_REG)
#define vi_v_start (*(UINT32*)gfx_info.VI_V_START_REG)
#define vi_v_intr (*(UINT32*)gfx_info.VI_INTR_REG)
#define vi_x_scale (*(UINT32*)gfx_info.VI_X_SCALE_REG)
#define vi_y_scale (*(UINT32*)gfx_info.VI_Y_SCALE_REG)
#define vi_timing (*(UINT32*)gfx_info.VI_TIMING_REG)
#define vi_v_current_line (*(UINT32*)gfx_info.VI_V_CURRENT_LINE_REG)

#define dp_start (*(UINT32*)gfx_info.DPC_START_REG)
#define dp_end (*(UINT32*)gfx_info.DPC_END_REG)
#define dp_current (*(UINT32*)gfx_info.DPC_CURRENT_REG)
#define dp_status (*(UINT32*)gfx_info.DPC_STATUS_REG)

#define GET_LOW(x)      (((x) & 0x003E) << 2)
#define GET_MED(x)      (((x) & 0x07C0) >> 3)
#define GET_HI(x)       (((x) >> 8) & 0x00F8)

#define RREADADDR8(rdst, in) {(in) &= RDRAM_MASK; (rdst) = ((in) <= plim) ? (rdram_8[(in) ^ BYTE_ADDR_XOR]) : 0;}
#define RREADIDX16(rdst, in) {(in) &= (RDRAM_MASK >> 1); (rdst) = ((in) <= idxlim16) ? (rdram_16[(in) ^ WORD_ADDR_XOR]) : 0;}
#define RREADIDX32(rdst, in) {(in) &= (RDRAM_MASK >> 2); (rdst) = ((in) <= idxlim32) ? (rdram[(in)]) : 0;}

#define RWRITEADDR8(in, val)	{(in) &= RDRAM_MASK; if ((in) <= plim) rdram_8[(in) ^ BYTE_ADDR_XOR] = (val);}
#define RWRITEIDX16(in, val)	{(in) &= (RDRAM_MASK >> 1); if ((in) <= idxlim16) rdram_16[(in) ^ WORD_ADDR_XOR] = (val);}
#define RWRITEIDX32(in, val)	{(in) &= (RDRAM_MASK >> 2); if ((in) <= idxlim32) rdram[(in)] = (val);}

#define PAIRREAD16(rdst, hdst, in) {             \
   (in) &= (RDRAM_MASK >> 1);			             \
    if ((in) <= idxlim16) {                      \
        (rdst) = rdram_16[(in) ^ WORD_ADDR_XOR]; \
        (hdst) = hidden_bits[(in)];              \
    } else                                       \
        (rdst) = (hdst) = 0;                     \
}
#define PAIRWRITE16(in, rval, hval) {            \
   (in) &= (RDRAM_MASK >> 1);	                   \
    if ((in) <= idxlim16) {                      \
        rdram_16[(in) ^ WORD_ADDR_XOR] = (rval); \
        hidden_bits[(in)] = (hval);              \
    }                                            \
}
#define PAIRWRITE32(in, rval, hval0, hval1) {    \
   (in) &= (RDRAM_MASK >> 2);                    \
    if ((in) <= idxlim32) {                      \
        rdram[(in)] = (rval);                    \
        hidden_bits[(in) << 1] = (hval0);        \
        hidden_bits[((in) << 1) + 1] = (hval1);  \
    }                                            \
}
#define PAIRWRITE8(in, rval, hval) {             \
   (in) &= RDRAM_MASK;                           \
    if ((in) <= plim) {                          \
        rdram_8[(in) ^ BYTE_ADDR_XOR] = (rval);  \
        if ((in) & 1)                            \
            hidden_bits[(in) >> 1] = (hval);     \
    }                                            \
}

#define VI_ANDER(x) {                                 \
    PAIRREAD16(pix, hidval, x);                       \
    if (hidval == 3 && (pix & 1)) {                   \
        backr[numoffull] = GET_HI(pix);               \
        backg[numoffull] = GET_MED(pix);              \
        backb[numoffull] = GET_LOW(pix);              \
       numoffull++;                                      \
    }                                                 \
}
#define VI_ANDER32(x) {                               \
   RREADIDX32(pix, (x));                              \
    pixcvg = (pix >> 5) & 7;                          \
    if (pixcvg == 7) {                                \
        backr[numoffull] = (pix >> 24) & 0xFF;        \
        backg[numoffull] = (pix >> 16) & 0xFF;        \
        backb[numoffull] = (pix >>  8) & 0xFF;        \
       numoffull++;                                      \
    }                                           \
}
#define VI_COMPARE(x) {                      \
   addr = (x);                               \
   RREADIDX16(pix, addr);                    \
    tempr = (pix >> 6) & 0x03E0;             \
    tempg = (pix >> 1) & 0x03E0;             \
    tempb = (pix << 4) & 0x03E0;             \
    rend += vi_restore_table[tempr | rcomp]; \
    gend += vi_restore_table[tempg | gcomp]; \
    bend += vi_restore_table[tempb | bcomp]; \
}
#define VI_COMPARE32(x) {                    \
   addr = (x);                               \
   RREADIDX32(pix, addr);                    \
    tempr = (pix >> 19) & 0x03E0;            \
    tempg = (pix >> 11) & 0x03E0;            \
    tempb = (pix >>  3) & 0x03E0;            \
    rend += vi_restore_table[tempr | rcomp]; \
    gend += vi_restore_table[tempg | gcomp]; \
    bend += vi_restore_table[tempb | bcomp]; \
}

#endif
