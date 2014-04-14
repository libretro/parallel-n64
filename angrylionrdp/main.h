#ifndef MAIN_H
#define MAIN_H

#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_config.h"
#include "m64p_vidext.h"
#include "../../libretro/libretro.h"

#define GL_GLEXT_PROTOTYPES
#include <SDL_opengles2.h>
#include "../../libretro/SDL.h"

#include <stdint.h>

extern GFX_INFO gfx_al;

int rdp_init();
int rdp_close();
int rdp_update();
void rdp_process_list(void);

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

extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_pitch;

#define SP_INTERRUPT	0x1
#define SI_INTERRUPT	0x2
#define AI_INTERRUPT	0x4
#define VI_INTERRUPT	0x8
#define PI_INTERRUPT	0x10
#define DP_INTERRUPT	0x20

#define SP_STATUS_HALT			0x0001
#define SP_STATUS_BROKE			0x0002
#define SP_STATUS_DMABUSY		0x0004
#define SP_STATUS_DMAFULL		0x0008
#define SP_STATUS_IOFULL		0x0010
#define SP_STATUS_SSTEP			0x0020
#define SP_STATUS_INTR_BREAK	0x0040
#define SP_STATUS_SIGNAL0		0x0080
#define SP_STATUS_SIGNAL1		0x0100
#define SP_STATUS_SIGNAL2		0x0200
#define SP_STATUS_SIGNAL3		0x0400
#define SP_STATUS_SIGNAL4		0x0800
#define SP_STATUS_SIGNAL5		0x1000
#define SP_STATUS_SIGNAL6		0x2000
#define SP_STATUS_SIGNAL7		0x4000

#define DP_STATUS_XBUS_DMA		0x01
#define DP_STATUS_FREEZE		0x02
#define DP_STATUS_FLUSH			0x04
#define DP_STATUS_START_GCLK		0x008
#define DP_STATUS_TMEM_BUSY		0x010
#define DP_STATUS_PIPE_BUSY		0x020
#define DP_STATUS_CMD_BUSY			0x040
#define DP_STATUS_CBUF_READY		0x080
#define DP_STATUS_DMA_BUSY			0x100
#define DP_STATUS_END_VALID		0x200
#define DP_STATUS_START_VALID		0x400

#define R4300i_SP_Intr 1


#define LSB_FIRST 1 
#ifdef LSB_FIRST
	#define BYTE_ADDR_XOR		3
	#define WORD_ADDR_XOR		1
	#define BYTE4_XOR_BE(a) 	((a) ^ 3)				
#else
	#define BYTE_ADDR_XOR		0
	#define WORD_ADDR_XOR		0
	#define BYTE4_XOR_BE(a) 	(a)
#endif

#ifdef LSB_FIRST
#define BYTE_XOR_DWORD_SWAP 7
#define WORD_XOR_DWORD_SWAP 3
#else
#define BYTE_XOR_DWORD_SWAP 4
#define WORD_XOR_DWORD_SWAP 2
#endif
#define DWORD_XOR_DWORD_SWAP 1

#ifndef __LIBRETRO__
#define INLINE
#endif
#ifdef _MSC_VER
#define STRICTINLINE	__forceinline
#else
#define STRICTINLINE	static inline
#endif

#define rdram ((uint32_t*)gfx_al.RDRAM)
#define rsp_imem ((uint32_t*)gfx_al.IMEM)
#define rsp_dmem ((uint32_t*)gfx_al.DMEM)

#define rdram16 ((UINT16*)gfx_al.RDRAM)
#define rdram8 (gfx_al.RDRAM)

#define vi_origin (*(uint32_t*)gfx_al.VI_ORIGIN_REG)
#define vi_width (*(uint32_t*)gfx_al.VI_WIDTH_REG)
#define vi_control (*(uint32_t*)gfx_al.VI_STATUS_REG)
#define vi_v_sync (*(uint32_t*)gfx_al.VI_V_SYNC_REG)
#define vi_h_sync (*(uint32_t*)gfx_al.VI_H_SYNC_REG)
#define vi_h_start (*(uint32_t*)gfx_al.VI_H_START_REG)
#define vi_v_start (*(uint32_t*)gfx_al.VI_V_START_REG)
#define vi_v_intr (*(uint32_t*)gfx_al.VI_INTR_REG)
#define vi_x_scale (*(uint32_t*)gfx_al.VI_X_SCALE_REG)
#define vi_y_scale (*(uint32_t*)gfx_al.VI_Y_SCALE_REG)
#define vi_timing (*(uint32_t*)gfx_al.VI_TIMING_REG)
#define vi_v_current_line (*(uint32_t*)gfx_al.VI_V_CURRENT_LINE_REG)

#define dp_start (*(uint32_t*)gfx_al.DPC_START_REG)
#define dp_end (*(uint32_t*)gfx_al.DPC_END_REG)
#define dp_current (*(uint32_t*)gfx_al.DPC_CURRENT_REG)
#define dp_status (*(uint32_t*)gfx_al.DPC_STATUS_REG)


#endif
