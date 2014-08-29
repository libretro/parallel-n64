/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Cross-platform driver                     *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2013-2014 Nebuleon                                      *
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
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(ND_SHOW_COMPILATION) || defined(ND_SHOW_INTERPRETATION) || defined(ND_SHOW_OPS_PER_FUNCTION)
#  include <inttypes.h>
#endif

#if defined(__GNUC__)
#include <unistd.h>
#ifndef __MINGW32__
#include <sys/mman.h>
#endif
#endif

#include "../../api/m64p_types.h"
#include "../../api/callbacks.h"
#include "../../memory/memory.h"
#include "../../main/profile.h"

#include "../r4300.h"
#include "../cached_interp.h" /* get declarations for actual, invalid_code */
#include "../recomp.h"
#include "../ops.h"
#include "../cp0.h"
#include "../cp1.h"
#include "../macros.h"
#include "../exception.h"
#include "../interupt.h"
#include "../tlb.h"
#include "driver.h"
#include "n64ops.h"
#include "emitflags.h"
#include "branches.h"

#define PAGE_INSNS 1024
#define MAX_FPR_LAYOUT_CHANGES 8

static uint8_t* code_cache;

static uint8_t* next_code;

static uint_fast8_t fpr_layout_changes = 0;

static void *malloc_exec(size_t size);
static void free_exec(void *ptr, size_t len);

#ifdef ND_SHOW_OPS_PER_FUNCTION
static uint64_t ops_per_func[PAGE_INSNS + 1 + (PAGE_INSNS >> 2)];
#endif

static void visit_valid_code(const uint32_t* source, const uint32_t start, precomp_block* page);
static void nd_recompile(const uint32_t* source, const uint32_t start, precomp_block* page);

/* - - - THE NEB DYNAREC'S CACHED INTERPRETER FALLBACK STARTS HERE - - - */

#ifdef DBG
#define UPDATE_DEBUGGER() if (g_DebuggerActive) update_debugger(PC->addr)
#else
#define UPDATE_DEBUGGER() do { } while(0)
#endif

#define PCADDR PC->addr
#define ADD_TO_PC(x) PC += x;
#define DECLARE_INSTRUCTION(name) static void name(void)

#define DECLARE_JUMP(name, destination, condition, link, likely, cop1) \
	static void name(void) \
	{ \
		const int take_jump = (condition); \
		const uint32_t jump_target = (destination); \
		long long int* link_register = (link); \
		if (cop1 && check_cop1_unusable()) return; \
		if (link_register != &reg[0]) \
		{ \
			*link_register=PC->addr + 8; \
			sign_extended(*link_register); \
		} \
		if (!likely || take_jump) \
		{ \
			PC++; \
			delay_slot=1; \
			UPDATE_DEBUGGER(); \
			PC->ops(); \
			if (skip_jump) \
			{ \
				/* JITed code can get out as soon as possible. All it needs \
				 * to do is unset skip_jump, like this, and save regs. */ \
				skip_jump = 0; \
				return; \
			} \
			update_count(); \
			delay_slot = 0; \
			if (take_jump /* && !skip_jump */) \
			{ \
				PC=actual->block+((jump_target-actual->start)>>2); \
			} \
		} \
		else \
		{ \
			PC += 2; \
			update_count(); \
		} \
		last_addr = PC->addr; \
		if (next_interupt <= g_cp0_regs[CP0_COUNT_REG]) gen_interupt(); \
	} \
	static void name##_OUT(void) \
	{ \
		const int take_jump = (condition); \
		const uint32_t jump_target = (destination); \
		long long int* link_register = (link); \
		if (cop1 && check_cop1_unusable()) return; \
		if (link_register != &reg[0]) \
		{ \
			*link_register=PC->addr + 8; \
			sign_extended(*link_register); \
		} \
		if (!likely || take_jump) \
		{ \
			PC++; \
			delay_slot=1; \
			UPDATE_DEBUGGER(); \
			PC->ops(); \
			if (skip_jump) \
			{ \
				/* JITed code can get out as soon as possible. All it needs \
				 * to do is unset skip_jump, like this, and save regs. */ \
				skip_jump = 0; \
				return; \
			} \
			update_count(); \
			delay_slot = 0; \
			if (take_jump /* && !skip_jump */) \
			{ \
				jump_to(jump_target); \
			} \
		} \
		else \
		{ \
			PC += 2; \
			update_count(); \
		} \
		last_addr = PC->addr; \
		if (next_interupt <= g_cp0_regs[CP0_COUNT_REG]) gen_interupt(); \
	} \
	static void name##_IDLE(void) \
	{ \
		const int take_jump = (condition); \
		unsigned int skip; \
		if (cop1 && check_cop1_unusable()) return; \
		if (take_jump) \
		{ \
			update_count(); \
			skip = next_interupt - g_cp0_regs[CP0_COUNT_REG]; \
			if (skip > 3) g_cp0_regs[CP0_COUNT_REG] += (skip & 0xFFFFFFFC); \
			else name(); \
		} \
		else name(); \
	}

#define CHECK_MEMORY() \
	if (!invalid_code[address>>12]) \
		if (blocks[address>>12]->block[(address&0xFFF)/4].ops != \
			current_instruction_table.NOTCOMPILED) \
			invalid_code[address>>12] = 1;

#include "../interpreter.def"

// two functions are defined from the macros above but never used
// these prototype declarations will prevent a warning
#if defined(__GNUC__)
	void JR_IDLE(void) __attribute__((used));
	void JALR_IDLE(void) __attribute__((used));
#endif

// -----------------------------------------------------------
// Flow control 'fake' instructions
// -----------------------------------------------------------
static void WRAP_PAGE(void)
{
	if (!delay_slot)
	{
		jump_to((PC - 1)->addr + 4);
/*#ifdef DBG
		if (g_DebuggerActive) update_debugger(PC->addr);
#endif
Used by dynarec only, check should be unnecessary
*/
		PC->ops();
	}
	else
	{
		precomp_block *blk = actual;
		precomp_instr *inst = PC;
		jump_to((PC - 1)->addr + 4);

/*#ifdef DBG
		if (g_DebuggerActive) update_debugger(PC->addr);
#endif
Used by dynarec only, check should be unnecessary
*/
		if (!skip_jump)
		{
			PC->ops();
			actual = blk;
			PC = inst + 1;
		}
		else
			PC->ops();
	}
}

static void NOTCOMPILED(void)
{
	unsigned int* mem = fast_mem_access(actual->start);
#ifdef CORE_DBG
	DebugMessage(M64MSG_INFO, "NOTCOMPILED: addr = %x ops = %lx", PC->addr, (long) PC->ops);
#endif

	if (mem != NULL)
	{
		visit_valid_code((uint32_t*) mem, (PC->addr - actual->start) / 4, actual);
		nd_recompile((uint32_t*) mem, (PC->addr - actual->start) / 4, actual);
	}
	else
		DebugMessage(M64MSG_ERROR, "not compiled exception");

/*#ifdef DBG
            if (g_DebuggerActive) update_debugger(PC->addr);
#endif
The preceeding update_debugger SHOULD be unnecessary since it should have been
called before NOTCOMPILED would have been executed
*/
	PC->ops();
}

