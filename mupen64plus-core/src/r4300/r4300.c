/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - r4300.c                                                 *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "../ai/ai_controller.h"
#include "../api/m64p_config.h"
#include "../api/m64p_types.h"
#include "../api/callbacks.h"
#include "../api/debugger.h"
#include "../memory/memory.h"
#include "../main/main.h"
#include "../main/rom.h"
#include "../pi/pi_controller.h"
#include "../rsp/rsp_core.h"
#include "../si/si_controller.h"
#include "../vi/vi_controller.h"
#include "../dd/dd_rom.h"

#include "r4300.h"
#include "r4300_core.h"
#include "cached_interp.h"
#include "cp0_private.h"
#include "cp1_private.h"
#include "ops.h"
#include "interupt.h"
#include "macros.h"
#include "pure_interp.h"
#include "recomp.h"
#include "recomph.h"
#include "tlb.h"
#include "new_dynarec/new_dynarec.h"
#include "rsp/rsp_core.h"


#ifdef DBG
#include "debugger/dbg_types.h"
#include "debugger/debugger.h"
#endif

unsigned int r4300emu = 0;
unsigned int count_per_op = 2;
int rompause;
unsigned int llbit;
#if NEW_DYNAREC != NEW_DYNAREC_ARM
int stop;
int64_t reg[32], hi, lo;
uint32_t next_interupt;
precomp_instr *PC;
#endif
int64_t local_rs;
unsigned int delay_slot;
uint32_t skip_jump = 0;
unsigned int dyna_interp = 0;
uint32_t last_addr;

cpu_instruction_table current_instruction_table;

void generic_jump_to(uint32_t address)
{
   if (r4300emu == CORE_PURE_INTERPRETER)
      PC->addr = address;
   else
   {
#ifdef NEW_DYNAREC
      if (r4300emu == CORE_DYNAREC)
         last_addr = pcaddr;
      else
#endif
         jump_to(address);
   }
}

/* this hard reset function simulates the boot-up state of the R4300 CPU */
void r4300_reset_hard(void)
{
   unsigned int i;

   // clear r4300 registers and TLB entries
   for (i = 0; i < 32; i++)
   {
      reg[i]=0;
      g_cp0_regs[i]=0;
      reg_cop1_fgr_64[i]=0;

      // --------------tlb------------------------
      tlb_e[i].mask=0;
      tlb_e[i].vpn2=0;
      tlb_e[i].g=0;
      tlb_e[i].asid=0;
      tlb_e[i].pfn_even=0;
      tlb_e[i].c_even=0;
      tlb_e[i].d_even=0;
      tlb_e[i].v_even=0;
      tlb_e[i].pfn_odd=0;
      tlb_e[i].c_odd=0;
      tlb_e[i].d_odd=0;
      tlb_e[i].v_odd=0;
      tlb_e[i].r=0;
      //tlb_e[i].check_parity_mask=0x1000;

      tlb_e[i].start_even=0;
      tlb_e[i].end_even=0;
      tlb_e[i].phys_even=0;
      tlb_e[i].start_odd=0;
      tlb_e[i].end_odd=0;
      tlb_e[i].phys_odd=0;
   }
   for (i=0; i<0x100000; i++)
   {
      tlb_LUT_r[i] = 0;
      tlb_LUT_w[i] = 0;
   }
   llbit = 0;
   hi    = 0;
   lo    = 0;
   FCR0  = UINT32_C(0x511);
   FCR31 = 0;

   // set COP0 registers
   g_cp0_regs[CP0_RANDOM_REG]   = UINT32_C(31);
   g_cp0_regs[CP0_STATUS_REG]   = UINT32_C(0x34000000);
   set_fpr_pointers(g_cp0_regs[CP0_STATUS_REG]);
   g_cp0_regs[CP0_CONFIG_REG]   = UINT32_C(0x6e463);
   g_cp0_regs[CP0_PREVID_REG]   = UINT32_C(0xb00);
   g_cp0_regs[CP0_COUNT_REG]    = UINT32_C(0x5000);
   g_cp0_regs[CP0_CAUSE_REG]    = UINT32_C(0x5C);
   g_cp0_regs[CP0_CONTEXT_REG]  = UINT32_C(0x7FFFF0);
   g_cp0_regs[CP0_EPC_REG]      = UINT32_C(0xFFFFFFFF);
   g_cp0_regs[CP0_BADVADDR_REG] = UINT32_C(0xFFFFFFFF);
   g_cp0_regs[CP0_ERROREPC_REG] = UINT32_C(0xFFFFFFFF);

   update_x86_rounding_mode(FCR31);
}

