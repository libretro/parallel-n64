#ifndef M64P_R4300_ASSEM_X64_H
#define M64P_R4300_ASSEM_X64_H

#include <stdint.h>
#include <sys/types.h>

#include "../../recomp_types.h"

#define EAX 0
#define ECX 1
#define EDX 2
#define EBX 3
#define ESP 4
#define EBP 5
#define ESI 6
#define EDI 7

#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7
#define R8  8
#define R9  9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

#define HOST_REGS 8
#define HOST_BTREG EBP
#define EXCLUDE_REG ESP
#define HOST_TEMPREG R15

//#define IMM_PREFETCH 1
#define NATIVE_64 1
#define RAM_OFFSET 1
#define INVERTED_CARRY 1
//#define DESTRUCTIVE_WRITEBACK 1
#define DESTRUCTIVE_SHIFT 1
#define USE_MINI_HT 1

#define BASE_ADDR ((intptr_t)(&extra_memory))
#define TARGET_SIZE_2 25 // 2^25 = 32 megabytes
#define JUMP_TABLE_SIZE 0 // Not needed for x64

#ifdef _WIN32
/* Microsoft x64 calling convention:
   func(rcx, rdx, r8, r9) {return rax;}
   The registers RAX, RCX, RDX, R8-R11 are volatile (caller-saved).
   The registers RBX, RBP, RDI, RSI, RSP, R12-R15 are nonvolatile. */
#define ARG1_REG ECX
#define ARG2_REG EDX
#define ARG3_REG R8
#define ARG4_REG R9
#define CALLER_SAVED_REGS 0xF07
#define HOST_CCREG ESI
#else
/* System V amd64 calling convention:
   func(rdi, rsi, rdx, rcx, r8, r9) {return rax;}
   The registers RAX, RCX, RDX, RSI, RDI, R8-R11 are volatile (caller-saved).
   The registers RBX, RBP, RSP, R12-R15 are nonvolatile. */
#define ARG1_REG EDI
#define ARG2_REG ESI
#define ARG3_REG EDX
#define ARG4_REG ECX
#define CALLER_SAVED_REGS 0xFC7
#define HOST_CCREG EBX
#endif

void jump_vaddr_eax(void);
void jump_vaddr_ecx(void);
void jump_vaddr_edx(void);
void jump_vaddr_ebx(void);
void jump_vaddr_ebp(void);
void jump_vaddr_esi(void);
void jump_vaddr_edi(void);
void invalidate_block_eax(void);
void invalidate_block_ecx(void);
void invalidate_block_edx(void);
void invalidate_block_ebx(void);
void invalidate_block_ebp(void);
void invalidate_block_esi(void);
void invalidate_block_edi(void);
void verify_code(void);
void verify_code_vm(void);
void verify_code_ds(void);
void cc_interrupt(void);
void do_interrupt(void);
void fp_exception(void);
void fp_exception_ds(void);
void jump_syscall(void);
void jump_eret(void);
void read_nomem_new(void);
void read_nomemb_new(void);
void read_nomemh_new(void);
void read_nomemd_new(void);
void write_nomem_new(void);
void write_nomemb_new(void);
void write_nomemh_new(void);
void write_nomemd_new(void);
void write_rdram_new(void);
void write_rdramb_new(void);
void write_rdramh_new(void);
void write_rdramd_new(void);
void breakpoint(void);

/* W^X toggling is an arm64/Apple JIT concern; the x64 code cache is a
 * static RWX blob, so these are no-ops here. */
#define apple_jit_wx_unprotect_enter() do{}while(0)
#define apple_jit_wx_unprotect_exit()  do{}while(0)

/* Code cache: a static BSS blob so that RIP-relative 32-bit
 * displacements in generated code always reach the core's data
 * and text (see new_dynarec_init). */
extern u_char extra_memory[1<<TARGET_SIZE_2];

extern int cycle_count;
extern int branch_target;
extern uint64_t readmem_dword;
extern struct precomp_instr fake_pc;
extern uint64_t memory_map[1048576];
extern u_char restore_candidate[512];
/* mini_ht and rounding_modes migrated to struct new_dynarec_hot_state
 * (region 14, Phase 2d increment 1); they are macro-aliased in assem_x64.c
 * onto g_dev.r4300.new_dynarec_hot_state and no longer exist as flat symbols.
 * last_count and ram_offset migrated likewise in increment 2 (with their
 * linkage_x64.asm references repointed in lockstep). restore_candidate stays
 * a flat global -- mupen64plus-next keeps it a flat static too, and it is used
 * in the shared new_dynarec_64.c body (before the x64 assem alias point). */

#endif /* M64P_R4300_ASSEM_X64_H */