static void NOTCOMPILED2(void)
{
	unsigned int* mem = fast_mem_access(actual->start);
#ifdef CORE_DBG
	DebugMessage(M64MSG_INFO, "NOTCOMPILED: addr = %x ops = %lx", PC->addr, (long) PC->ops);
#endif

	if (mem != NULL)
	{
		nd_recompile((uint32_t*) mem, (PC->addr - actual->start) / 4, actual);
	}
	else
		DebugMessage(M64MSG_ERROR, "not compiled exception");

/*#ifdef DBG
            if (g_DebuggerActive) update_debugger(PC->addr);
#endif
The preceeding update_debugger SHOULD be unnecessary since it should have been
called before NOTCOMPILED would have been executed
*/
	PC->ops();
}

const cpu_instruction_table neb_fallback_table = {
	LB,
	LBU,
	LH,
	LHU,
	LW,
	LWL,
	LWR,
	SB,
	SH,
	SW,
	SWL,
	SWR,

	LD,
	LDL,
	LDR,
	LL,
	LWU,
	SC,
	SD,
	SDL,
	SDR,
	SYNC,

	ADDI,
	ADDIU,
	SLTI,
	SLTIU,
	ANDI,
	ORI,
	XORI,
	LUI,

	DADDI,
	DADDIU,

	ADD,
	ADDU,
	SUB,
	SUBU,
	SLT,
	SLTU,
	AND,
	OR,
	XOR,
	NOR,

	DADD,
	DADDU,
	DSUB,
	DSUBU,

	MULT,
	MULTU,
	DIV,
	DIVU,
	MFHI,
	MTHI,
	MFLO,
	MTLO,

	DMULT,
	DMULTU,
	DDIV,
	DDIVU,

	J,
	J_OUT,
	J_IDLE,
	JAL,
	JAL_OUT,
	JAL_IDLE,
	// Use the _OUT versions of JR and JALR, since we don't know
	// until runtime if they're going to jump inside or outside the block
	JR_OUT,
	JALR_OUT,
	BEQ,
	BEQ_OUT,
	BEQ_IDLE,
	BNE,
	BNE_OUT,
	BNE_IDLE,
	BLEZ,
	BLEZ_OUT,
	BLEZ_IDLE,
	BGTZ,
	BGTZ_OUT,
	BGTZ_IDLE,
	BLTZ,
	BLTZ_OUT,
	BLTZ_IDLE,
	BGEZ,
	BGEZ_OUT,
	BGEZ_IDLE,
	BLTZAL,
	BLTZAL_OUT,
	BLTZAL_IDLE,
	BGEZAL,
	BGEZAL_OUT,
	BGEZAL_IDLE,

	BEQL,
	BEQL_OUT,
	BEQL_IDLE,
	BNEL,
	BNEL_OUT,
	BNEL_IDLE,
	BLEZL,
	BLEZL_OUT,
	BLEZL_IDLE,
	BGTZL,
	BGTZL_OUT,
	BGTZL_IDLE,
	BLTZL,
	BLTZL_OUT,
	BLTZL_IDLE,
	BGEZL,
	BGEZL_OUT,
	BGEZL_IDLE,
	BLTZALL,
	BLTZALL_OUT,
	BLTZALL_IDLE,
	BGEZALL,
	BGEZALL_OUT,
	BGEZALL_IDLE,
	BC1TL,
	BC1TL_OUT,
	BC1TL_IDLE,
	BC1FL,
	BC1FL_OUT,
	BC1FL_IDLE,

	SLL,
	SRL,
	SRA,
	SLLV,
	SRLV,
	SRAV,

	DSLL,
	DSRL,
	DSRA,
	DSLLV,
	DSRLV,
	DSRAV,
	DSLL32,
	DSRL32,
	DSRA32,

	MTC0,
	MFC0,

	TLBR,
	TLBWI,
	TLBWR,
	TLBP,
	CACHE,
	ERET,

	LWC1,
	SWC1,
	MTC1,
	MFC1,
	CTC1,
	CFC1,
	BC1T,
	BC1T_OUT,
	BC1T_IDLE,
	BC1F,
	BC1F_OUT,
	BC1F_IDLE,

	DMFC1,
	DMTC1,
	LDC1,
	SDC1,

	CVT_S_D,
	CVT_S_W,
	CVT_S_L,
	CVT_D_S,
	CVT_D_W,
	CVT_D_L,
	CVT_W_S,
	CVT_W_D,
	CVT_L_S,
	CVT_L_D,

	ROUND_W_S,
	ROUND_W_D,
	ROUND_L_S,
	ROUND_L_D,

	TRUNC_W_S,
	TRUNC_W_D,
	TRUNC_L_S,
	TRUNC_L_D,

	CEIL_W_S,
	CEIL_W_D,
	CEIL_L_S,
	CEIL_L_D,

	FLOOR_W_S,
	FLOOR_W_D,
	FLOOR_L_S,
	FLOOR_L_D,

	ADD_S,
	ADD_D,

	SUB_S,
	SUB_D,

	MUL_S,
	MUL_D,

	DIV_S,
	DIV_D,

	ABS_S,
	ABS_D,

	MOV_S,
	MOV_D,

	NEG_S,
	NEG_D,

	SQRT_S,
	SQRT_D,

	C_F_S,
	C_F_D,
	C_UN_S,
	C_UN_D,
	C_EQ_S,
	C_EQ_D,
	C_UEQ_S,
	C_UEQ_D,
	C_OLT_S,
	C_OLT_D,
	C_ULT_S,
	C_ULT_D,
	C_OLE_S,
	C_OLE_D,
	C_ULE_S,
	C_ULE_D,
	C_SF_S,
	C_SF_D,
	C_NGLE_S,
	C_NGLE_D,
	C_SEQ_S,
	C_SEQ_D,
	C_NGL_S,
	C_NGL_D,
	C_LT_S,
	C_LT_D,
	C_NGE_S,
	C_NGE_D,
	C_LE_S,
	C_LE_D,
	C_NGT_S,
	C_NGT_D,

	SYSCALL,

	TEQ,

	NOP,
	RESERVED,
	NI,

	WRAP_PAGE,
	NOTCOMPILED,
	NOTCOMPILED2
};

/* - - - THE NEB DYNAREC'S CACHED INTERPRETER FALLBACK ENDS HERE - - - */

void nd_init()
{
	code_cache = malloc_exec(CODE_CACHE_SIZE);
	if (code_cache == NULL)
	{
		DebugMessage(M64MSG_ERROR, "Memory error: Couldn't allocate memory for the dynamic recompiler's code cache.");
	}
	next_code = code_cache;
#ifdef ARCH_NEED_INIT
	nd_arch_init();
#endif
}