static unsigned int get_tv_type(void)
{
   switch(ROM_PARAMS.systemtype)
   {
      default:
      case SYSTEM_NTSC:
         return 1;
      case SYSTEM_PAL:
         return 0;
      case SYSTEM_MPAL:
         return 2;
   }
}

/* Simulates end result of PIFBootROM execution */
void r4300_reset_soft(void)
{
   uint32_t bsd_dom1_config;
   unsigned int rom_type = 0; /* 0:Cart, 1:DD */
   unsigned int reset_type = 0; /* 0:ColdReset, 1:NMI */
   unsigned int s7 = 0; /* ??? */
   unsigned int tv_type = get_tv_type(); /* 0:PAL, 1:NTSC, 2:MPAL */

   if ((ConfigGetParamInt(g_CoreConfig, "BootDevice") != 0) && (g_ddrom != NULL) && (g_ddrom_size != 0))
   {
      bsd_dom1_config = *(uint32_t*)g_ddrom;
      rom_type = 1;
   }
   else
      bsd_dom1_config = *(uint32_t*)g_rom;

   g_cp0_regs[CP0_STATUS_REG] = 0x34000000;
   g_cp0_regs[CP0_CONFIG_REG] = 0x0006e463;
   g_sp.regs[SP_STATUS_REG]   = 1;
   g_sp.regs2[SP_PC_REG]      = 0;

   g_pi.regs[PI_BSD_DOM1_LAT_REG] = (bsd_dom1_config ) & 0xff;
   g_pi.regs[PI_BSD_DOM1_PWD_REG] = (bsd_dom1_config >> 8) & 0xff;
   g_pi.regs[PI_BSD_DOM1_PGS_REG] = (bsd_dom1_config >> 16) & 0x0f;
   g_pi.regs[PI_BSD_DOM1_RLS_REG] = (bsd_dom1_config >> 20) & 0x03;
   g_pi.regs[PI_STATUS_REG] = 0;

   g_ai.regs[AI_DRAM_ADDR_REG] = 0;
   g_ai.regs[AI_LEN_REG] = 0;

   g_vi.regs[VI_V_INTR_REG] = 1023;
   g_vi.regs[VI_CURRENT_REG] = 0;
   g_vi.regs[VI_H_START_REG] = 0;

   g_r4300.mi.regs[MI_INTR_REG] &= ~(MI_INTR_PI | MI_INTR_VI | MI_INTR_AI | MI_INTR_SP);

   if ((ConfigGetParamInt(g_CoreConfig, "BootDevice") != 0) && (g_ddrom != NULL) && (g_ddrom_size != 0))
      memcpy((unsigned char*)g_sp.mem+0x40, g_ddrom+0x40, 0xfc0);
   else
      memcpy((unsigned char*)g_sp.mem+0x40, g_rom+0x40, 0xfc0);

   reg[19] = rom_type; /* s3 */
   reg[20] = tv_type; /* s4 */
   reg[21] = reset_type; /* s5 */
   reg[22] = g_si.pif.cic.seed; /* s6 */
   reg[23] = s7; /* s7 */

   if ((ConfigGetParamInt(g_CoreConfig, "BootDevice") != 0) && (g_ddrom != NULL) && (g_ddrom_size != 0))
      reg[22] = 0xdd;

   /* required by CIC x105 */
   g_sp.mem[0x1000/4] = 0x3c0dbfc0;
   g_sp.mem[0x1004/4] = 0x8da807fc;
   g_sp.mem[0x1008/4] = 0x25ad07c0;
   g_sp.mem[0x100c/4] = 0x31080080;
   g_sp.mem[0x1010/4] = 0x5500fffc;
   g_sp.mem[0x1014/4] = 0x3c0dbfc0;
   g_sp.mem[0x1018/4] = 0x8da80024;
   g_sp.mem[0x101c/4] = 0x3c0bb000;

   /* required by CIC x105 */
   reg[11] = 0xffffffffa4000040ULL; /* t3 */
   reg[29] = 0xffffffffa4001ff0ULL; /* sp */
   reg[31] = 0xffffffffa4001550ULL; /* ra */
   /* ready to execute IPL3 */
}