void nd_exit()
{
#ifdef ARCH_NEED_EXIT
	nd_arch_exit();
#endif
	free_exec(code_cache, CODE_CACHE_SIZE);
#ifdef ND_SHOW_OPS_PER_FUNCTION
	unsigned int i, max;
	for (max = sizeof(ops_per_func) / sizeof(ops_per_func[0]) - 1; max > 0; max--)
	{
		if (ops_per_func[max] != 0)
			break;
	}
	max++;
	DebugMessage(M64MSG_INFO, "Block emission statistics");
	for (i = 1; i < max; i++)
		DebugMessage(M64MSG_INFO, "%4u opcodes: %19" PRIu64, i, ops_per_func[i]);
#endif
}

static void convert_i(const n64_insn_t* insn)
{
	insn->runtime->f.i.rs = &reg[insn->rs];
	insn->runtime->f.i.rt = &reg[insn->rt];
	insn->runtime->f.i.immediate = insn->imm;
}

static void convert_j(const n64_insn_t* insn)
{
	insn->runtime->f.j.inst_index = insn->imm;
}

static void convert_r(const n64_insn_t* insn)
{
	insn->runtime->f.r.rs = &reg[insn->rs];
	insn->runtime->f.r.rt = &reg[insn->rt];
	insn->runtime->f.r.rd = &reg[insn->rd];
	insn->runtime->f.r.sa = insn->imm;
}

static void convert_lf(const n64_insn_t* insn)
{
	insn->runtime->f.lf.base = insn->rs;
	insn->runtime->f.lf.ft = insn->rt;
	insn->runtime->f.lf.offset = insn->imm;
}

static void convert_cf(const n64_insn_t* insn)
{
	insn->runtime->f.cf.ft = insn->rt;
	insn->runtime->f.cf.fs = insn->rs;
	insn->runtime->f.cf.fd = insn->rd;
}

static void convert_mc0(const n64_insn_t* insn)
{
	insn->runtime->f.r.rt = &reg[insn->rt];
	insn->runtime->f.r.nrd = insn->rd;
	insn->runtime->f.r.rd = (long long int*) &g_cp0_regs[insn->rd];
}

static void convert_mc1(const n64_insn_t* insn)
{
	insn->runtime->f.r.rt = &reg[insn->rt];
	insn->runtime->f.r.nrd = insn->rs;
}

static struct {
	void (*op) (void);
	void (*operand_conv) (const n64_insn_t*);
} opcodes[N64_OP_MAX] = {
	[N64_OP_NOP]           = { NOP,          NULL        },
	[N64_OP_RESERVED]      = { RESERVED,     NULL        },
	[N64_OP_UNIMPLEMENTED] = { NI,           NULL        },
	[N64_OP_SLL]           = { SLL,          convert_r   },
	[N64_OP_SRL]           = { SRL,          convert_r   },
	[N64_OP_SRA]           = { SRA,          convert_r   },
	[N64_OP_DSLL]          = { DSLL,         convert_r   },
	[N64_OP_DSRL]          = { DSRL,         convert_r   },
	[N64_OP_DSRA]          = { DSRA,         convert_r   },
	[N64_OP_DSLL32]        = { DSLL32,       convert_r   },
	[N64_OP_DSRL32]        = { DSRL32,       convert_r   },
	[N64_OP_DSRA32]        = { DSRA32,       convert_r   },
	[N64_OP_SLLV]          = { SLLV,         convert_r   },
	[N64_OP_SRLV]          = { SRLV,         convert_r   },
	[N64_OP_SRAV]          = { SRAV,         convert_r   },
	[N64_OP_DSLLV]         = { DSLLV,        convert_r   },
	[N64_OP_DSRLV]         = { DSRLV,        convert_r   },
	[N64_OP_DSRAV]         = { DSRAV,        convert_r   },
	[N64_OP_ADD]           = { ADD,          convert_r   },
	[N64_OP_ADDU]          = { ADDU,         convert_r   },
	[N64_OP_SUB]           = { SUB,          convert_r   },
	[N64_OP_SUBU]          = { SUBU,         convert_r   },
	[N64_OP_AND]           = { AND,          convert_r   },
	[N64_OP_OR]            = { OR,           convert_r   },
	[N64_OP_XOR]           = { XOR,          convert_r   },
	[N64_OP_NOR]           = { NOR,          convert_r   },
	[N64_OP_SLT]           = { SLT,          convert_r   },
	[N64_OP_SLTU]          = { SLTU,         convert_r   },
	[N64_OP_DADD]          = { DADD,         convert_r   },
	[N64_OP_DADDU]         = { DADDU,        convert_r   },
	[N64_OP_DSUB]          = { DSUB,         convert_r   },
	[N64_OP_DSUBU]         = { DSUBU,        convert_r   },
	[N64_OP_ADDI]          = { ADDI,         convert_i   },
	[N64_OP_ADDIU]         = { ADDIU,        convert_i   },
	[N64_OP_SLTI]          = { SLTI,         convert_i   },
	[N64_OP_SLTIU]         = { SLTIU,        convert_i   },
	[N64_OP_ANDI]          = { ANDI,         convert_i   },
	[N64_OP_ORI]           = { ORI,          convert_i   },
	[N64_OP_XORI]          = { XORI,         convert_i   },
	[N64_OP_LUI]           = { LUI,          convert_i   },
	[N64_OP_DADDI]         = { DADDI,        convert_i   },
	[N64_OP_DADDIU]        = { DADDIU,       convert_i   },
	[N64_OP_LDL]           = { LDL,          convert_i   },
	[N64_OP_LDR]           = { LDR,          convert_i   },
	[N64_OP_LB]            = { LB,           convert_i   },
	[N64_OP_LH]            = { LH,           convert_i   },
	[N64_OP_LWL]           = { LWL,          convert_i   },
	[N64_OP_LW]            = { LW,           convert_i   },
	[N64_OP_LBU]           = { LBU,          convert_i   },
	[N64_OP_LHU]           = { LHU,          convert_i   },
	[N64_OP_LWR]           = { LWR,          convert_i   },
	[N64_OP_LWU]           = { LWU,          convert_i   },
	[N64_OP_LL]            = { LL,           convert_i   },
	[N64_OP_LLD]           = { NI,           NULL        },
	[N64_OP_LD]            = { LD,           convert_i   },
	[N64_OP_SB]            = { SB,           convert_i   },
	[N64_OP_SH]            = { SH,           convert_i   },
	[N64_OP_SWL]           = { SWL,          convert_i   },
	[N64_OP_SW]            = { SW,           convert_i   },
	[N64_OP_SDL]           = { SDL,          convert_i   },
	[N64_OP_SDR]           = { SDR,          convert_i   },
	[N64_OP_SWR]           = { SWR,          convert_i   },
	[N64_OP_SC]            = { SC,           convert_i   },
	[N64_OP_SCD]           = { NI,           NULL        },
	[N64_OP_SD]            = { SD,           convert_i   },
	[N64_OP_MFHI]          = { MFHI,         convert_r   },
	[N64_OP_MFLO]          = { MFLO,         convert_r   },
	[N64_OP_MTHI]          = { MTHI,         convert_r   },
	[N64_OP_MTLO]          = { MTLO,         convert_r   },
	[N64_OP_MULT]          = { MULT,         convert_r   },
	[N64_OP_MULTU]         = { MULTU,        convert_r   },
	[N64_OP_DIV]           = { DIV,          convert_r   },
	[N64_OP_DIVU]          = { DIVU,         convert_r   },
	[N64_OP_DMULT]         = { DMULT,        convert_r   },
	[N64_OP_DMULTU]        = { DMULTU,       convert_r   },
	[N64_OP_DDIV]          = { DDIV,         convert_r   },
	[N64_OP_DDIVU]         = { DDIVU,        convert_r   },
	[N64_OP_J]             = { J,            convert_j   },
	[N64_OP_J_OUT]         = { J_OUT,        convert_j   },
	[N64_OP_J_IDLE]        = { J_IDLE,       convert_j   },
	[N64_OP_JAL]           = { JAL,          convert_j   },
	[N64_OP_JAL_OUT]       = { JAL_OUT,      convert_j   },
	[N64_OP_JAL_IDLE]      = { JAL_IDLE,     convert_j   },
	[N64_OP_JR]            = { JR_OUT,       convert_i   },
	[N64_OP_JALR]          = { JALR_OUT,     convert_r   },
	[N64_OP_SYSCALL]       = { SYSCALL,      NULL        },
	[N64_OP_ERET]          = { ERET,         NULL        },
	[N64_OP_TGE]           = { NI,           NULL        },
	[N64_OP_TGEU]          = { NI,           NULL        },
	[N64_OP_TLT]           = { NI,           NULL        },
	[N64_OP_TLTU]          = { NI,           NULL        },
	[N64_OP_TEQ]           = { TEQ,          convert_r   },
	[N64_OP_TNE]           = { NI,           NULL        },
	[N64_OP_TGEI]          = { NI,           NULL        },
	[N64_OP_TGEIU]         = { NI,           NULL        },
	[N64_OP_TLTI]          = { NI,           NULL        },
	[N64_OP_TLTIU]         = { NI,           NULL        },
	[N64_OP_TEQI]          = { NI,           NULL        },
	[N64_OP_TNEI]          = { NI,           NULL        },
	[N64_OP_BLTZ]          = { BLTZ,         convert_i   },
	[N64_OP_BLTZ_OUT]      = { BLTZ_OUT,     convert_i   },
	[N64_OP_BLTZ_IDLE]     = { BLTZ_IDLE,    convert_i   },
	[N64_OP_BLTZL]         = { BLTZL,        convert_i   },
	[N64_OP_BLTZL_OUT]     = { BLTZL_OUT,    convert_i   },
	[N64_OP_BLTZL_IDLE]    = { BLTZL_IDLE,   convert_i   },
	[N64_OP_BGEZ]          = { BGEZ,         convert_i   },
	[N64_OP_BGEZ_OUT]      = { BGEZ_OUT,     convert_i   },
	[N64_OP_BGEZ_IDLE]     = { BGEZ_IDLE,    convert_i   },
	[N64_OP_BGEZL]         = { BGEZL,        convert_i   },
	[N64_OP_BGEZL_OUT]     = { BGEZL_OUT,    convert_i   },
	[N64_OP_BGEZL_IDLE]    = { BGEZL_IDLE,   convert_i   },
	[N64_OP_BLTZAL]        = { BLTZAL,       convert_i   },
	[N64_OP_BLTZAL_OUT]    = { BLTZAL_OUT,   convert_i   },
	[N64_OP_BLTZAL_IDLE]   = { BLTZAL_IDLE,  convert_i   },
	[N64_OP_BLTZALL]       = { BLTZALL,      convert_i   },
	[N64_OP_BLTZALL_OUT]   = { BLTZALL_OUT,  convert_i   },
	[N64_OP_BLTZALL_IDLE]  = { BLTZALL_IDLE, convert_i   },
	[N64_OP_BGEZAL]        = { BGEZAL,       convert_i   },
	[N64_OP_BGEZAL_OUT]    = { BGEZAL_OUT,   convert_i   },
	[N64_OP_BGEZAL_IDLE]   = { BGEZAL_IDLE,  convert_i   },
	[N64_OP_BGEZALL]       = { BGEZALL,      convert_i   },
	[N64_OP_BGEZALL_OUT]   = { BGEZALL_OUT,  convert_i   },
	[N64_OP_BGEZALL_IDLE]  = { BGEZALL_IDLE, convert_i   },
	[N64_OP_BLEZ]          = { BLEZ,         convert_i   },
	[N64_OP_BLEZ_OUT]      = { BLEZ_OUT,     convert_i   },
	[N64_OP_BLEZ_IDLE]     = { BLEZ_IDLE,    convert_i   },
	[N64_OP_BLEZL]         = { BLEZL,        convert_i   },
	[N64_OP_BLEZL_OUT]     = { BLEZL_OUT,    convert_i   },
	[N64_OP_BLEZL_IDLE]    = { BLEZL_IDLE,   convert_i   },
	[N64_OP_BGTZ]          = { BGTZ,         convert_i   },
	[N64_OP_BGTZ_OUT]      = { BGTZ_OUT,     convert_i   },
	[N64_OP_BGTZ_IDLE]     = { BGTZ_IDLE,    convert_i   },
	[N64_OP_BGTZL]         = { BGTZL,        convert_i   },
	[N64_OP_BGTZL_OUT]     = { BGTZL_OUT,    convert_i   },
	[N64_OP_BGTZL_IDLE]    = { BGTZL_IDLE,   convert_i   },
	[N64_OP_BEQ]           = { BEQ,          convert_i   },
	[N64_OP_BEQ_OUT]       = { BEQ_OUT,      convert_i   },
	[N64_OP_BEQ_IDLE]      = { BEQ_IDLE,     convert_i   },
	[N64_OP_BEQL]          = { BEQL,         convert_i   },
	[N64_OP_BEQL_OUT]      = { BEQL_OUT,     convert_i   },
	[N64_OP_BEQL_IDLE]     = { BEQL_IDLE,    convert_i   },
	[N64_OP_BNE]           = { BNE,          convert_i   },
	[N64_OP_BNE_OUT]       = { BNE_OUT,      convert_i   },
	[N64_OP_BNE_IDLE]      = { BNE_IDLE,     convert_i   },
	[N64_OP_BNEL]          = { BNEL,         convert_i   },
	[N64_OP_BNEL_OUT]      = { BNEL_OUT,     convert_i   },
	[N64_OP_BNEL_IDLE]     = { BNEL_IDLE,    convert_i   },
	[N64_OP_SYNC]          = { SYNC,         NULL        },
	[N64_OP_CACHE]         = { CACHE,        NULL        },
	[N64_OP_MFC0]          = { MFC0,         convert_mc0 },
	[N64_OP_MTC0]          = { MTC0,         convert_mc0 },
	[N64_OP_TLBR]          = { TLBR,         NULL        },
	[N64_OP_TLBWI]         = { TLBWI,        NULL        },
	[N64_OP_TLBWR]         = { TLBWR,        NULL        },
	[N64_OP_TLBP]          = { TLBP,         NULL        },
	[N64_OP_ABS_S]         = { ABS_S,        convert_cf  },
	[N64_OP_MOV_S]         = { MOV_S,        convert_cf  },
	[N64_OP_NEG_S]         = { NEG_S,        convert_cf  },
	[N64_OP_ROUND_W_S]     = { ROUND_W_S,    convert_cf  },
	[N64_OP_TRUNC_W_S]     = { TRUNC_W_S,    convert_cf  },
	[N64_OP_CEIL_W_S]      = { CEIL_W_S,     convert_cf  },
	[N64_OP_FLOOR_W_S]     = { FLOOR_W_S,    convert_cf  },
	[N64_OP_CVT_W_S]       = { CVT_W_S,      convert_cf  },
	[N64_OP_CVT_S_W]       = { CVT_S_W,      convert_cf  },
	[N64_OP_ADD_S]         = { ADD_S,        convert_cf  },
	[N64_OP_SUB_S]         = { SUB_S,        convert_cf  },
	[N64_OP_MUL_S]         = { MUL_S,        convert_cf  },
	[N64_OP_DIV_S]         = { DIV_S,        convert_cf  },
	[N64_OP_SQRT_S]        = { SQRT_S,       convert_cf  },
	[N64_OP_ROUND_L_S]     = { ROUND_L_S,    convert_cf  },
	[N64_OP_TRUNC_L_S]     = { TRUNC_L_S,    convert_cf  },
	[N64_OP_CEIL_L_S]      = { CEIL_L_S,     convert_cf  },
	[N64_OP_FLOOR_L_S]     = { FLOOR_L_S,    convert_cf  },
	[N64_OP_CVT_D_S]       = { CVT_D_S,      convert_cf  },
	[N64_OP_CVT_L_S]       = { CVT_L_S,      convert_cf  },
	[N64_OP_CVT_D_W]       = { CVT_D_W,      convert_cf  },
	[N64_OP_C_F_S]         = { C_F_S,        NULL        },
	[N64_OP_C_UN_S]        = { C_UN_S,       convert_cf  },
	[N64_OP_C_EQ_S]        = { C_EQ_S,       convert_cf  },
	[N64_OP_C_UEQ_S]       = { C_UEQ_S,      convert_cf  },
	[N64_OP_C_OLT_S]       = { C_OLT_S,      convert_cf  },
	[N64_OP_C_ULT_S]       = { C_ULT_S,      convert_cf  },
	[N64_OP_C_OLE_S]       = { C_OLE_S,      convert_cf  },
	[N64_OP_C_ULE_S]       = { C_ULE_S,      convert_cf  },
	[N64_OP_C_SF_S]        = { C_SF_S,       convert_cf  },
	[N64_OP_C_NGLE_S]      = { C_NGLE_S,     convert_cf  },
	[N64_OP_C_SEQ_S]       = { C_SEQ_S,      convert_cf  },
	[N64_OP_C_NGL_S]       = { C_NGL_S,      convert_cf  },
	[N64_OP_C_LT_S]        = { C_LT_S,       convert_cf  },
	[N64_OP_C_NGE_S]       = { C_NGE_S,      convert_cf  },
	[N64_OP_C_LE_S]        = { C_LE_S,       convert_cf  },
	[N64_OP_C_NGT_S]       = { C_NGT_S,      convert_cf  },
	[N64_OP_ABS_D]         = { ABS_D,        convert_cf  },
	[N64_OP_MOV_D]         = { MOV_D,        convert_cf  },
	[N64_OP_NEG_D]         = { NEG_D,        convert_cf  },
	[N64_OP_ROUND_L_D]     = { ROUND_L_D,    convert_cf  },
	[N64_OP_TRUNC_L_D]     = { TRUNC_L_D,    convert_cf  },
	[N64_OP_CEIL_L_D]      = { CEIL_L_D,     convert_cf  },
	[N64_OP_FLOOR_L_D]     = { FLOOR_L_D,    convert_cf  },
	[N64_OP_CVT_L_D]       = { CVT_L_D,      convert_cf  },
	[N64_OP_CVT_D_L]       = { CVT_D_L,      convert_cf  },
	[N64_OP_ADD_D]         = { ADD_D,        convert_cf  },
	[N64_OP_SUB_D]         = { SUB_D,        convert_cf  },
	[N64_OP_MUL_D]         = { MUL_D,        convert_cf  },
	[N64_OP_DIV_D]         = { DIV_D,        convert_cf  },
	[N64_OP_SQRT_D]        = { SQRT_D,       convert_cf  },
	[N64_OP_ROUND_W_D]     = { ROUND_W_D,    convert_cf  },
	[N64_OP_TRUNC_W_D]     = { TRUNC_W_D,    convert_cf  },
	[N64_OP_CEIL_W_D]      = { CEIL_W_D,     convert_cf  },
	[N64_OP_FLOOR_W_D]     = { FLOOR_W_D,    convert_cf  },
	[N64_OP_CVT_S_D]       = { CVT_S_D,      convert_cf  },
	[N64_OP_CVT_W_D]       = { CVT_W_D,      convert_cf  },
	[N64_OP_CVT_S_L]       = { CVT_S_L,      convert_cf  },
	[N64_OP_C_F_D]         = { C_F_D,        NULL        },
	[N64_OP_C_UN_D]        = { C_UN_D,       convert_cf  },
	[N64_OP_C_EQ_D]        = { C_EQ_D,       convert_cf  },
	[N64_OP_C_UEQ_D]       = { C_UEQ_D,      convert_cf  },
	[N64_OP_C_OLT_D]       = { C_OLT_D,      convert_cf  },
	[N64_OP_C_ULT_D]       = { C_ULT_D,      convert_cf  },
	[N64_OP_C_OLE_D]       = { C_OLE_D,      convert_cf  },
	[N64_OP_C_ULE_D]       = { C_ULE_D,      convert_cf  },
	[N64_OP_C_SF_D]        = { C_SF_D,       convert_cf  },
	[N64_OP_C_NGLE_D]      = { C_NGLE_D,     convert_cf  },
	[N64_OP_C_SEQ_D]       = { C_SEQ_D,      convert_cf  },
	[N64_OP_C_NGL_D]       = { C_NGL_D,      convert_cf  },
	[N64_OP_C_LT_D]        = { C_LT_D,       convert_cf  },
	[N64_OP_C_NGE_D]       = { C_NGE_D,      convert_cf  },
	[N64_OP_C_LE_D]        = { C_LE_D,       convert_cf  },
	[N64_OP_C_NGT_D]       = { C_NGT_D,      convert_cf  },
	[N64_OP_MFC1]          = { MFC1,         convert_mc1 },
	[N64_OP_DMFC1]         = { DMFC1,        convert_mc1 },
	[N64_OP_CFC1]          = { CFC1,         convert_mc1 },
	[N64_OP_MTC1]          = { MTC1,         convert_mc1 },
	[N64_OP_DMTC1]         = { DMTC1,        convert_mc1 },
	[N64_OP_CTC1]          = { CTC1,         convert_mc1 },
	[N64_OP_LWC1]          = { LWC1,         convert_lf  },
	[N64_OP_LDC1]          = { LDC1,         convert_lf  },
	[N64_OP_SWC1]          = { SWC1,         convert_lf  },
	[N64_OP_SDC1]          = { SDC1,         convert_lf  },
	[N64_OP_BC1F]          = { BC1F,         convert_i   },
	[N64_OP_BC1F_OUT]      = { BC1F_OUT,     convert_i   },
	[N64_OP_BC1F_IDLE]     = { BC1F_IDLE,    convert_i   },
	[N64_OP_BC1FL]         = { BC1FL,        convert_i   },
	[N64_OP_BC1FL_OUT]     = { BC1FL_OUT,    convert_i   },
	[N64_OP_BC1FL_IDLE]    = { BC1FL_IDLE,   convert_i   },
	[N64_OP_BC1T]          = { BC1T,         convert_i   },
	[N64_OP_BC1T_OUT]      = { BC1T_OUT,     convert_i   },
	[N64_OP_BC1T_IDLE]     = { BC1T_IDLE,    convert_i   },
	[N64_OP_BC1TL]         = { BC1TL,        convert_i   },
	[N64_OP_BC1TL_OUT]     = { BC1TL_OUT,    convert_i   },
	[N64_OP_BC1TL_IDLE]    = { BC1TL_IDLE,   convert_i   },
};