#if !defined(NO_ASM) && defined(DYNAREC)
static void dynarec_setup_code(void)
{
   // The dynarec jumps here after we call dyna_start and it prepares
   // Here we need to prepare the initial code block and jump to it
   jump_to(UINT32_C(0xa4000040));

   // Prevent segfault on failed jump_to
   if (!actual->block || !actual->code)
      dyna_stop();
}
#endif

void r4300_init(void)
{
    current_instruction_table = cached_interpreter_table;

    delay_slot=0;
    stop = 0;
    rompause = 0;

    last_addr = 0xa4000040;
    next_interupt = 624999;
    init_interupt();

    if (r4300emu == CORE_PURE_INTERPRETER)
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Pure Interpreter");
        r4300emu = CORE_PURE_INTERPRETER;
        pure_interpreter_init();
    }
#if defined(DYNAREC)
    else if (r4300emu >= 2)
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Dynamic Recompiler");

        {
           r4300emu = CORE_DYNAREC;
           init_blocks();

#ifdef NEW_DYNAREC
           new_dynarec_init();
#elif !defined(NO_ASM) && defined(DYNAREC)
           dyna_start(dynarec_setup_code);
#endif
        }
    }
#endif
    else /* if (r4300emu == CORE_INTERPRETER) */
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Cached Interpreter");
        r4300emu = CORE_INTERPRETER;
        init_blocks();
        jump_to(UINT32_C(0xa4000040));

        /* Prevent segfault on failed jump_to */
        if (!actual->block)
            return;

        last_addr = PC->addr;
    }
}

void r4300_deinit(void)
{
    DebugMessage(M64MSG_INFO, "R4300 emulator finished.");

    if (r4300emu == CORE_PURE_INTERPRETER)
    {
    }
#if defined(DYNAREC)
    else if (r4300emu >= 2)
    {
#ifdef NEW_DYNAREC
          new_dynarec_cleanup();
          free_blocks();
#else
          PC++;
#endif
    }
#endif
    else /* if (r4300emu == CORE_INTERPRETER) */
    {
        free_blocks();
    }
}

static void (*r4300_step)(void);

#if defined(DYNAREC)
static void dyna_start_wrapper()
{
#ifdef NEW_DYNAREC
       new_dyna_start();
#else
       dyna_start(dyna_jump);
#endif
}
#endif

static void pc_ops_wrapper()
{
#ifdef COMPARE_CORE
           if (PC->ops == cached_interpreter_table.FIN_BLOCK && (PC->addr < 0x80000000 || PC->addr >= 0xc0000000))
               virtual_to_physical_address(PC->addr, TLB_FAST_READ);
           CoreCompareCallback();
#endif
#ifdef DBG
           if (g_DebuggerActive) update_debugger(PC->addr);
#endif
           PC->ops();
}

void r4300_execute(void)
{
   if (r4300emu == CORE_PURE_INTERPRETER)
      r4300_step = pure_interpreter;
   else if (r4300emu == CORE_INTERPRETER)
      r4300_step = pc_ops_wrapper;
#if defined(DYNAREC)
   else if (r4300emu >= 2)
      r4300_step = dyna_start_wrapper;
#endif

   while (!stop)
      r4300_step();
}