/*
 * For instructions using the interpreter fallback, transfers the analysed
 * N64 instruction data from an instruction structure into its corresponding
 * 'struct precomp_instr'.
 */
static void fill_runtime_info(const n64_insn_t* insn)
{
	insn->runtime->addr = insn->addr;

	if (opcodes[insn->opcode].op)
		insn->runtime->ops = opcodes[insn->opcode].op;
	if (opcodes[insn->opcode].operand_conv)
		(*opcodes[insn->opcode].operand_conv) (insn);
}

static void fill_recompilation_info(n64_insn_t* insn)
{
	/* TODO Fill this out */
#if 0
	switch (insn->opcode)
	{
		default:                   insn->RecompFunc = NULL;             break;
	}
#endif
}

static int get_page_len(const precomp_block* page)
{
	return (page->end-page->start)/4;
}

static size_t get_page_bytes(const precomp_block* page)
{
	int len = get_page_len(page);
	return ((len+1)+(len>>2)) * sizeof(precomp_instr);
}

/**********************************************************************
 ********************* initialize an empty page ***********************
 **********************************************************************/
void nd_init_page(precomp_block* page)
{
	int i, len;
	timed_section_start(TIMED_SECTION_COMPILER);
#ifdef CORE_DBG
	DebugMessage(M64MSG_INFO, "init page %x - %x", (int) block->start, (int) block->end);
#endif

	page->adler32 = 0;

	len = get_page_len(page);

	if (!page->block)
	{
		size_t memsize = get_page_bytes(page);
		page->block = (precomp_instr*) calloc(memsize, 1);
		if (!page->block) {
			DebugMessage(M64MSG_ERROR, "Memory error: Couldn't allocate memory for the dynamic recompiler.");
			return;
		}
	}

	for (i = 0; i < len + 1 + (len >> 2); i++)
	{
		precomp_instr* dst = page->block + i;
		dst->addr = page->start + i*4;
		/* This function cannot be made custom, because self-modifiying code
		 * detection requires that instructions that have never been seen as
		 * code have an 'ops' member of current_instruction_table.NOTCOMPILED. */
		dst->ops = current_instruction_table.NOTCOMPILED;
	}

	/* here we're marking the page as a valid code even if it's not compiled
	 * yet as the game should have already set up the code correctly.
	 */
	invalid_code[page->start>>12] = 0;
	if (page->end < 0x80000000 || page->start >= 0xc0000000)
	{
		unsigned int paddr;

		paddr = virtual_to_physical_address(page->start, 2);
		invalid_code[paddr>>12] = 0;
		if (!blocks[paddr>>12])
		{
			blocks[paddr>>12] = (precomp_block*) malloc(sizeof(precomp_block));
			blocks[paddr>>12]->block = NULL;
			blocks[paddr>>12]->start = paddr & ~0xFFF;
			blocks[paddr>>12]->end = (paddr & ~0xFFF) + 0x1000;
		}
		nd_init_page(blocks[paddr>>12]);

		paddr += page->end - page->start - 4;
		invalid_code[paddr>>12] = 0;
		if (!blocks[paddr>>12])
		{
			blocks[paddr>>12] = (precomp_block*) malloc(sizeof(precomp_block));
			blocks[paddr>>12]->block = NULL;
			blocks[paddr>>12]->start = paddr & ~0xFFF;
			blocks[paddr>>12]->end = (paddr & ~0xFFF) + 0x1000;
		}
		nd_init_page(blocks[paddr>>12]);
	}
	else
	{
		if (page->start >= 0x80000000 && page->end < 0xC0000000 && invalid_code[(page->start ^ 0x20000000) >> 12])
		{
			if (!blocks[(page->start ^ 0x20000000) >> 12])
			{
				blocks[(page->start ^ 0x20000000) >> 12] = (precomp_block*) malloc(sizeof(precomp_block));
				blocks[(page->start ^ 0x20000000) >> 12]->block = NULL;
				blocks[(page->start ^ 0x20000000) >> 12]->start = (page->start ^ 0x20000000) & ~0xFFF;
				blocks[(page->start ^ 0x20000000) >> 12]->end = ((page->start ^ 0x20000000) & ~0xFFF) + 0x1000;
			}
			nd_init_page(blocks[(page->start ^ 0x20000000) >> 12]);
		}
	}
	timed_section_end(TIMED_SECTION_COMPILER);
}

void nd_free_page(precomp_block* page)
{
	if (page->block)
	{
		free(page->block);
		page->block = NULL;
	}
}

void nd_invalidate_code(enum nd_invalidate_reason reason)
{
	switch (reason)
	{
		case INVALIDATE_FULL_CACHE:
			DebugMessage(M64MSG_WARNING, "The dynamic recompiler's code cache is full. Invalidating code.");
			break;
		case INVALIDATE_FPR_LAYOUT_MIPS_I:
		case INVALIDATE_FPR_LAYOUT_MIPS_III:
			if (fpr_layout_changes >= MAX_FPR_LAYOUT_CHANGES)
				return;
			DebugMessage(M64MSG_INFO, "The game is now using %d-bit floating-point registers. Invalidating code.", (reason == INVALIDATE_FPR_LAYOUT_MIPS_III) ? 64 : 32);
			fpr_layout_changes++;
			if (fpr_layout_changes >= MAX_FPR_LAYOUT_CHANGES)
				DebugMessage(M64MSG_INFO, "The game switches often between FPR sizes. New code will have this in mind.");
			break;
	}

	uint32_t i;
	for (i = 0; i < 0x100000; i++)
	{
		if (blocks[i] && blocks[i]->block)
		{
			int len = (blocks[i]->end - blocks[i]->start) / 4, j;
			for (j = 0; j < len + 1 + (len >> 2); j++)
			{
				if ((uint8_t*) blocks[i]->block[j].ops >= code_cache
				 && (uint8_t*) blocks[i]->block[j].ops <  code_cache + CODE_CACHE_SIZE)
					// Seen as code, just not compiled right now.
					blocks[i]->block[j].ops = current_instruction_table.NOTCOMPILED2;
			}
		}
	}

	next_code = code_cache;
}

static bool can_continue_page(const uint32_t offset, const precomp_block* page)
{
	int len = (page->end - page->start) / 4;
	return !(offset >= len - 1 + (len >> 2)
		  || (offset >= len
		   && (page->start == 0xA4000000
		    || page->start >= 0xC0000000
		    || page->end   <  0x80000000)));
}

/*
 * Fills the runtime information structures until, and including, the first
 * unconditional branch and its possible delay slot with the NOTCOMPILED2
 * function.
 * 
 * In the following cases, one or more WRAP_PAGE functions may be inserted
 * at the end instead of getting to an unconditional branch:
 * a) In virtual memory, a WRAP_PAGE is inserted to represent the instruction
 *    past the end of the page, i.e. 0x<Next page>000.
 * b) In physical memory, a WRAP_PAGE is inserted to represent the instruction
 *    past 511 additional instructions from the next page if the last one is
 *    not a branch (for example, an ALU operation).
 * c) In physical memory, two WRAP_PAGE are inserted to represent the delay
 *    slot of a branch and the instruction that follows if the branch fails.
 *    Both of them will be resolved on the next page.
 * 
 * In:
 *   source: A pointer to the start of a 1024-instruction page to be used to
 *     fill the functions.
 *   start: The number of instructions past 'source' at which the first
 *     instruction to be read resides.
 * Out:
 *   page: A pointer to the page to be written. The precomp_instr structures
 *     between page->block[start] and page->block[end] are filled with
 *     the NOTCOMPILED2 function.
 */
static void visit_valid_code(const uint32_t* source, const uint32_t start, precomp_block* page)
{
	timed_section_start(TIMED_SECTION_COMPILER);

	int i, finished = 0, len = (page->end - page->start) / 4;
	n64_insn_t insn;

	// First figure out the range of instructions we will fill with
	// cached interpreter functions.
	for (i = start; can_continue_page(i, page) && finished != 2; i++)
	{
		// Optimisation in time: Stop at the first op that has been seen.
		if (page->block[i].ops != current_instruction_table.NOTCOMPILED)
			break;

		if (page->start < 0x80000000 || page->start >= 0xC0000000)
		{
			unsigned int address2 =
				virtual_to_physical_address(page->start + i * 4, 0);
			if (blocks[address2 >> 12]->block[(address2 & 0xFFF) / 4].ops == current_instruction_table.NOTCOMPILED)
				blocks[address2 >> 12]->block[(address2 & 0xFFF) / 4].ops = current_instruction_table.NOTCOMPILED2; /* seen as code */
		}

		page->block[i].ops = current_instruction_table.NOTCOMPILED2;

		parse_n64_insn(source[i], page->start + i * 4, &insn);

		if (insn.opcode == N64_OP_ERET
		 || finished == 1)
			finished = 2;

		if ((insn.opcode == N64_OP_J
		  || insn.opcode == N64_OP_JR)
		 && !(i >= len - 1
		   && (page->start >= 0xC0000000 || page->end < 0x80000000)))
			finished = 1;
	}

	// Then create WRAP_PAGE pseudo-opcodes at the end that will execute
	// the last instruction in the correct block, if required.
	if (i >= len)
	{
		page->block[i].ops = WRAP_PAGE;
		i++;
		// If a jump/branch and delay slot pair end the page, 2 WRAP_PAGEs are
		// needed; the second is for the untaken branch to land properly.
		if (i < len - 1 + (len >> 2))
		{
			page->block[i].ops = WRAP_PAGE;
			i++;
		}
	}

	timed_section_end(TIMED_SECTION_COMPILER);
}

/*
 * Fills some runtime information structures from parsed instruction
 * structures.
 * 
 * If the 'start' variable is the index of an instruction in 'source' that is
 * not a branch, then the block of structures that is filled ends at the first
 * branch reachable from 'start', and excludes the branch.
 * 
 * If the 'start' variable is the index of an instruction in 'source' that is
 * a branch, then the block of structures that is filled ends after the branch
 * and its possible delay slot.
 * 
 * Regardless of the above, if a runtime information structure already
 * contains WRAP_PAGE (see visit_valid_code), the block ends just before that
 * WRAP_PAGE.
 * 
 * In:
 *   source: A pointer to the start of a 1024-instruction page to be used to
 *     fill the functions.
 *   start: The number of instructions past 'source' at which the first
 *     instruction to be read resides.
 * Out:
 *   page: A pointer to the page to be written. The precomp_instr structures
 *     between page->block[start] and page->block[end] are filled with
 *     the appropriate functions.
 */
static void fill_interpreter_ops(const uint32_t* source, const uint32_t start, precomp_block* page)
{
	n64_insn_t insns[PAGE_INSNS + 1 + (PAGE_INSNS >> 2)];

	uint32_t i, end = start + 1;

	insns[start].runtime = &page->block[start];
	parse_n64_insn(source[start], page->start + start * 4, &insns[start]);
	simplify_n64_insn(&insns[start]);

	// If it's a branch, we need to check if it's _OUT or _IDLE.
	// And we need to compile only the branch and its delay slot,
	// if applicable.
	if (insns[start].branch_flags & BRANCH_N64)
	{
		if ((insns[start].branch_flags & BRANCH_DELAY_SLOT)
		 && can_continue_page(start + 1, page))
		{
			insns[end].runtime = &page->block[end];
			parse_n64_insn(source[end], page->start + end * 4, &insns[end]);
			simplify_n64_insn(&insns[end]);

			end++;
		}
	}
	else
	{
		// To optimise in time, we actually fill the interpreter ops for the
		// following instructions if they don't have emitters, up until the
		// next branch.
		for (i = end; can_continue_page(i, page); i++)
		{
			insns[i].runtime = &page->block[i];
			parse_n64_insn(source[i], page->start + i * 4, &insns[i]);
			if (insns[i].branch_flags & BRANCH_N64)
				break;

			simplify_n64_insn(&insns[i]);
			fill_emit_flags(&insns[i]);
			if (insns[i].emit_flags & INSTRUCTION_HAS_EMITTERS)
				break;
		}
		end = i;
	}

#ifdef ND_SHOW_INTERPRETATION
	printf("  %08" PRIX32 " INT: ", page->start + start * 4);
#endif

	post_parse_n64_insns(&insns[start], end - start, page->start, page->end);

	for (i = start; i < end; i++)
	{
		fill_emit_flags(&insns[i]);
		if (insns[i].emit_flags & INSTRUCTION_HAS_EMITTERS)
			break;
#ifdef ND_SHOW_INTERPRETATION
		printf(" %s", get_n64_op_name(insns[i].opcode));
#endif
		fill_runtime_info(&insns[i]);
	}
#ifdef ND_SHOW_INTERPRETATION
	printf("\n");
#endif
}

static uint32_t get_range_end(const uint32_t* source, const uint32_t start, precomp_block* page, n64_insn_t* insns)
{
	uint32_t i;

	insns[start].runtime = &page->block[start];
	parse_n64_insn(source[start], page->start + start * 4, &insns[start]);
	// It is not possible to create a block starting at the first instruction
	// of a memory page, because that first instruction may be the delay slot
	// of a branch ending the previous page. Making a block of multiple
	// instructions for the delay slot is not allowed.
	// However, if the first instruction of the page is a branch or jump, it
	// would be undefined behaviour for it to be the delay slot of the branch
	// or jump ending the previous page, so we go ahead in that case.
	if (start == 0 && !(insns[start].branch_flags & BRANCH_N64))
	{
#  ifdef ND_SHOW_INTERPRETATION
		printf("Cannot compile the following block, at the start of a page\n");
#  endif
		return start;
	}
	else if (start > 0)
	{
		simplify_n64_insn(&insns[start]);
		// If the instruction preceding the requested start of the block is a
		// branch or a jump with a delay slot, we are compiling a block for
		// this delay slot right now. The interpreter function for that branch
		// or jump would execute this entire block, comprised of many
		// instructions, which is not allowed.
		// So we only allow it if the branch or jump is backed by a custom
		// function, and in that case the custom function will be created on
		// demand.
		insns[start - 1].runtime = &page->block[start - 1];
		parse_n64_insn(source[start - 1], page->start + (start - 1) * 4, &insns[start - 1]);

		if (insns[start - 1].branch_flags & BRANCH_DELAY_SLOT)
		{
			simplify_n64_insn(&insns[start - 1]);
			post_parse_n64_insns(&insns[start - 1], 2, page->start, page->end);
			for (i = start - 1; i < start + 1; i++)
				fill_emit_flags(&insns[i]);
			if (!(insns[start - 1].emit_flags & INSTRUCTION_HAS_EMITTERS)
			 || !(insns[start].emit_flags & INSTRUCTION_HAS_EMITTERS))
			{
#  ifdef ND_SHOW_INTERPRETATION
				printf("Cannot compile the following block as the delay slot of an interpreted branch\n");
#  endif
				return start;
			}
		}
	}

	uint32_t finished = 0, len = (page->end - page->start) / 4, no_emitters = 0;

	for (i = start; can_continue_page(i, page) && finished != 2; i++)
	{
		insns[i].runtime = &page->block[i];
		parse_n64_insn(source[i], page->start + i * 4, &insns[i]);
		simplify_n64_insn(&insns[i]);
		fill_emit_flags(&insns[i]);

		// If 2 consecutive instructions have insufficient emitters, it's
		// likely that, even if one of them is a branch or jump for which
		// we have an in-page emitter but not _OUT or _IDLE, the block has
		// ended.
		if (insns[i].emit_flags & INSTRUCTION_HAS_EMITTERS)
			no_emitters = 0;
		else
		{
			no_emitters++;
			if (no_emitters >= 2)
				break;
		}

		if ((insns[i].branch_flags & (BRANCH_UNCONDITIONAL | BRANCH_DELAY_SLOT)) == BRANCH_UNCONDITIONAL
		 || finished == 1)
			finished = 2;
		else if ((insns[i].branch_flags & (BRANCH_UNCONDITIONAL | BRANCH_DELAY_SLOT)) == (BRANCH_UNCONDITIONAL | BRANCH_DELAY_SLOT)
		      && !(i >= len - 1
		        && (page->start >= 0xC0000000 || page->end < 0x80000000)))
			finished = 1;
	}

	uint32_t end = i;

	// Convert branches to _OUT and _IDLE as appropriate.
	post_parse_n64_insns(&insns[start], end - start, page->start, page->end);

	// Now re-get proper emission flags after all branches are converted.
	for (i = start; i < end; i++)
	{
		fill_emit_flags(&insns[i]);
		if (insns[i].emit_flags & INSTRUCTION_HAS_EMITTERS)
			fill_recompilation_info(&insns[i]);
		else
		{
			end = i;
			break;
		}
	}

	// If a branch or jump starts on the last instruction of a virtual page,
	// or its delay slot cannot be compiled, then it cannot bake the delay
	// slot in. In that case, the branch or jump may not be part of the block.
	// It will stay interpreted.
	if (end > start
	 && (insns[end - 1].branch_flags & BRANCH_DELAY_SLOT))
	{
#  ifdef ND_SHOW_COMPILATION
		printf("Excluding a branch with an interpreted delay slot from the following block\n");
#  endif
		end--;
	}

#  ifdef ND_SHOW_INTERPRETATION
	if (end == start)
		printf("Insufficient JIT emitters for the following block\n");
#  endif

	return end;
}

/**********************************************************************
 ********************* recompile a block of code **********************
 **********************************************************************/
static void nd_recompile(const uint32_t* source, const uint32_t start, precomp_block* page)
{
	timed_section_start(TIMED_SECTION_COMPILER);

	n64_insn_t insns[PAGE_INSNS + 1 + (PAGE_INSNS >> 2)];

	uint32_t end = get_range_end(source, start, page, insns);

	if (end == start)
	{
		fill_interpreter_ops(source, start, page);
		goto end;
	}

	// TODO: Recompile code.
	fill_interpreter_ops(source, start, page);
	goto end;

#  ifdef ND_SHOW_OPS_PER_FUNCTION
	ops_per_func[end - start]++;
#  endif

	if (next_code - code_cache >= CODE_CACHE_SIZE - 131072)
		nd_invalidate_code(INVALIDATE_FULL_CACHE);

	// Now act as a dynamic recompiler for the block.
	// A single function will act as the sequence of opcodes starting at
	// source[start] and ending at the first unconditional jump. Any
	// branch inside the block will set the PC and return if it succeeds, to
	// allow the interpreter loop to consider the PC a new block and call back
	// here. Failing branches will continue in the same block.
	page->block[start].ops = (void (*) (void)) next_code;

#  ifdef ND_SHOW_COMPILATION
	printf("  %08" PRIX32 " JIT: ", page->start + start * 4);
#  endif

end:
	timed_section_end(TIMED_SECTION_COMPILER);
}

/**********************************************************************
 ************** allocate memory with executable bit set ***************
 **********************************************************************/
static void *malloc_exec(size_t size)
{
#if defined(WIN32)
	return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#elif defined(__GNUC__)

	#ifndef  MAP_ANONYMOUS
		#ifdef MAP_ANON
			#define MAP_ANONYMOUS MAP_ANON
		#endif
	#endif

	void *block = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (block == MAP_FAILED)
	{
		DebugMessage(M64MSG_ERROR, "Memory error: couldn't allocate %zi byte block of aligned RWX memory.", size); return NULL;
	}

#ifdef ND_SHOW_MEMORY_ALLOCATION
	DebugMessage(M64MSG_VERBOSE, "Allocated a %u-byte block of executable memory at %08X\n", size, (unsigned int) block);
#endif

	return block;
#else
	return malloc(size);
#endif
}

/**********************************************************************
 **************** frees memory with executable bit set ****************
 **********************************************************************/
static void free_exec(void *ptr, size_t len)
{
#if defined(WIN32)
	VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(__GNUC__)
	munmap(ptr, len);
#else
	free(ptr);
#endif
#ifdef ND_SHOW_MEMORY_ALLOCATION
	DebugMessage(M64MSG_VERBOSE, "Freed a %u-byte block of executable memory at %08X\n", len, (unsigned int) ptr);
#endif
}
